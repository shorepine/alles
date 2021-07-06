# AMY functions

from alles_util import get_sock, get_multicast_group
import datetime

try:
	import sounddevice as sd
	import libamy
	import numpy as np
	try:
		import scipy.io.wavfile as wav
		import matplotlib.pyplot as plt
	except ImportError:
		print("libAMY loaded, but no scipy or matlplotlib, can't visualize or save to WAV")
except ImportError:
	print("Couldn't load libAMY. This is fine, but you can't run locally.")



LATENCY_MS = 10
BLOCK_SIZE = 128
SAMPLE_RATE = 44100.0
OSCS = 64
MAX_QUEUE = 400
[SINE, PULSE, SAW, TRIANGLE, NOISE, FM, KS, PCM, ALGO, PARTIAL, OFF] = range(11)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE, TARGET_LINEAR = (1, 2, 4, 8, 16, 32)
FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF = range(4)


stream = None
is_local = False
is_immediate = False
sd.default.samplerate = SAMPLE_RATE
sd.default.channels = 1


def millis():
    # Timestamp to send over to synths for global sync
    # This is a suggestion. I use ms since today started
    d = datetime.datetime.now()
    return int((datetime.datetime.utcnow() - datetime.datetime(d.year, d.month, d.day)).total_seconds()*1000)

def reset(osc=None):
    if(osc is not None):
        send(reset=osc)
    else:
        send(reset=100) # reset > ALLES_OSCS resets all oscs

def volume(volume, client = -1):
    send(0, client=client, volume=volume)


def note_on(vel=1, **kwargs):
    send(vel=vel, **kwargs)

def note_off(**kwargs):
    send(vel=0, **kwargs)

# Buffer messages sent to the synths if you call buffer(). 
# Calling buffer(0) turns off the buffering
# flush() sends whatever is in the buffer now, and is called after buffer(0) as well 
send_buffer = ""
buffer_size = 0

def transmit(message, retries=1):
    if(local()):
        libamy.send(message)
    else:
        for x in range(retries):
            get_sock().sendto(message.encode('ascii'), get_multicast_group())

def buffer(size=508):
    global buffer_size
    buffer_size = size
    if(buffer_size == 0):
        flush()

def flush(retries=1):
    global send_buffer
    transmit(send_buffer)
    send_buffer = ""


# Removes trailing 0s and x.0000s 
def trunc(number):
    return ('%.10f' % number).rstrip('0').rstrip('.')

def send(osc=0, wave=-1, patch=-1, note=-1, vel=-1, amp=-1, freq=-1, duty=-1, feedback=-1, timestamp=None, reset=-1, phase=-1, \
        client=-1, retries=1, volume=-1, filter_freq = -1, resonance = -1, bp0=None, bp1=None, bp0_target=-1, bp1_target=-1, lfo_target=-1, \
        debug=-1, lfo_source=-1, eq_l = -1, eq_m = -1, eq_h = -1, filter_type= -1, algorithm=-1, freq_ratio = -1, algo_source=None):
    global send_buffer, buffer_size, is_immediate
    m = ""
    if(not immediate()):
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
    if(client>=0): m = m + "c" + trunc(client)
    if(amp>=0): m = m + "a" + trunc(amp)
    if(vel>=0): m = m + "l" + trunc(vel)
    if(volume>=0): m = m + "V" + trunc(volume)
    if(resonance>=0): m = m + "R" + trunc(resonance)
    if(filter_freq>=0): m = m + "F" + trunc(filter_freq)
    if(freq_ratio>=0): m = m + "I" + trunc(freq_ratio)
    if(algorithm>=0): m = m + "o" + trunc(algorithm)
    if(bp0 is not None): m = m +"A%s" % (bp0)
    if(bp1 is not None): m = m +"B%s" % (bp1)
    if(algo_source is not None): m = m +"O%s" % (algo_source)
    if(bp0_target>=0): m = m + "T" +trunc(bp0_target)
    if(bp1_target>=0): m = m + "W" +trunc(bp1_target)
    if(lfo_target>=0): m = m + "g" + trunc(lfo_target)
    if(lfo_source>=0): m = m + "L" + trunc(lfo_source)
    if(reset>=0): m = m + "S" + trunc(reset)
    if(debug>=0): m = m + "D" + trunc(debug)
    if(eq_l>=0): m = m + "x" + trunc(eq_l)
    if(eq_m>=0): m = m + "y" + trunc(eq_m)
    if(eq_h>=0): m = m + "z" + trunc(eq_h)
    if(filter_type>=0): m = m + "G" + trunc(filter_type)

    if(buffer_size > 0):
        if(len(send_buffer + m + '\n') > buffer_size):
            transmit(send_buffer)
            send_buffer = m + '\n'
        else:
            send_buffer = send_buffer + m + '\n'
    else:
        transmit(m+'\n')





def local():
	global is_local
	return is_local

def immediate():
	global is_immediate
	return is_immediate	


def spec(data):
	plt.specgram(data, NFFT=512, Fs=SAMPLE_RATE)
	plt.show()

def show(data):
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
	"""Write a waveform to a WAV file."""
	wav.write(filename, int(SAMPLE_RATE), (32768.0 * data).astype(np.int16))

def play(samples):
    sd.play(samples)

def render(seconds):
    # Output a npy array of samples
    frame_count = int((seconds*SAMPLE_RATE)/BLOCK_SIZE)
    frames = []
    for f in range(frame_count):
        frames.append( np.array(libamy.render())/32767.0 )
    return np.hstack(frames)

# TODO - airpods ask for 2880 samples in a block, /64 but not /128
def amy_callback(indata, outdata, frames, time, status):
    single = render(frames/SAMPLE_RATE)
    outdata[:] = single.reshape(single.shape[0],1)

def start():
    global is_local, is_immediate
    if(is_local):
        print("Already started AMY")
    else:
        # Set immediate until live is started
        is_local = True
        is_immediate = True
        libamy.start()

def stop():
    global is_local, is_immediate
    if(not is_local):
        print("AMY not running")
    else:
        libamy.stop()
        is_local = False
        is_immediate = False


def live():
    global stream, is_immediate
    start()
    if stream is not None:
        print("Stream running")
    else:
        is_immediate = False
        stream = sd.Stream(callback=amy_callback)
        stream.start()

def pause():
    global stream
    if stream is None:
        print("Stream already stopped")
    else:
        stream.stop()
        stream = None
        stop()
