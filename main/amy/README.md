# libAMY - additive music synthesizer library

`AMY` is a C library for additive sound synthesis. It is the synthesis engine behind [`alles`](https://github.com/bwhitman/alles). It was designed work on small, memory constrained MCUs like the ESP series, but can be ported to most any system with a FPU. We ship it with a Python wrapper so you can make nice sounds in Python, too.

`AMY`'s features include: 
 * Support for an arbitrary number of oscillators, each with:
   * Band-limited sine, saw, triangle, pulse waves powered by DAn Ellis' `libblosca`
   * Karplus-strong and noise synthesis 
   * PCM synthesis from a buffer
   * Bandpass, lowpass and hi-pass filters, with adjustable center frequency and resonance
   * 3 breakpoint generators that can control amplitude, frequency, pulse width, partial bandwidth or filter parameters
 * Any oscillator can modulate another using LFO modulation of amplitude, frequency, pulse width or filter parameters
 * Selectable routing algorithms for modulating frequency and mixing oscillators, DX7-like and somewhat compatible
 * Partial breakpoint synthesis in the style of Alles, Synergy or Atari's AMY, with a sinewave decomposition front end

## Controlling AMY

AMY's wire protocol is a series of numbers delimited by ascii characters that define all possible parameters of an oscillator. This is a design decision intended to make using AMY from any sort of environment as easy as possible, with no data structure or parsing overhead on the client. It's also readable and compact, far more expressive than MIDI and can be sent over network links, UARTs, or as arguments to functions or commands. We send AMY messages over multicast UDP to power [`alles`](https://github.com/bwhitman/alles) and have generated AMY messages in C, C++, Python, Max/MSP, shell scripts, JavaScript and more. 

AMY accepts commands in ASCII, each command separated with a newline (you can group multiple messages in one, to avoid network overhead if that's your transport). Like so:

```
v0w4f440.0l0.9\n
```

AMY's full commandset:

```
a = amplitude, float 0-1+. use after a note on is triggered with velocity to adjust amplitude without re-triggering the note
A = breakpoint0, string, in commas, like 100,0.5,150,0.25,200,0 -- envelope generator with alternating time(ms) and ratio. last pair triggers on note off
B = breakpoint1, set the second breakpoint generator. see breakpoint0
b = feedback, float 0-1. use for the ALGO synthesis type, partial synthesis (for bandwidth) or for karplus-strong 
C = breakpoint2, set the third breakpoint generator. see breakpoint0
d = duty cycle, float 0.001-0.999. duty cycle for pulse wave, default 0.5
D = debug, uint, 2-4. 2 shows queue sample, 3 shows oscillator data, 4 shows modified oscillator. will interrupt audio!
f = frequency, float 0-44100 (and above). default 0. Sampling rate of synth is 44,100Hz but higher numbers can be used for PCM waveforms
F = center frequency of biquad filter. default 0. 
g = modulation target mask. Which parameter modulation/LFO controls. 1=amp, 2=duty, 4=freq, 8=filter freq, 16=resonance. Can handle any combo, add them together
G = filter type. 0 = none (default.) 1 = low pass, 2 = band pass, 3 = hi pass. 
I = ratio. for ALGO types, where the base note frequency controls the modulators, or for PARTIALS, where the ratio controls the speed of the partials playback
L = modulation source oscillator. 0-63. Which oscillator is used as an modulation/LFO source for this oscillator. Source oscillator will be silent. 
l = velocity (amplitude), float 0-1+, >0 to trigger note on, 0 to trigger note off.  
n = midinote, uint, 0-127 (this will also set f). default 0
o = algorithm, choose which algorithm for the algorithm oscillator, uint, 0-31. mirrors DX7 algorithms
O = algorithn source oscillators, choose which oscillators make up the algorithm oscillator, like "0,1,2,3,4,5" for algorithm 0
p = patch, uint, 0-999, choose a preloaded PCM sample, partial patch or DX7 patch number for FM waveforms. See patches.h, pcm.h, partials.h. default 0
P = phase, float 0-1. where in the oscillator's cycle to start sampling from (also works on the PCM buffer). default 0
R = q factor / "resonance" of biquad filter. float. in practice, 0 to 10.0. default 0.7.
S = reset oscillator, uint 0-63 or for all oscillators, anything >63, which also resets speaker gain and EQ.
T = breakpoint0 target mask. Which parameter the breakpoints controls. 1=amp, 2=duty, 4=freq, 8=filter freq, 16=resonanc, 32=feedback. Can handle any combo, add them together. Add 64 to indicate linear ramp, otherwise exponential
t = time, int64: ms since some fixed start point on your host. you should always give this if you can.
u = detune, in hertz, for partials and algorithm types, to apply after the ratio 
v = oscillator, uint, 0 to 63. default: 0
V = volume, float 0 to about 10 in practice. volume knob for the entire synth / speaker. default 1.0
w = waveform, uint: [0=SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, KS, PCM, ALGO, PARTIAL, OFF]. default: 0/SINE
W = breakpoint1 target mask. 
x = "low" EQ amount for the entire synth (Fc=800Hz). float, in dB, -15 to 15. 0 is off. default: 0
X = breakpoint2 target mask. 
y = "mid" EQ amount for the entire synth (Fc=2500Hz). float, in dB, -15 to 15. 0 is off. default: 0
z = "high" EQ amount for the entire synth (Fc=7500Hz). float, in dB, -15 to 15. 0 is off. default: 0
```

Synthesizer state is held per oscillator, so you can optionally send only changes in parameters each message per oscillator. Oscillators don't make noise until velocity (`l`) is over 0, you can consider this a "note on" and will trigger envelopes / modulation if set.

You can use any environment to pass AMY commands to the synthesizer and retrieve rendered audio samples back using a very simple API:

```c
#include "amy.h"

int16_t * hello_AMY() {
	start_amy(); // initialize the oscillators and sequencer
	parse_message("v0f440.0w0l0.5t100\n"); // start rendering a 440Hz sine wave on oscillator 0 at 100ms
	return fill_audio_buffer_task(); // render BLOCK_SIZE (128) samples of S16LE ints
}
```

libAMY ships with a Python module and the [`alles`](https://github.com/bwhitman/alles) project ships with demos and patches, alongside network features for that synth.

```python
import amy
amy.start()
amy.send("v0f440.0w0l0.5t100\n")
samples = amy.render(1.0) # seconds
amy.stop()
```

Using `amy.py`'s local mode:

```python
import amy, alles
amy.live() # starts an audio callback thread to play audio in real time
amy.send(osc=0,freq=3000,amp=1,wave=amy.SINE)
amy.send(osc=1,freq=500,amp=1,wave=amy.SINE,bp0_target=amy.TARGET_AMP,bp0="0,0,10,1,5000,0")
amy.send(osc=2,wave=amy.ALGO,algorithm=4,algo_source="0,1")
amy.note_on(osc=2,vel=1,freq=400) # play an FM bell tone
alles.drums() # play a drum pattern
amy.pause()
```


## Compile AMY and install the Python library

```
$ [brew/apt-get] install libsoundio # required for live audio 
$ cd alles/main/amy
$ python3 setup.py install
```

## Synth details

### Partial synthesizer

AMY was imagined first as a partial synthesizer, where a series of sine waves are built and modified over time to recreate harmonic tones. `partials.py` has a number of functions to analyze PCM audio and emit partial commands for AMY to render. Once parameterized, you can modify a partials' time and pitch ratios in real time. You can set parameters in `partials.sequence()` to choose the density of breakpoints and oscillators. 

To install the Loris partial analyzer Python library and use `partials.py`,

```
$ tar xvf loris.tar # from the main `alles` repository
$ cd loris-1.8
$ CPPFLAGS=`python3-config --includes` PYTHON=`which python3` ./configure --with-python
$ make
$ sudo make install
```

```
### FM 

AMY also supports FM synthesis, modeled after the DX7 (but is not an emulator or clone.) The `ALGO` type lets you build up to 6 oscillators (also called operators) that can modulate and mix with each other to create complex tones. [You can read more about the algorithms here](https://djjondent.blogspot.com/2019/10/yamaha-dx7-algorithms.html). (Note our algorithm count starts at 0, so DX7 algorithm 1 is our algorithm 0.)


### Breakpoints

AMY allows you to set 3 "breakpoint generators" per oscillator. You can see these as ADSR / envelopes (and they can perform the same task), but they are slightly more capable. Breakpoints are defined as pairs (up to 8 per breakpoint) of time (specified in milliseconds) and ratio. You can specify any amount of pairs, but the last pair you specify will always be seen as the "release" pair, which doesn't trigger until note off. All other pairs previously have time in the aggregate from note on, e.g. 10ms, then 100ms is 90ms later, then 250ms is 150ms after the last one. The last "release" pair counts from ms from the note-off. 

For example, to define a common ADSR curve where a sound sweeps up in volume from note on over 50ms, then has a 100ms decay stage to 50% of the volume, then is held until note off at which point it takes 250ms to trail off to 0, you'd set time to be 50ms at ratio to be 1.0, then 150ms with ratio .5, then a 250ms release with ratio 0. You then set the target of this breakpoint to be amplitude. At every synthesizer tick, the given amplitude (default of 1.0) will be multiplied by the breakpoint modifier. In AMY string parlance, this would look like "`v0f220w0A50,1.0,150,0.5,250,0T1`" to specify a sine wave at 220Hz with this envelope. 

Every note on (specified by setting velocity / `l` to anything > 0) will trigger this envelope, and setting `l` to 0 will trigger the note off / release section. 

Adding 64 to the target mask `T` will set the breakpoints to compute in linear, while the default is an exponential curve. 

You can set a completely separate breakpoints using the second and third breakpoint operator and target mask, for example, to change pitch and amplitude at different rates.










