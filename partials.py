import alles, amy, sys

# Sorry I have to add this. getting loris to compile cleanly was a PITA. looking for a better one
sys.path.append('/usr/local/lib/python3.8/site-packages')
import pydub
import loris
import time
import numpy as np
from math import pi
import queue



def parse_wav_file(wav_filename):
    # Use APS code to parse a wave file 
    # This is a forked version of wavdumper that saves these variables and works in py3
    import wavdumper
    w = wavdumper.Wav(wav_filename)
    w.printInfo()
    return (w.midiUnityNote, (w.loopstart, w.loopend), w.samples, w.channels, w.sampleRate)

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

def loris_synth(filename, freq_res=150, analysis_window=100,amp_floor=-100, noise_ratio=1, hop_time=0.01):
    # Pure loris synth for A/B testing
    audio = pydub.AudioSegment.from_file(filename)
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


def sequence(filename, max_len_s = 10, noise_ratio=0, amp_floor=-30, hop_time=0.04, max_oscs=amy.OSCS, freq_res = 150, analysis_window = 100):
    # my job: take a file, analyze it, output a sequence + some metadata
    # i do voice stealing to keep maximum partials at once to max_oscs 
    # my sequence is a list of partials/oscillators, each partial a list with (ms, freq, amp, bw, phase)
    # phase is -1 if a continuation
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
    loris.scaleNoiseRatio(partials_it, noise_ratio)
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

    # Now go and order them
    time_ordered = sorted(sequence, key=lambda x:x[0])
    # Clear the sequence
    sequence = []
    min_q_len = max_oscs
    # Now add in a voice / osc # 
    osc_map = {}
    osc_q = queue.Queue(max_oscs) 
    for i in range(max_oscs): osc_q.put(i)
    for s in time_ordered:
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
    print("%d partials, max oscs used at once was %d" % (partial_count, max_oscs - min_q_len))
    return (metadata, sequence)


def play(sequence, osc_offset=0):
    # i take a sequence and play it to AMY, just like native AMY will do from a .h file
    for i,s in enumerate(sequence):
        # Get the next bp, if there is one, first
        next_idx = -1
        if(s[5] != -2): # if not the end of a partial
            next_idx = i+1
            while(sequence[next_idx][1] != s[1]):
                next_idx = next_idx + 1
            n = sequence[next_idx]
            bp0 = "%d,%s,0,0" % (n[0] - s[0], amy.trunc(n[3]/s[3]))
            bp1 = "%d,%s,0,0" % (n[0] - s[0], amy.trunc(n[2]/s[2]))

        if(s[5]>=0): # start
            amy.send(timestamp=s[0], osc=s[1]+osc_offset, wave=alles.SINE, freq=s[2], phase=s[5], vel=s[3], feedback=s[4], bp0=bp0, bp1=bp1, bp0_target=amy.TARGET_AMP+amy.TARGET_LINEAR, bp1_target=amy.TARGET_FREQ+amy.TARGET_LINEAR)
        if(s[5]==-1): # continue
            amy.send(timestamp=s[0], osc=s[1]+osc_offset, wave=alles.PARTIAL, freq=s[2], vel=s[3], feedback=s[4], bp0=bp0, bp1=bp1, bp0_target=amy.TARGET_AMP+amy.TARGET_LINEAR, bp1_target=amy.TARGET_FREQ+amy.TARGET_LINEAR)
        if(s[5]==-2): # end
            amy.send(timestamp=s[0], osc=s[1]+osc_offset, vel=0, amp=s[3])

#TODO. fix this

# OK defaults here
def partial_test(   filename="/Users/bwhitman/sounds/billboard/0157/0157.mp4", \
                    max_len_s=60, \
                    min_partial_len_s = 0.0, \
                    freq_res = 150, \
                    freq_drift = 50, \
                    analysis_window = 100, \
                    every_n_partial = 1, \
                    every_n_bp = 1, \
                    time_ratio = 1, \
                    max_oscs = 40, \
                    noise_ratio = 0,\
                    amp_mult = 1, \
                    amp_floor = -100, \
                    hop_time = 0.01, \
                    **kwargs):
    a = partial_scheduler(filename, len_s=len_s, min_partial_len_s=min_partial_len_s, freq_res=freq_res, noise_ratio=noise_ratio, \
        freq_drift=freq_drift, analysis_window=analysis_window, every_n_bp=every_n_bp, every_n_partial=every_n_partial, time_ratio=time_ratio, amp_floor = amp_floor, hop_time=hop_time)
    print("Generated %d alles events" % (len(a)))
    play_partial_sequence(a, amp_mult=amp_mult, max_oscs=max_oscs, **kwargs)

