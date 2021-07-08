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


def partial_scheduler(filename, len_s = 10, noise_ratio = 1, freq_res = 150, analysis_window = 100, freq_drift = 50, min_partial_len_s = 0.25, \
        every_n_partial=1, every_n_bp=1, time_ratio=1, amp_floor=-100, hop_time=0.01):
    # Given a file, read it in, SWS analyze it, send it back out as an alles sequence


    # https://www.classes.cs.uchicago.edu/archive/2005/spring/29500-1/Computing_resources/Loris/docs/python_module.html#Breakpoint-phase 
    audio = pydub.AudioSegment.from_file(filename)[:len_s*1000]
    y = np.array(audio.get_array_of_samples())
    if audio.channels == 2:
        y =y.reshape((-1, 2))
    y = y[:,1]
    y = np.float64(y) / 2**15
    analyzer = loris.Analyzer(freq_res, analysis_window)
    #analyzer.setFreqDrift(freq_drift) 
    print("Amp floor set to %f" % (amp_floor))
    analyzer.setAmpFloor(amp_floor)
    analyzer.setHopTime(hop_time)
    partials = analyzer.analyze(y, audio.frame_rate)
    loris.scaleNoiseRatio(partials, noise_ratio)
    sequence = []
    print("%d partials" % (partials.size()))
    # TODO: every_n_partial may skip ones that are big enough, probably makes more sense to grab them all then slice them out
    full_partial_list = list_from_py2_iterator(partials, partials.size())
    long_enough_partials = [partial for partial in full_partial_list if partial.duration() > min_partial_len_s]
    print("but only %d were long enough (%2.2f%%)" % (len(long_enough_partials),len(long_enough_partials)/partials.size()*100.0 ))
    for partial_idx, partial in enumerate(long_enough_partials[::every_n_partial]):
        breakpoints = list_from_py2_iterator(partial, partial.numBreakpoints())
        if(len(breakpoints) > 2):
            first_bp = breakpoints[0]
            # phase is in radians -- -2pi - 2pi, do phase = phase / 2pi, if < 0 then add 1
            phase = first_bp.phase() / (2*pi)
            if(phase < 0): phase = phase + 1
            time_ms = int(first_bp.time() * 1000)*time_ratio
            next_bp = breakpoints[1]
            next_bp_time_ms = int(next_bp.time() * 1000)*time_ratio

            sequence.append( (time_ms, partial_idx, 0, first_bp.frequency(), first_bp.amplitude(), first_bp.bandwidth(), phase, next_bp.frequency(), next_bp.amplitude(), next_bp.bandwidth(), next_bp_time_ms ) )
            for bp_idx, bp in enumerate(breakpoints[1:-1:every_n_bp]):
                time_ms = int(bp.time() * 1000)*time_ratio
                next_bp = breakpoints[bp_idx+2]
                next_bp_time_ms = int(next_bp.time() * 1000)*time_ratio
                sequence.append( (time_ms, partial_idx, bp_idx+1, bp.frequency(), bp.amplitude(), bp.bandwidth(), -1, next_bp.frequency(), next_bp.amplitude(), next_bp.bandwidth(), next_bp_time_ms) )
            # Turn off osc at last one
            last_bp = breakpoints[-1]
            time_ms = int(last_bp.time() * 1000)*time_ratio
            sequence.append( (time_ms, partial_idx, bp_idx + 2, 0, -1, 0, -1, 0, -1, 0, 0) )
    # Now sort by time
    time_ordered = sorted(sequence, key=lambda x:x[0])
    return time_ordered



def play_partial_sequence(sequence, amp_mult=1, max_oscs=amy.OSCS, show_cpu=False, **kwargs):
    time.sleep(1)
    amy.reset()
    amy.buffer()
    (time_ms, partial_idx, bp_idx, freq, amp, bw, phase, next_freq, next_amp, next_bw, next_time_ms) = range(11)
    q = queue.Queue(max_oscs)
    for i in range(int(max_oscs/2)):
        q.put(int(i))
        q.put(int(i+amy.OSCS/2))
    osc_map = {}
    start = alles.millis()
    offset = alles.millis() - sequence[0][time_ms]
    m = 0
    #amy.send(debug=2)
    for event in sequence:
        event_time_ms = (event[time_ms] + offset)
        while(event_time_ms - alles.millis() > 1):
            pass
        # If this is the start of a partial
        if(event[phase]>-1):
            if(not q.empty()):
                osc_map[event[partial_idx]] = q.get()
                m = m + 1
                #print("Time: %d. Partial %d start: freq %f phase %f bw %f amp %f. Next bp is freq %f amp %f, %d ms later. " % (event[time_ms], event[partial_idx], event[freq], event[phase], event[bw], event[amp], event[next_freq], event[next_amp], event[next_time_ms]-event[time_ms]))
                amy.send(osc = osc_map[event[partial_idx]], wave=alles.SINE, freq=event[freq], phase=event[phase], timestamp = event_time_ms, feedback = event[bw], vel=event[amp]*amp_mult, 
                   bp0="%d,%s,0,0" % (event[next_time_ms]-event[time_ms], amy.trunc(event[next_amp] / event[amp])),   bp0_target=amy.TARGET_AMP+amy.TARGET_LINEAR, \
                   bp1="%d,%s,0,0" % (event[next_time_ms]-event[time_ms], amy.trunc(event[next_freq] / event[freq])), bp1_target=amy.TARGET_FREQ+amy.TARGET_LINEAR, **kwargs)
            else:
                # No oscs available for this partial, so skip it
                continue
        else:
            if(event[partial_idx] in osc_map):
                # If this is a normal breakpoint
                if(event[amp]>-1):
                    m = m + 1
                    #print("Time: %d. Partial %d continue: freq %f bw %f amp %f. Next bp is freq %f amp %f, %d ms later. " % (event[time_ms], event[partial_idx], event[freq], event[bw], event[amp], event[next_freq], event[next_amp], event[next_time_ms]-event[time_ms]))
                    amy.send(osc = osc_map[event[partial_idx]], wave=alles.PARTIAL, freq=event[freq], timestamp = event_time_ms, feedback = event[bw], vel=event[amp]*amp_mult,
                       bp0="%d,%s,0,0" % (event[next_time_ms]-event[time_ms], amy.trunc(event[next_amp] / event[amp])),   bp0_target=amy.TARGET_AMP+amy.TARGET_LINEAR, \
                       bp1="%d,%s,0,0" % (event[next_time_ms]-event[time_ms], amy.trunc(event[next_freq] / event[freq])), bp1_target=amy.TARGET_FREQ+amy.TARGET_LINEAR, **kwargs)
                else:
                    # partial is over, free the oscillator
                    m = m + 1
                    #print("Time: %d. Partial %d end" % (event[time_ms], event[partial_idx]))
                    amy.send(osc = osc_map[event[partial_idx]], vel=0, timestamp = event_time_ms, **kwargs)
                    q.put(osc_map[event[partial_idx]])
        if(alles.millis()-start > 1000):
            if(show_cpu): alles.send(debug=1)
            start = alles.millis()
    #print("Voice allocator was able to send %d messages out of %d (%2.2f%%)" % (m, len(sequence), float(m)/len(sequence)*100.0))
    # Wait for the last bits in the latency buffer
    #amy.send(debug=1)
    amy.buffer(size=0)
    time.sleep(1)
    amy.reset()



# OK defaults here
def partial_test(   filename="/Users/bwhitman/sounds/billboard/0157/0157.mp4", \
                    len_s=60, \
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

