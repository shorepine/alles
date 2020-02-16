# alles

![picture](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/IMG_2872.jpeg)

Turns an ESP32 & an i2s chip into a remote battery powered 10-voice syntheiszer that responds over wifi using UDP. 

They're cheap to make ($25 each, bring your own battery).

The idea is you can install a bunch of them throughout a space and make a distributed / spatial version of the [Alles Machine](https://en.wikipedia.org/wiki/Bell_Labs_Digital_Synthesizer) / [AMY](https://www.atarimax.com/jindroush.atari.org/achamy.html) additive synthesizer where each speaker represents up to 10 partials, all controlled from a laptop or phone or etc. 

## Putting it together 

currently using

* https://www.adafruit.com/product/3006
* https://www.adafruit.com/product/3405
* https://www.adafruit.com/product/1314 (but any unpowered desktop speaker will work)

LiPo battery is charged via USB powers both boards and a small speaker at pretty good volumes.

Wire it up like

```
LRC -> A1
BCLK -> A0
DIN -> A5
GAIN -> Vin (i jumper this on the breakout)
SD -> NC
GND -> GND
Vin -> 3v3
Speaker connectors -> speaker
```

(Note, you more likely want to feed the i2s amp from 5V if you can, so stay tuned)

![closeup](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/closeup.png)

(note, in this picture the GAIN pin is connected incorrectly, it should be on Vin) 

## Building

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
v = voice, int, 0 to 9 for now
w = waveform, int, 0,1,2,3,4,5,6 [SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF]
a = amplitude, float 0-1 summed over all voices
f = frequency, float 0-22050
n = midinote, 0-127 (note that this interacts with f) 
p = patch, 0-X, choose a preloaded DX7 patch number for FM waveforms
```

Commands are cumulative, state is held. The only required command per message is voice. e.g.

```
v0f440a0.1
v0a0.5
v0w1
```

Will set voice 0 to a sine wave (default) at 440Hz amplitude 0.1, then set amplitude of the voice to 0.5, then change the waveform to a square but keep everything else the same.


Python example:
```
import socket
udp_ip = "192.168.86.66" # see the IP of the ESP32 via make monitor
udp_port = 6001
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def tone(voice=0, type=0, amp=0.1, freq=0):
    sock.sendto("v%dw%da%ff%f" % (voice, type, amp, freq), (udp_ip, udp_port))

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
* SVF filter (maybe use the FM synth's) 
* envelopes
* wifi hotspot for in-field setup
* broadcast UDP for multiples
* case / battery setup



