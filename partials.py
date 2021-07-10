import alles, amy, sys

# Sorry I have to add this. getting loris to compile cleanly was a PITA. looking for a better one
sys.path.append('/usr/local/lib/python3.8/site-packages')
import pydub
import loris
import time
import numpy as np
from math import pi
import queue


tests = [
    "/Users/bwhitman/sounds/aps/samples/ADVORCH1/BA LONG FF/BA LGFF#C3.wav",
    "/Users/bwhitman/sounds/aps/samples/ADVORCH2/VIS LONG F/VIS LGF G2.wav"
]

def list_from_py2_iterator(obj, how_many):
    # Oof, the loris object uses some form of iteration that py3 doesn't like. 
    ret = []
    it = obj.iterator()
    for i in range(how_many):
        ret.append(it.next())
    return ret

def loris_synth(filename, freq_res=150, analysis_window=100,amp_floor=-30, max_len_s = 10, noise_ratio=1, hop_time=0.04):
    # Pure loris synth for A/B testing
    audio = pydub.AudioSegment.from_file(filename)
    audio = audio[:int(max_len_s*1000.0)]
    y = np.array(audio.get_array_of_samples())
    if audio.channels == 2:
        y =y.reshape((-1, 2))
        y = y[:,1]
    y = np.float64(y) / 2**15
    analyzer = loris.Analyzer(freq_res, analysis_window)
    analyzer.setAmpFloor(amp_floor)
    analyzer.setHopTime(hop_time)
    partials = analyzer.analyze(y,44100)
    bps = 0
    for i in list_from_py2_iterator(partials, len(partials)):
        bps = bps + len(list_from_py2_iterator(i, i.numBreakpoints()))
    print("%d partials %d bps" % (len(partials), bps))
    loris.scaleNoiseRatio(partials, noise_ratio)
    return loris.synthesize(partials,44100)


def sequence(filename, max_len_s = 10, amp_floor=-30, hop_time=0.04, max_oscs=amy.OSCS, freq_res = 10, analysis_window = 100):
    # my job: take a file, analyze it, output a sequence + some metadata
    # i do voice stealing to keep maximum partials at once to max_oscs 
    # my sequence is an ordered list of partials/oscillators, a list with (ms, osc, freq, amp, bw, phase, time_delta, amp_delta, freq_delta, bw_delta)
    audio = pydub.AudioSegment.from_file(filename)
    audio = audio[:int(max_len_s*1000.0)]
    y = np.array(audio.get_array_of_samples())
    if int(audio.frame_rate) != int(amy.SAMPLE_RATE):
        print("SR mismatch, todo")
        return (None, None)
    if audio.channels == 2:
        y =y.reshape((-1, 2))
        y = y[:,1]
    y = np.float64(y) / 2**15
    metadata = {"filename":filename, "samples":y.shape[0]}

    if(filename.endswith(".wav")):
        import wavdumper # Forked version
        w = wavdumper.Wav(filename)
        w.printInfo() # TODO, call this extractInfo
        try:
            if(w.midiUnityNote>0):
                metadata["midi_note"] = w.midiUnityNote
            if(w.loopstart >= 0 and w.loopend >= 0):
                metadata["sustain_ms"] = int(((w.loopstart + ((w.loopend-w.loopstart)/2.0)) / amy.SAMPLE_RATE) * 1000.0)
        except AttributeError:
            pass # No wav metadata

    # Do the loris analyze
    analyzer = loris.Analyzer(freq_res, analysis_window)
    analyzer.setAmpFloor(amp_floor)
    analyzer.setHopTime(hop_time)
    partials_it = analyzer.analyze(y, audio.frame_rate)
    # build the sequence
    sequence = []
    partials = list_from_py2_iterator(partials_it, partials_it.size())
    partial_count = 0
    for partial_idx, partial in enumerate(partials):
        breakpoints = list_from_py2_iterator(partial, partial.numBreakpoints())
        if(len(breakpoints)>1):
            partial_count = partial_count + 1
            for bp_idx, bp in enumerate(breakpoints):
                phase = -1
                # Last breakpoint?
                if(bp_idx == len(breakpoints)-1): phase = -2
                # First breakpoint
                if(bp_idx == 0):
                    phase = bp.phase() / (2*pi)
                    if(phase < 0): phase = phase + 1
                time_ms = int(bp.time() * 1000.0)
                sequence.append( [time_ms, partial_idx, bp.frequency(), bp.amplitude(), bp.bandwidth(), phase] )

    # Now go and order them and figure out which oscillator gets which partial
    time_ordered = sorted(sequence, key=lambda x:x[0])
    # Clear the sequence
    sequence = []
    min_q_len = max_oscs
    # Now add in a voice / osc # 
    osc_map = {}
    osc_q = queue.Queue(max_oscs) 
    for i in range(max_oscs): osc_q.put(i)
    for i,s in enumerate(time_ordered):
        next_idx = -1
        time_delta, amp_delta, freq_delta, bw_delta = (0,0,0,0)
        if(s[5] != -2): # if not the end of a partial
            next_idx = i+1
            while(time_ordered[next_idx][1] != s[1]):
                next_idx = next_idx + 1
            n = time_ordered[next_idx]
            time_delta = n[0] - s[0]
            amp_delta = n[3]/s[3]
            freq_delta = n[2]/s[2]
            if(s[4]>0):
                bw_delta = n[4]/s[4]
            else:
                bw_delta = 0

        s.append(time_delta)
        s.append(amp_delta)
        s.append(freq_delta)
        s.append(bw_delta)
        if(s[5]>=0): #new partial
            if(not osc_q.empty()):
                osc_map[s[1]] = osc_q.get()
                # Replace the partial_idx with a osc offset
                s[1] = osc_map[s[1]]
                sequence.append(s)
        else:
            osc = osc_map.get(s[1], None)
            if(osc is not None):
                s[1] = osc_map[s[1]]
                sequence.append(s)
                if(s[5] == -2): # last bp
                    # Put the oscillator back
                    osc_q.put(osc)
        if(osc_q.qsize() < min_q_len): min_q_len = osc_q.qsize()
    print("%d partials and %d breakpoints, max oscs used at once was %d" % (partial_count, len(sequence), max_oscs - min_q_len))
    return (metadata, sequence)


