# AMY - additive music synthesizer library

`AMY` is a C library for additive sound synthesis with a Python wrapper. It is the synthesis engine behind [`alles`](https://github.com/bwhitman/alles). It was designed work on small, memory constrained MCUs like the ESP series, but can be ported to most any system with a FPU. 

`AMY`'s features include: 
 * Support for an arbitrary number of oscillators, each with:
   * Band-limited sine, saw, triangle, pulse waves powered by Dan Ellis' `libblosca`
   * Karplus-strong and noise synthesis 
   * PCM synthesis from a buffer
   * Bandpass, lowpass and hi-pass filters, with adjustable center frequency and resonance
   * Envelope generator that can control amplitude, frequency, pulse width or filter parameters
 * Any oscillator can modulate another using LFO modulation of amplitude, frequency, pulse width or filter parameters
 * Selectable FM-style algorithms for modulating frequency and mixing oscillators
 * Partial synthesis in the style of Alles or Atari's AMY

AMY's wire protocol is a series of ascii characters that define all possible parameters of a voice. You can use any language to pass AMY commands to the synthesizer and render audio samples back using a very simple API:

```c
#include "amy.h"

int16_t * make_music() {
	start_amy(); // initialize the sequencer
	parse_message("v0f440.0w0l0.5t100\n"); // start rendering a 440Hz sine wave on oscillator 0 at 100ms
	return fill_audio_buffer_task(); // render BLOCK_SIZE (128) samples of S16LE ints
}
```

AMY ships with a Python wrapper and the `Alles` project ships with a more high-level descriptive API ontop of AMY. 

```python
import amy
amy.start()
amy.send("v0f440.0w0l0.5t100\n")
samples = amy.render()
amy.stop()
```

Using `alles.py`'s local mode:

```python
import alles
alles.local_start() # starts an audio callback thread to play audio in real time
alles.send(osc=0,freq=3000,amp=1,wave=alles.SINE)
alles.send(osc=1,freq=500,amp=1,wave=alles.SINE,adsr_target=alles.TARGET_AMP,envelope="10,5000,0,0")
alles.send(osc=2,wave=alles.ALGO,algorithm=4,algo_source="0,1")
alles.note_on(osc=2,vel=1,freq=400) # play an FM bell tone
alles.drums() # play a drum pattern
alles.local_stop()
```


## Install the Python library

```
$ cd alles/main/amy
$ python3 setup.py build [or install]
```



