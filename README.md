# Alles - the mesh synthesizer

![picture](https://raw.githubusercontent.com/bwhitman/alles/master/pics/set.jpg)

**Alles** is a many-speaker distributed mesh synthesizer that responds over WiFi. Each synth -- there can be hundreds in a mesh -- supports up to 24 additive oscillators, a full FM stage, biquad filter, with LFOs and ADSRs per oscillator. They're open source, cheap and easy to make -- you can build one yourself for about US$20.

The synthesizers automatically form a mesh and listen to multicast WiFi messages. You can control the mesh from a host computer using any programming language or environments like Max or Pd. You can also wire one synth up to MIDI or MIDI over Bluetooth, and use any MIDI software or controller; the directly connected synth will broadcast to the rest of the mesh for you. 

We intended their first use as distributed / spatial version of an [Alles Machine](https://en.wikipedia.org/wiki/Bell_Labs_Digital_Synthesizer) / [AMY](https://www.atarimax.com/jindroush.atari.org/achamy.html) additive synthesizer where each speaker represents up to 10 partials, all controlled as a group or individually. But you can just treat them as dozens of individual synthesizers and do whatever you want with them. It's pretty fun!

Our friends at [Blinkinlabs](https://blinkinlabs.com) are helping us produce small self-contained battery powered speakers with Alles built in. But in the meantime, or if you want to DIY, you can easily build your own! They're cheap to make ($7 for the microcontroller, $6 for the amplifier, speakers from $0.50 up depending on quality). And very easy to put together with hookup wire or only a few soldering points. 

## Synthesizer specs

Each individual synthesizer supports:

 * 24 oscillators, each with adjustable frequency and amplitude:
   * pulse (+ adjustable duty cycle)
   * sine
   * saw
   * triangle
   * noise
   * PCM, reading from a baked-in buffer of percussive and misc samples
   * karplus-strong string with adjustable feedback (can have up to 2 per synth)
   * FM, using a DX7 simulation, with support for DX7 patches and 1000 presets 
 * Biquad low-pass filter with cutoff and resonance at the last stage
 * Oscillators can be specified by frequency in floating point or midi note 
 * Each oscillator has a dedicated ADSR VCA, which can modify any combination of amplitude, frequency, duty, filter cutoff or resonance
 * Each oscillator (except for those using KS or FM) can also act as an LFO to modify any combination of parameters of another oscillator, for example, a bass drum can be indicated via a half phase sine wave at 0.25Hz modulating the frequency of another sine wave. 
 * Speaker gain control


## Using it

On first boot, each synth will create a captive wifi network called `alles-synth-X` where X is some ID of the synth. Join it (preferably on a mobile device), and you should get redirected to a captive wifi setup page. If not, go to `http://10.10.0.1` in your browser after joining the network. Once you tell each synth what the wifi SSID and password you want it to join are, it will reboot. You only need to do that once per synth.

Alles can be used two ways: 

 * **Direct mode**, where you directly control the entire mesh from a computer or mobile device: This is the preferred way and gives you the most functionality. You can control every synth on the mesh from a single host, using UDP over WiFi. You can address any synth in the mesh or all of them at once with one message, or use groups. You can specify synth parameters down to 32 bits of precision, far more than MIDI. This method can be used in music environments like Max or Pd, or by musicians or developers using languages like Python, or for plug-in developers who want to bridge Alles's full features to DAWs.

 * **MIDI mode**, using MIDI over Bluetooth or a MIDI cable: A single Alles synth can be set up as a MIDI relay, by hitting the `MIDI` (or `BOOT0 / GPIO0` on DIY Alles) button. Once in MIDI relay mode, that synth stops making its own sound and acts as a relay to the rest of the mesh. You can connect to the relay over MIDI cable (details below) or wirelessly via MIDI bluetooth, supported by most OSes. You can then control the mesh using any MIDI sequencer or DAW of your choice. You are limited to directly addressing 16 synths in this mode (vs 100s), and lose some control over fine grained parameter tuning. 


In direct mode, Alles responds to commands via UDP in ASCII delimited by a character, like

```
v0w4f440.0l0.9
```

Where
```
A = ADSR envelope, string, in commas, like 100,50,0.5,200 -- A, D, R are in ms, S is in fraction of the peak, 0-1. default 0,0,1,0
b = feedback, float 0-1 for karplus-strong. default 0.996
c = client, uint, 0-255 indicating a single client, 256-510 indicating (client_id % (x-255) == 0) for groups, default all clients
d = duty cycle, float 0.001-0.999. duty cycle for pulse wave, default 0.5
D = debug, uint, 0 or 1. if 1, the monitor will print debug state for all oscillators and CPU usage. will interrupt audio!
f = frequency, float 0-44100 (and above). default 0. Sampling rate of synth is 44,100Hz but higher numbers can be used for PCM waveforms
F = center frequency of biquad filter. 0 is off. default 0. applies to entire synth audio
g = LFO target mask. Which parameter LFO controls. 1=amp, 2=duty, 4=freq, 8=filter freq, 16=resonance. Can handle any combo, add together
L = LFO source oscillator. 0-7. Which oscillator is used as an LFO source for this oscillator. Source oscillator will be silent. 
l = velocity (amplitude), float 0-1, >0 to trigger note on, 0 to trigger note off.  
n = midinote, uint, 0-127 (note that this will also set f). default 0
p = patch, uint, 0-999, choose a preloaded PCM sample or DX7 patch number for FM waveforms. See patches.h, pcm.h. default 0
P = phase, float 0-1. where in the oscillator's cycle to start sampling from (also works on the PCM buffer). default 0
R = q factor / "resonance" of biquad filter. float. in practice, 0 to 10.0. default 0.7.
S = reset oscillator, uint 0-23 or for all oscillators, anything >=24. resets every oscillator parameter to default.
s = sync, int64, same as time but used alone to do an enumeration / sync, see alles.py
T = ADSR target mask. Which parameter ADSR controls. 1=amp, 2=duty, 4=freq, 8=filter freq, 16=resonance. Can handle any combo, add together
t = time, int64: ms since some fixed start point on your host. you should always give this if you can.
v = oscillator, uint, 0 to 23. default: 0
V = volume, float 0 to about 10 in practice. volume knob for the entire synth / speaker. default 1.0
w = waveform, uint, 0 to 8 [SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, KS, PCM, OFF]. default: 0/SINE
```

Synthesizer state is held per oscillator, so you can optionally send only changes in parameters each message per oscillator.


## alles.py 

Alles comes with its own full-featured client, written in Python. Feel free to adapt it or use it in your own clients. It can be seen as documentation, an API as well as a testing suite. You simply `import alles` and can control the entire mesh.

```
$ python3
>>> import alles
>>> alles.drums() # plays a drum pattern on all synths
>>> alles.drums(client=2) # just on one 
```

Or experiment with oscillators:

```
>>> # use a a 0.25Hz sine wave at half phase (going down) to modify frequency of another sine wave
>>> alles.reset()
>>> alles.send(oscillator=1, wave=alles.SINE, vel=0.50, freq=0.25, phase=0.5) # LFO source oscillator
>>> alles.send(oscillator=0, wave=alles.SINE, vel=0, envelope="0,500,0,0", adsr_target=alles.TARGET_AMP, lfo_target=alles.TARGET_FREQ, lfo_source=1)
>>> alles.note_on(oscillator=0, note=60, vel=1.5) # Bass drum!
>>> alles.lowpass(800, 1.5) # filter it
>>> alles.note_on(oscillator=0, note=50, vel=1.5)
```


## Addressing individual synthesizers

By default, a message is played by all booted synthesizers. But you can address them individually or in groups using the `client` parameter.

The synthesizers form a mesh that self-identify who is running. They get auto-addressed `client_id`s starting at 0 through 255. The first synth to be booted in the mesh gets `0`, then `1`, and so on. If a synth is shut off or otherwise no longer sends a heartbeat signal to the mesh, the `client_ids` will reform so that they are always contiguous. A synth may take 10-20 seconds to join the mesh and get assigned a `client_id` after booting, but it will immediately receive messages sent to all synths. 

The `client` parameter wraps around given the number of booted synthesizers to make it easy on the composer. If you have 6 booted synths, a `client` of 0 only reaches the first synth, `1` only reaches the 2nd synth, and a client of `7` reaches the 2nd synth (`7 % 6 = 1`). 

Setting `client` to a number greater than 255 allows you to address groups. For example, a `client` of 257 performs the following check on each booted synthesizer: `my_client_id % (client-255) == 0`. This would only address every other synthesizer. A `client` of 259 would address every fourth synthesizer, and so on.

You can read the heartbeat messages on your host if you want to enumerate the synthesizers locally, see `sync` below. 

## Timing & latency

Alles is not designed as a low latency real-time performance instrument, where your actions have an immediate effect on the sound. Changes you make on the host will take a fixed latency -- currently set at 1000ms -- to get to every synth. This fixed latency ensures that messages arrive to every synth in the mesh in time to play in perfect sync, even though Wi-Fi's transmission latency varies widely. This allows you to have millisecond-accurate timing in your performance across dozens of speakers in a large space. 

Your host should send along the `time` parameter of the relative time when you expect the sound to play. I'd suggest using the number of milliseconds since your host started, e.g. in Python:

```
def millis():
    d = datetime.datetime.now()
    return int((datetime.datetime.utcnow() - datetime.datetime(d.year, d.month, d.day)).total_seconds()*1000)
```

If using Max, use the `cpuclock` object as the `time` parameter. If using MIDI mode, the relay synth's clock is used as a time base. 

The first time you send a message with `time` the synth mesh uses it to figure out the delta between its time and your expected time. (If you never send a time parameter, you're at the mercy of WiFi jitter.) Further messages will be millisecond accurate message-to-message, but with the fixed latency. You can adapt `time` per client if you want to account for speed-of-sound delay. 

The `time` parameter is not meant to schedule things far in the future on the clients. If you send a new `time` that is outside 20,000ms from its expected delta, the clock base will re-compute. Your host should be the main "sequencer" and keep track of performance state and future events. 

## Enumerating synths

The `sync` command (see `alles_util.sync()`) triggers an immediate response back from each on-line synthesizer. The response looks like `_s65201i4c248y2`, where s is the time on the client, i is the index it is responding to, y has battery status (for versions that support that) and c is the client id. This lets you build a map of not only each booted synthesizer, but if you send many messages with different indexes, will also let you figure the round-trip latency for each one along with the reliability. 

## WiFi & reliability for performances

UDP multicast is naturally 'lossy' -- there is no guarantee that a message will be received by a synth. Depending on a lot of factors, but most especially your wireless router and the presence of other devices, that reliability can sometimes go as low as 70%. For performance purposes, I highly suggest using a dedicated wireless router instead of an existing WiFi network. You'll want to be able to turn off many "quality of service" features (these prioritize a randomly chosen synth and will make sync hard to work with), and you'll want to in the best case only have synthesizers as direct WiFi clients. An easy way to do this is to set up a dedicated wireless router but not wire any internet into it. Connect your laptop or host machine to the router over a wired connection (via a USB-ethernet adapter if you need one), but keep your laptop's wifi or other internet network active. In your controlling software, you simply set the source network address to send and receive multicast packets from. `alles_util.py` has setup code for this. This will keep your host machine on its normal network but allow you to control the synths from a second interface.

If you're in a place where you can't control your network, you can mitigate reliability by simply sending messages N times. Sending multiple duplicate messages (with the same `time` parameter) do not have any adverse effect on the synths.


## Clients

Minimal Python example:

```
import socket
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def send(oscillator=0, freq=0, vel=1):
    sock.sendto("v%df%fl%f" % (oscillator, freq, vel), multicast_group)

def c_major(octave=2):
    send(oscillator=0,freq=220.5*octave)
    send(oscillator=1,freq=138.5*octave)
    send(oscillator=2,freq=164.5*octave)

```

See [`alles.py`](https://github.com/bwhitman/alles/blob/master/alles.py) for a better example. Any language that supports sockets and multicast can work, I encourage pull requests with new clients!

You can also easily use it in Max or Pd:

![Max](https://raw.githubusercontent.com/bwhitman/alles/master/pics/max.png)

## MIDI mode

You can also control Alles through MIDI, either wired or over Bluetooth. You can use standard MIDI programs / DAWs to control the entire mesh. The MIDI connected synth will broadcast the messages out to the rest of the mesh in sync. 

Use the MIDI toggle button on the Alles V1 PCB to enter MIDI mode. If using a devboard, use the GPIO0 button. The synth will stop making sound until you press the MIDI button again or reboot it and will only listen for MIDI messages and broadcast those out to the rest of the mesh.

To use BLE MIDI: On a Mac, open Audio MIDI Setup, then show MIDI Studio, then the Bluetooth button, and connect to "Alles MIDI." The Alles MIDI port will then show up in all your MIDI capable software.

![BLE MIDI on Alles](https://raw.githubusercontent.com/bwhitman/alles/master/pics/alles_midi_setup.png)


To use hardwired MIDI: I recommend using a pre-built MIDI breakout with the support hardware -- like this one from [Sparkfun](https://www.sparkfun.com/products/12898) or [Adafruit](https://www.adafruit.com/product/4740) to make it easier to wire up. Connect 3.3V, GND and MIDI to either the devboard (GPIO 19) or the Alles V1 PCB (MIDI header.)

![MIDI on Alles](https://raw.githubusercontent.com/bwhitman/alles/master/pics/alles_midi.png)

`CHANNEL: 1-16`: sets which synth ID in the mesh you want to send the message to. `1` sends the message to all synths, and `2-16`sends the message to only that ID, minus 1. So to send a message to only the first booted synth, use the second channel.

`"Pgm Change Bank"`: set to "Bank 1" and then use `PROGRAM CHANGE` messages to set the tone. Bank `1` and `PGM` 1 is a sine wave, 2 is a square, and so on like the `w` parameter above. `Bank 2` and onwards are the FM patches. `Bank 2` and `PGM 1` is the first FM patch. `Bank 2 PGM 2` is the second patch. `BANK X PGM Y` is the `(128*(X-2) + (Y-1))` patch. 

Currently supported are program / bank changes and note on / offs. Will be adding more CCs soon.


# Developer & DIY Zone

## Building your own DIY Alles 

We are currently testing [rev2 of a all-in-one design for Alles](https://github.com/bwhitman/alles/blob/master/pcbs/2021-03-22_Alles_RevB.pdf). The self-contained version has its own rechargable battery, 4ohm speaker, case and buttons for configuration & setup. We're hoping to be able to sell these in packs for anyone to use. More details soon. 

![blinkinlabs PCB](https://raw.githubusercontent.com/bwhitman/alles/master/pics/alles_reva.png)

But it's still very simple to make one yourself with parts you can get from electronics distributors like Sparkfun, Adafruit or Amazon. 

To make an Alles synth yourself, you need

* [ESP32 dev board (any one will do, but you want pins broken out)](https://www.amazon.com/gp/product/B07Q576VWZ/) (pack of 2, $7.45 each)
* [The Adafruit I2S mono amplifier](https://www.adafruit.com/product/3006) ($5.95)
* [4 ohm speaker, this one is especially nice](https://www.parts-express.com/peerless-by-tymphany-tc6fd00-04-2-full-range-paper-cone-woofer-4-ohm--264-1126?gclid=EAIaIQobChMIwcX3-vXi5wIVgpOzCh0a7gjuEAYYASABEgLwf_D_BwE) ($9.77, but you can save a lot of money here going lower-end if you're ok with the sound quality). I also like speakers with prebuilt cases, like [these bookshelf speakers](https://www.amazon.com/Pyle-PCB3BK-100-Watt-Bookshelf-Speakers/dp/B000MCGF1O/ref=sr_1_1?dchild=1&keywords=pyle+home+speaker&qid=1592156929&s=electronics&sr=1-1).
* A breadboard, custom PCB, or just some hookup wire!

A 5V input (USB battery, USB input, rechargeable batteries direct to power input) powers both boards and speaker at pretty good volumes. A 3.7V LiPo battery will also work, but note the I2S amp will not get as loud (without distorting) if you give it 3.7V. If you want your DIY Alles to be portable, I recommend using a USB battery pack that does not do [low current shutoff](https://www.element14.com/community/groups/test-and-measurement/blog/2018/10/15/on-using-a-usb-battery-for-a-portable-project-power-supply). The draw of the whole unit at loud volumes is around 90mA, and at idle 40mA. 

Wire up your DIY Alles like this (I2S -> ESP)

```
LRC -> GPIO25
BCLK -> GPIO26
DIN -> GPIO27
GAIN -> I2S Vin (i jumper this on the I2S board)
SD -> not connected
GND -> GND
Vin -> Vin / USB / 3.3 (or direct to your 5V power source)
Speaker connectors -> speaker
```


![DIY Alles 1](https://raw.githubusercontent.com/bwhitman/alles/master/pics/diy_alles_1.png)
![DIY Alles 2](https://raw.githubusercontent.com/bwhitman/alles/master/pics/diy_alles_2.png)


### DIY bridge PCB

*You don't need this PCB made to build a DIY Alles!* -- it will work with just hookup wire. But if you're making a lot of DIY Alleses want more stability, I had a tiny little board made to join the boards together, like so:

![closeup](https://raw.githubusercontent.com/bwhitman/alles/master/pics/adapter.jpg)

This assumes you're using the suggested ESP32 dev board with its pin layout. If you use another one, you can probably change the GPIO assignments in `alles.h`. Fritzing file in the `pcbs` folder of this repository, and [it's here on Aisler](https://aisler.net/p/TEBMDZWQ). This is a lot more stable and easier to wire up than snipping small bits of hookup wire, especially for the GAIN connection. 


## Firmware

Alles is completely open source, and can be a fun platform to adapt beyond its current capabilities. To build your own firmware, [start by setting up `esp-idf`](http://esp-idf.readthedocs.io/en/latest/get-started/). If using macOS, you'll want to install the [CP210X drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) if you haven't already. Once you've installed esp-idf and the serial drivers if you need them, run `. ./esp-idf/export.sh`, then connect to your board over USB, clone and cd into this repository and run `idf.py -p /dev/YOUR_SERIAL_TTY flash` to build and flash to the board.

Use `idf.py -p /dev/YOUR_SERIAL_TTY monitor` to reboot the board and see stdout/stderr. Use Ctrl-] to exit the monitor.

## Generating new FM patches or changing the PCM bank

Alles comes prebaked with 1,000 DX7 patches from the [learnFM](https://github.com/bwhitman/learnfm) project. It also comes prebaked with a long buffer of PCM samples, mostly ones that are more complex to synthesize using additive oscillators, for example, closed hi-hats or cymbals. If you build your own firmware, you're free to change both. In [`alles_util.py`](https://github.com/bwhitman/alles/blob/master/alles_util.py) you'll see functions that can regenerate `pcm.h` and `patches.h` for you, by giving it other FM patches, PCM buffers or even SoundFonts. 


## THANK YOU TO

* dan ellis, for many great changes and fixes to the oscillators, and finding out about that weird word-swapping i2s bug!!
* douglas repetto
* [MSFA](https://github.com/google/music-synthesizer-for-android) for their FM impl
* mark fell
* [esp32 WiFi Manager](https://github.com/tonyp7/esp32-wifi-manager)
* kyle mcdonald 
* blargg for [BlipBuffer](http://slack.net/~ant/libs/audio.html#Blip_Buffer)'s bandlimiting
* [BLE-MIDI-IDF](https://github.com/mathiasbredholt/blemidi-idf)
* Matt Mets / [Blinkinlabs](https://blinkinlabs.com)
* [nodemcu-pyflasher](https://github.com/marcelstoer/nodemcu-pyflasher)


## TODO

* ~~see if BLE midi works~~
* ~~power button~~
* ~~wifi setup should ask for default power saving / latency -- no for now~~
* ~~remove distortion at higher amplitudes for mixed sine waves~~
* ~~FM~~
* ~~should synths self-identify to each other? would make it easier to use in Max~~
* ~~see what you can do about wide swings of UDP latency on the netgear router~~
* ~~envelopes / note on/offs / LFOs~~
* ~~confirm UDP still works from Max/Pd~~
* ~~bandlimit the square/saw/triangle oscillators~~
* ~~karplus-strong~~ 
* ~~wifi hotspot mode for in-field setup~~
* ~~broadcast UDP for multiples~~
* ~~dropped packets~~
* ~~sync and enumerate across multiple devices~~
* ~~addresses / communicate to one or groups, like "play this on half / one-quarter / all"~~
* ~~do what i can about timing / jitter - sync time? timed messages?~~
* ~~case / battery setup~~
* ~~overloading the volume (I think only on FM) crashes~~
* ~~UDP message clicks~~
* more MIDI parameters 
* desktop USB flasher
* BT / app based config instead of captive portal (later)



