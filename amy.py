# AMY functions

#TODO , generate these from amy.h when compiling
BLOCK_SIZE = 256
SAMPLE_RATE = 44100.0
OSCS = 64
MAX_QUEUE = 400
[SINE, PULSE, SAW, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, PARTIALS, OFF] = range(11)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE, TARGET_FEEDBACK, TARGET_LINEAR = (1, 2, 4, 8, 16, 32, 64)
FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF = range(4)

def millis():
    import datetime
    # Timestamp to send over to synths for global sync
    # This is a suggestion. I use ms since today started
    d = datetime.datetime.now()
    return int((datetime.datetime.utcnow() - datetime.datetime(d.year, d.month, d.day)).total_seconds()*1000)

# Send to libamy
def send(**kwargs):
    libamy.send(message(**kwargs))


# Construct an AMY message
def message(osc=0, wave=-1, patch=-1, note=-1, vel=-1, amp=-1, freq=-1, duty=-1, feedback=-1, timestamp=None, reset=-1, phase=-1, \
        client=-1, retries=1, volume=-1, filter_freq = -1, resonance = -1, bp0="", bp1="", bp2="", bp0_target=-1, bp1_target=-1, bp2_target=-1, lfo_target=-1, \
        debug=-1, lfo_source=-1, eq_l = -1, eq_m = -1, eq_h = -1, filter_type= -1, algorithm=-1, ratio = -1, detune = -1, algo_source=None):

    # Removes trailing 0s and x.0000s 
    def trunc(number):
        return ('%.10f' % number).rstrip('0').rstrip('.')

    m = ""
    if(timestamp is None): timestamp = millis()
    m = m + "t" + trunc(timestamp)
    if(osc>=0): m = m + "v" + trunc(osc)
    if(wave>=0): m = m + "w" + trunc(wave)
    if(duty>=0): m = m + "d" + trunc(duty)
    if(feedback>=0): m = m + "b" + trunc(feedback)
    if(freq>=0): m = m + "f" + trunc(freq)
    if(note>=0): m = m + "n" + trunc(note)
    if(patch>=0): m = m + "p" + trunc(patch)
    if(phase>=0): m = m + "P" + trunc(phase)
    if(detune>=0): m = m + "u" + trunc(detune)
    if(client>=0): m = m + "c" + trunc(client)
    if(amp>=0): m = m + "a" + trunc(amp)
    if(vel>=0): m = m + "l" + trunc(vel)
    if(volume>=0): m = m + "V" + trunc(volume)
    if(resonance>=0): m = m + "R" + trunc(resonance)
    if(filter_freq>=0): m = m + "F" + trunc(filter_freq)
    if(ratio>=0): m = m + "I" + trunc(ratio)
    if(algorithm>=0): m = m + "o" + trunc(algorithm)
    if(len(bp0)): m = m +"A%s" % (bp0)
    if(len(bp1)): m = m +"B%s" % (bp1)
    if(len(bp2)): m = m +"C%s" % (bp2)
    if(algo_source is not None): m = m +"O%s" % (algo_source)
    if(bp0_target>=0): m = m + "T" +trunc(bp0_target)
    if(bp1_target>=0): m = m + "W" +trunc(bp1_target)
    if(bp2_target>=0): m = m + "X" +trunc(bp2_target)
    if(lfo_target>=0): m = m + "g" + trunc(lfo_target)
    if(lfo_source>=0): m = m + "L" + trunc(lfo_source)
    if(reset>=0): m = m + "S" + trunc(reset)
    if(debug>=0): m = m + "D" + trunc(debug)
    if(eq_l>=0): m = m + "x" + trunc(eq_l)
    if(eq_m>=0): m = m + "y" + trunc(eq_m)
    if(eq_h>=0): m = m + "z" + trunc(eq_h)
    if(filter_type>=0): m = m + "G" + trunc(filter_type)
    return m+'Z'








def spec(data):
    import matplotlib.pyplot as plt
    fig, (s0,s1) = plt.subplots(2,1)
    s0.specgram(data, NFFT=512, Fs=SAMPLE_RATE)
    s1.plot(data)
    fig.show()	

def show(data):
    import matplotlib.pyplot as plt
    import numpy as np
    fftsize = len(data)
    windowlength = fftsize
    window = np.hanning(windowlength)
    wavepart = data[:len(window)]
    logspecmag = 20 * np.log10(np.maximum(1e-10, 
        np.abs(np.fft.fft(wavepart * window)))[:(fftsize // 2 + 1)])
    freqs = SAMPLE_RATE * np.arange(len(logspecmag)) / fftsize
    plt.subplot(211)
    times = np.arange(len(wavepart)) / SAMPLE_RATE
    plt.plot(times, wavepart, '.')
    plt.subplot(212)
    plt.plot(freqs, logspecmag, '.-')
    plt.ylim(np.array([-100, 0]) + np.max(logspecmag))
    plt.show()

def write(data, filename):
    import scipy.io.wavfile as wav
    import numpy as np
    """Write a waveform to a WAV file."""
    print(str(data.shape))
    wav.write(filename, int(SAMPLE_RATE), (32768.0 * data).astype(np.int16))

def play(samples):
    import sounddevice as sd
    sd.play(samples)

def render(seconds):
    import numpy as np
    import libamy
    # Output a npy array of samples
    frame_count = int((seconds*SAMPLE_RATE)/BLOCK_SIZE)
    frames = []
    for f in range(frame_count):
        frames.append( np.array(libamy.render())/32767.0 )
    return np.hstack(frames)

is_local = False
def local(what=None):
    global is_local
    if(what is not None):
        is_local = what
    return is_local

# Starts AMY in local mode -- for debugging 
def start():
    import libamy
    stream = None
    if(local()):
        print("Already started AMY")
    else:
        local(what=True)
        libamy.start()

def stop():
    import libamy
    if(not local()):
        print("AMY not running")
    else:
        libamy.stop()
        local(what=False)

def live():
    import libamy
    start()
    libamy.live()

def pause():
    import libamy
    libamy.pause()
    stop()

