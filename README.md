# synthserver

![picture](https://raw.githubusercontent.com/bwhitman/synthserver/master/pics/IMG_2872.jpeg)

Turns an ESP32 & an i2s chip into a remote speaker that accepts synthesizer commands over UDP.

currently using

* https://www.adafruit.com/product/3006
* https://www.adafruit.com/product/3405
* https://www.adafruit.com/product/1314

LIPO battery charged via USB powers both + 4W speaker

Wire it up like

```
LRC -> A1
BCLK -> A0
DIN -> A5
GAIN -> GND (i jumper this on the breakout)
SD -> NC
GND -> GND
Vin -> 3v3
```


Send commands via UDP ASCII as

```
voice,type,amplitude{,frequency}

Where 
voice = 0..7
type = 0,1,2,3,4 [SINE, SQUARE, SAW, TRIANGLE, NOISE]
amplitude = float 0-1 summed over all voices
frequency = float 0-22050 

e.g.

0,0,0.4,440.0
```

Python example:
```
udp_ip = "192.168.86.66"
udp_port = 6001
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def tone(voice=0, type=0, amplitude=1.0/8.0, freq=0):
	sock.sendto(str(voice)+","+str(type)+","+str(amplitude)+","+str(freq), (udp_ip, udp_port))
  
```