def play(sequence, osc_offset=0, sustain_ms = -1, sustain_len_ms = 0, time_ratio = 1, pitch_ratio = 1, amp_ratio = 1, bw_ratio = 1):
    # i take a sequence and play it to AMY, just like native AMY will do from a .h file
    sustain_offset = 0
    if(sustain_ms > 0):
        if(sustain_ms > sequence[-1][0]):
            print("Moving sustain_ms from %d to %d" % (sustain_ms, sequence[-1][0]-100))
            sustain_ms = sequence[-1][0] - 100
    for i,s in enumerate(sequence):
        # Make envelope strings
        bp0 = "%d,%s,0,0" % (s[6] * time_ratio, amy.trunc(s[7]))
        bp1 = "%d,%s,0,0" % (s[6] * time_ratio, amy.trunc(s[8]))
        if(bw_ratio > 0):
            bp2 = "%d,%s,0,0" % (s[6] * time_ratio, amy.trunc(s[9]))
        else:
            bp2 = ""
        if(sustain_ms > 0 and sustain_offset == 0):
            if(s[0]*time_ratio > sustain_ms*time_ratio):
                sustain_offset = sustain_len_ms*time_ratio

        if(s[5]>=0): # start
            amy.send(timestamp=s[0]*time_ratio + sustain_offset, osc=s[1]+osc_offset, wave=alles.SINE, freq=s[2]*pitch_ratio, phase=s[5], vel=s[3]*amp_ratio, feedback=s[4]*bw_ratio, bp0=bp0, bp1=bp1, bp2=bp2, \
                bp0_target=amy.TARGET_AMP+amy.TARGET_LINEAR, bp1_target=amy.TARGET_FREQ+amy.TARGET_LINEAR, bp2_target=amy.TARGET_FEEDBACK+amy.TARGET_LINEAR)
        if(s[5]==-1): # continue
            amy.send(timestamp=s[0]*time_ratio + sustain_offset, osc=s[1]+osc_offset, wave=alles.PARTIAL, freq=s[2]*pitch_ratio, vel=s[3]*amp_ratio, feedback=s[4]*bw_ratio, bp0=bp0, bp1=bp1, bp2=bp2, \
                bp0_target=amy.TARGET_AMP+amy.TARGET_LINEAR, bp1_target=amy.TARGET_FREQ+amy.TARGET_LINEAR, bp2_target=amy.TARGET_FEEDBACK+amy.TARGET_LINEAR)
        if(s[5]==-2): # end
            amy.send(timestamp=s[0]*time_ratio + sustain_offset, osc=s[1]+osc_offset, vel=0, amp=s[3]*amp_ratio)
    return sequence[-1][0]*time_ratio


# OK defaults here
def test(   filename="/Users/bwhitman/sounds/billboard/0157/0157.mp4", \
                    max_len_s=60, \
                    freq_res = 10, \
                    analysis_window = 100, \
                    time_ratio = 1, \
                    max_oscs = 40, \
                    bw_ratio = 0,\
                    amp_ratio = 1, \
                    pitch_ratio = 1, \
                    amp_floor = -40, \
                    hop_time = 0.04, \
                    sustain_len_ms = 0, \
                    **kwargs):
    import sounddevice as sd
    amy.stop()
    amy.start(immediate=False)
    m,s = sequence(filename, max_len_s = max_len_s, freq_res = freq_res, analysis_window = analysis_window, amp_floor=amp_floor, hop_time=hop_time, max_oscs=max_oscs)
    ms = play(s, sustain_ms = m.get("sustain_ms", -1), time_ratio=time_ratio, pitch_ratio=pitch_ratio, amp_ratio=amp_ratio, bw_ratio = bw_ratio, sustain_len_ms = sustain_len_ms)
    sd.play(amy.render(ms/1000.0))
