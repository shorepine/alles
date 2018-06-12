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


Send sine frequency (for now) via UDP

```
udp_ip = "192.168.86.66"
udp_port = 6001
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto("220", (udp_ip, udp_port))
```


