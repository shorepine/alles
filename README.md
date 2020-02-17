# alles

![picture](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/IMG_2872.jpeg)

Turns an ESP32 & an i2s chip & speaker into a WiFi controlled battery powered 10-voice synthesizer. Supports additive sine, saw, square, triangle oscillators as well as a full FM stage, modeled after the DX7 including support for DX7 patches. They're cheap to make ($7 for the ESP, $6 for the I2S amp, speakers from $0.50 up depending on quality). And only a few soldering points. 

The synthesizers listen to UDP multicast messages. The idea is you can install a bunch of them throughout a space and make a distributed / spatial version of the [Alles Machine](https://en.wikipedia.org/wiki/Bell_Labs_Digital_Synthesizer) / [AMY](https://www.atarimax.com/jindroush.atari.org/achamy.html) additive synthesizer where each speaker represents up to 10 partials, all controlled as a group or individually from a laptop or phone or etc. 

## Putting it together 

currently using

* https://www.adafruit.com/product/3006 
* https://www.adafruit.com/product/3405
* https://www.adafruit.com/product/1314 

### Power 

A 5V input (USB battery, USB input, rechargeable batteries direct to power input) powers both boards and a small speaker at pretty good volumes. A 3.7V LiPo battery will also work, but note the I2S amp will not get as loud (without distorting) if you give it 3.7V. I recommend using a USB battery pack that does not do [low current shutoff](https://www.element14.com/community/groups/test-and-measurement/blog/2018/10/15/on-using-a-usb-battery-for-a-portable-project-power-supply). I lucked on [this one at Amazon for $9.44](https://www.amazon.com/gp/product/B00MWU1GGI). The draw of the whole unit at loud volumes is around 100mA, so my battery should power a single synth making sound for 50 hours. 

### Wiring

Wire it up like

```
LRC -> A1
BCLK -> A0
DIN -> A5
GAIN -> Vin (i jumper this on the breakout)
SD -> not connected
GND -> GND
Vin -> 3v3 (or direct to your 5V power source)
Speaker connectors -> speaker
```

![closeup](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/closeup.png)

(please note, in this picture the GAIN pin is connected incorrectly, it should be on Vin) 

## Firmware

Setup esp-idf: http://esp-idf.readthedocs.io/en/latest/get-started/

Make sure to add an auth.h in the main/ folder with 
```
#define WIFI_SSID "your ssid"
#define WIFI_PASS "your password"
```

Just run `idf.py -p /dev/YOUR_SERIAL_TTY flash` to build and flash to the board after setup.

## Using it

Send commands via UDP in ASCII delimited by a character, like

```
v0w4f440.0a0.5
```

Where
```
a = amplitude, float 0-1 summed over all voices. default 0
f = frequency, float 0-22050. default 0
n = midinote, uint, 0-127 (note that this will also set f). default 0
p = patch, uint, 0-X, choose a preloaded DX7 patch number for FM waveforms. default 0
s = sync, int64: ms since epoch, intended as a sync signal. see tones.py
v = voice, uint, 0 to 9. default: 0
w = waveform, uint, 0,1,2,3,4,5,6 [SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF]. default: 0/SINE
```

Commands are cumulative, state is held per voice. If voice is not given it's assumed to be 0. 

Example:

```
f440a0.1
a0.5
w1
v1w5n50a0.2
v10.4
w2
```

Will set voice 0 (default) to a sine wave (default) at 440Hz amplitude 0.1, then set amplitude of voice 0 to 0.5, then change the waveform to a square but keep everything else the same. Then set voice 1 to an FM synth playing midi note 50 at amplitude 0.2. Then set voice 1's amplitude to 0.4. Then change voice 0 again to a saw wave.


Python example:

```
import socket, struct
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, struct.pack('b', 1))

def tone(voice=0, type=0, amp=0.1, freq=0):
    sock.sendto("v%dw%da%ff%f" % (voice, type, amp, freq), multicast_group)

def c_major(octave=2,vol=0.2):
    tone(voice=0,freq=220.5*octave,amp=vol/3.0)
    tone(voice=1,freq=138.5*octave,amp=vol/3.0)
    tone(voice=2,freq=164.5*octave,amp=vol/3.0)

```

See `tones.py` for a better example.

You can also use it in Max or similar software (note you have to wrap string commands in quotes in Max, as otherwise it'll assume it's an OSC message.)

![Max](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/max.png)

## THANK YOU TO

* douglas repetto
* dan ellis
* [MSFA](https://github.com/google/music-synthesizer-for-android) for FM impl

## TODO

* ~~remove distortion at higher amplitudes for mixed sine waves~~
* ~~FM~~
* SVF filter (maybe use the FM synth's) 
* envelopes / note on/offs
* wifi hotspot mode for in-field setup (tbh think it's better to use a dedicated router)
* ~~broadcast UDP for multiples~~
* addresses / communicate to one or groups 
* do what i can about timing / jitter - sync time? timed messages? 
* case / battery setup



