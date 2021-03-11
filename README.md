# alles

![picture](https://raw.githubusercontent.com/bwhitman/alles/master/pics/set.jpg)

**alles** is a many-speaker distributed mesh synthesizer that responds to control signals over WiFi. Each synth supports up to 10 additive sine, saw, pulse/square, noise and triangle oscillators, a Karplus-Strong string implementation, and a full FM stage including support for DX7 patches. They're cheap to make ($7 for the microcontroller, $6 for the amplifier, speakers from $0.50 up depending on quality). And very easy to put together with hookup wire or only a few soldering points. 

The synthesizers form a mesh and listen to UDP multicast messages. You can control the mesh from a host computer from any programming language or environments like Max or Pd. You can also wire one synth up to MIDI and use any MIDI software or controller; the directly connected synth will broadcast to the rest of the mesh for you. 

The original idea was to install a bunch of them throughout a space and make a distributed / spatial version of an [Alles Machine](https://en.wikipedia.org/wiki/Bell_Labs_Digital_Synthesizer) / [AMY](https://www.atarimax.com/jindroush.atari.org/achamy.html) additive synthesizer where each speaker represents up to 10 partials, all controlled as a group or individually from a laptop or phone or etc. But you can just treat them as dozens / hundreds of individual synthesizers and do whatever you want with them. It's pretty fun!

## Putting it together 

To make one yourself, you need

* [ESP32 dev board (any one will do, but you want pins broken out)](https://www.amazon.com/gp/product/B07Q576VWZ/) (pack of 2, $7.45 each)
* [The Adafruit I2S mono amplifier](https://www.adafruit.com/product/3006) ($5.95)
* [4 ohm speaker, this one is especially nice](https://www.parts-express.com/peerless-by-tymphany-tc6fd00-04-2-full-range-paper-cone-woofer-4-ohm--264-1126?gclid=EAIaIQobChMIwcX3-vXi5wIVgpOzCh0a7gjuEAYYASABEgLwf_D_BwE) ($9.77, but you can save a lot of money here going lower-end if you're ok with the sound quality). I also like speakers with prebuilt cases, like [these bookshelf speakers](https://www.amazon.com/Pyle-PCB3BK-100-Watt-Bookshelf-Speakers/dp/B000MCGF1O/ref=sr_1_1?dchild=1&keywords=pyle+home+speaker&qid=1592156929&s=electronics&sr=1-1).
* A breadboard, custom PCB, or just some hookup wire!

### Power 

A 5V input (USB battery, USB input, rechargeable batteries direct to power input) powers both boards and speaker at pretty good volumes. A 3.7V LiPo battery will also work, but note the I2S amp will not get as loud (without distorting) if you give it 3.7V. I recommend using a USB battery pack that does not do [low current shutoff](https://www.element14.com/community/groups/test-and-measurement/blog/2018/10/15/on-using-a-usb-battery-for-a-portable-project-power-supply). I lucked on [this one at Amazon for $9.44](https://www.amazon.com/gp/product/B00MWU1GGI). The draw of the whole unit at loud volumes is around 150mA, so my battery should power a single synth making sound for 30 hours. 

### Wiring

Wire it up like this (I2S -> ESP)

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

### PCBs

*You don't need any PCBs made to build this synth!* -- it will work with just hookup wire. But if you're making a lot or want more stability, you've got two options so far:

(1) I had a tiny little board made to join the boards together, like so:

![closeup](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/adapter.jpg)

Fritzing file in the `pcbs` folder of this repository, and [it's here on Aisler](https://aisler.net/p/TEBMDZWQ). This is a lot more stable and easier to wire up than snipping small bits of hookup wire, especially for the GAIN connection. 

(2) I am working on a PCB with the ESP32 and i2s amp chip mounted directly on the board. This should be cheaper and smaller and less reliant on the dev-board's quirks. Eagle files in `pcbs` but I'd hold off until I verify this works.


## Firmware

Setup esp-idf: http://esp-idf.readthedocs.io/en/latest/get-started/

Just run `idf.py -p /dev/YOUR_SERIAL_TTY flash` to build and flash to the board after setup.

## Using it

On first boot, each synth will create a captive wifi network called `alles-synth-X` where X is some ID of the synth. Join it, and you should get redirected to a captive wifi setup page. If not, go to `http://10.10.0.1` in your browser. Once you tell each synth what the wifi SSID and password you want it to join are, it will reboot. You only need to do that once per synth.

Alles responds to commands via UDP in ASCII delimited by a character, like

```
v0w4f440.0a0.5
```

Where
```
a = amplitude, float 0-1 summed over all voices. default 0
b = feedback, float 0-1 for karplus-strong. default 0.996
c = client, uint, 0-255 indicating a single client, 256-510 indicating (client_id % (x-255) == 0) for groups, default all clients
d = duty cycle, float 0.001-0.999. duty cycle for pulse wave, default 0.5
e = velocity, uint 0-127, MIDI velocity for the DX7, default 100
f = frequency, float 0-22050. default 0
n = midinote, uint, 0-127 (note that this will also set f). default 0
p = patch, uint, 0-999, choose a preloaded DX7 patch number for FM waveforms. See patches.h and alles.py. default 0
s = sync, int64, same as time but used alone to do an enumeration / sync, see alles.py, also uses i for sync_index
t = time, int64: ms since some fixed start point on your host. you should always give this if you can.
v = voice, uint, 0 to 9. default: 0
w = waveform, uint, 0 to 6 [SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, KS, OFF]. default: 0/SINE
```

Commands are cumulative, state is held per voice. If voice is not given it's assumed to be 0. 

Example:

```
f440a0.1t4500
a0.5t4600
w1t4600
v1w5n50a0.2t5500
v10.4t7000
w2t7000
```

Will set voice 0 (default) to a sine wave (default) at 440Hz amplitude 0.1 at timestamp 4.5s, then set amplitude of voice 0 to 0.5 100ms later, then change the waveform to a square but keep everything else the same. Then set voice 1 to an FM synth playing midi note 50 at amplitude 0.2. Then set voice 1's amplitude to 0.4. Then change voice 0 again to a saw wave.

## Testing

If you are not on wifi, you can test the synthesizers by pushing the BOOT button (if using a dev board) or GPIO0 within 2 seconds *after* applying power. Don't press BOOT while applying power, that puts it in a bootloader mode. The synthesizer will run through a scale and enter immediate mode (see MIDI detail below.)

In normal operation, a small "bleep" noise is made a few seconds after boot to confirm that the synthesizer is ready to receive UDP packets. If you don't hear the bleep, ensure the Wi-Fi authentication is correct.

## Addressing individual synthesizers

By default, a message is played by all booted synthesizers. But you can address them individually or in groups using the `client` parameter.

The synthesizers form a mesh that self-identify who is running. They get auto-addressed `client_id`s starting at 0 through 255. The first synth to be booted in the mesh gets `0`, then `1`, and so on. If a synth is shut off or otherwise no longer sends a heartbeat signal to the mesh, the `client_ids` will reform so that they are always contiguous. A synth may take 10-20 seconds to join the mesh and get assigned a `client_id` after booting, but it will immediately receive messages sent to all synths. 

The `client` parameter wraps around given the number of booted synthesizers to make it easy on the composer. If you have 6 booted synths, a `client` of 0 only reaches the first synth, `1` only reaches the 2nd synth, and a client of `7` reaches the 2nd synth (`7 % 6 = 1`). 

Setting `client` to a number greater than 255 allows you to address groups. For example, a `client` of 257 performs the following check on each booted synthesizer: `my_client_id % (client-255) == 0`. This would only address every other synthesizer. A `client` of 259 would address every fourth synthesizer, and so on.

You can read the heartbeat messages on your host if you want to enumerate the synthesizers locally, see `sync` below. 

## Timing & latency & sync

WiFi, UDP multicast, distance, microcontrollers with bad antennas: all of these are in the way of doing anything close to "real time" control from your host. A message you send from a laptop will arrive between 5ms and 200ms to the connected speakers, and it'll change each time. That's definitely noticeable. We mitigate this by setting a global latency, right now 1000ms, and by allowing any host to send along a `time` parameter of when the host expects the sound to play. `time` can be anything, but I'd suggest using the number of milliseconds since the "alles epoch", e.g.

```
def alles_ms():
    return int((datetime.datetime.utcnow() - datetime.datetime(2020, 2, 1)).total_seconds() * 1000)
```

The first time you send a message the synth uses this to figure out the delta between its time and your expected time. (If you never send a time parameter, you're at the mercy of both fixed latency and jitter.) Further messages will be accurate message-to-message, but with the fixed latency. 

If you want to update this delta (to correct for drift over time or clock base changes) use the `sync` command. See `alles.py`'s implementation, but sending an `sTIMEiINDEX` message will update the delta and also trigger an immediate response back from each on-line synthesizer. The response looks like `_s65201i4c248`, where s is the time on the client, i is the index it is responding to, and c is the client id. This lets you build a map of not only each booted synthesizer, but if you send many messages with different indexes, will also let you figure the round-trip latency for each one along with the reliability. This is helpful when your synths are spread far apart in space and each may have unique latencies. e.g. here we see we have two synths booted on a busy WiFi network.

```
>>> alles.sync(count=10)
{
	248: {'avg_rtt': 319.14285714285717, 'reliability': 0.7}, 
	26: {'avg_rtt': 323.5, 'reliability': 0.8}
}
```

And here's a response with 4 synths on a local wired dedicated router:

```
>>> alles.sync()
{
	3: {'avg_rtt': 4.0, 'reliability': 1.0},
	4: {'avg_rtt': 4.6, 'reliability': 1.0},
	5: {'avg_rtt': 4.2, 'reliability': 1.0},
	6: {'avg_rtt': 5.2, 'reliability': 1.0}
}
```

## WiFi & reliability 

UDP multicast is naturally 'lossy' -- there is no guarantee that a message will be received by a synth. Depending on a lot of factors, but most especially your wireless router and the presence of other devices, that reliability can go between 70% and 99%. In my home network, a many-client Google WiFi mesh, my average round trip latencies are in the high 200 ms range, and my reliability is in the 75% range. On a direct wired Netgear Nighthawk AC2300 with only synthesizers as clients, latencies are well under 50ms and reliability is close to 100%. For performance purposes, I highly suggest using a dedicated wireless router instead of an existing WiFi network. You'll want to be able to turn off many "quality of service" features (these prioritize a randomly chosen synth and will make sync hard to work with), and you'll want to in the best case only have synthesizers as direct WiFi clients. Using a standalone router also helps with WiFi authentication setup -- by default the same login & password is on all synthesizers. If you were using your own network you'll have to recompile with your own authentication, which gets tiresome with many synths. 

An easy way to do this is to set up a dedicated router but not wire any internet into it. Connect your laptop or host machine to the router over a wired connection (via a USB-ethernet adapter if you need one) from the router, but keep your laptop's wifi or other internet network active. In your controlling software, you simply set the source network address to send and receive multicast packets from. See `alles.py` for more details on this. This will keep your host machine on its normal network but allow you to control the synths from a second interface.

If you're in a place where you can't control your network, you can mitigate reliability by simply sending messages N times (2-4). Since individual messages are stateless and can have target timestamps, sending multiple duplicate messages do not have any averse effect on the synths.


## Clients

Simple Python example:

```
import socket
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def tone(voice=0, type=0, amp=0.1, freq=0):
    sock.sendto("v%dw%da%ff%f" % (voice, type, amp, freq), multicast_group)

def c_major(octave=2,vol=0.2):
    tone(voice=0,freq=220.5*octave,amp=vol/3.0)
    tone(voice=1,freq=138.5*octave,amp=vol/3.0)
    tone(voice=2,freq=164.5*octave,amp=vol/3.0)

```

See `alles.py` for a better example.

You can also use it in Max or similar software that supports sending UDP packets. Note that for Max, you'll need your host machine to be on the same wifi network as the synths as it has no easy way to set a source IP address like Python does. Max's `cpuclock` object is a great standin for host time. 

![Max](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/max.png)

## Optional MIDI support

*Still in progress*, but you can now control Alles through MIDI. 

Wire one of your synths to a MIDI in jack, and use standard MIDI programs / DAWs to control the entire mesh. The directly connected synth can broadcast the messages out to the rest of the mesh in sync. Or, if you want, you can control one synth directly via MIDI in "immediate mode", where there is no mesh/network and no latency. 

Put the MIDI input on GPIO 19. You could use a pre-built [MIDI breakout](https://www.sparkfun.com/products/12898) to make it easier to wire up. I've found 3.3V from the ESP32 is fine to power the MIDI in circuit. 

DAWs may start MIDI messages with 0, or 1, so to avoid confusion, I'm using 1 addressing below. In Ableton Live, for example, a Pgm Change of Bank 1 is the first available bank. 

`CHANNEL: 1-16`: sets which synth ID in the mesh you want to send the message to. `1` sends the message to all synths, and `2-16`sends the message to only that ID, minus 1. So to send a message to only the first booted synth, use the second channel.

`"Pgm Change Bank"`: set to "Bank 1" and then use `PROGRAM CHANGE` messages to set the tone. Bank `1` and `PGM` 1 is a sine wave, 2 is a square, and so on like the `w` parameter above. `Bank 2` and onwards are the FM patches. `Bank 2` and `PGM 1` is the first FM patch. `Bank 2 PGM 2` is the second patch. `BANK X PGM Y` is the `(128*(X-2) + (Y-1))` patch. 

MIDI messages will have the default latency added to allow for sync between all your synths. If you want to directly control a synth, and disable the mesh, hold down the `BOOT` button after applying power like in the Testing section above. This will disable wifi and immediately play each MIDI message on the connected synth. 



## THANK YOU TO

* douglas repetto
* dan ellis
* [MSFA](https://github.com/google/music-synthesizer-for-android) for their FM impl
* mark fell
* [esp32 WiFi Manager](https://github.com/tonyp7/esp32-wifi-manager)
* kyle mcdonald 
* blargg for [BlipBuffer](http://slack.net/~ant/libs/audio.html#Blip_Buffer)'s bandlimiting

## TODO

* ~~remove distortion at higher amplitudes for mixed sine waves~~
* ~~FM~~
* ~~should synths self-identify to each other? would make it easier to use in Max~~
* ~~see what you can do about wide swings of UDP latency on the netgear router~~
* envelopes / note on/offs / LFOs
* ~~confirm UDP still works from Max/Pd~~
* ~~bandlimit the square/saw/triangle oscillators~~
* ~~karplus-strong~~ 
* ~~wifi hotspot mode for in-field setup (tbh think it's better to use a dedicated router)~~
* ~~broadcast UDP for multiples~~
* ~~dropped packets~~
* ~~sync and enumerate across multiple devices~~
* ~~addresses / communicate to one or groups, like "play this on half / one-quarter / all"~~
* ~~do what i can about timing / jitter - sync time? timed messages?~~
* ~~case / battery setup~~
* ~~overloading the volume (I think only on FM) crashes~~
* ~~UDP message clicks~~



