import socket, time
udp_ip = ["192.168.86.20", "192.168.86.24"] # see the IP of the ESP32 via make monitor
udp_port = 6001
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

[SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF] = range(7)

def tone(voice=0, type=SINE, patch=-1, amp=-1, note=-1, freq=-1, which=0):
    m = "v%dw%d" % (voice,type)
    if(amp>=0): m = m + "a%f" % (amp)
    if(freq>=0): m = m + "f%f" % (freq)
    if(note>=0): m = m + "n%d" % (note)
    if(patch>=0): m = m + "p%d" % (patch)
    sock.sendto(m, (udp_ip[which], udp_port))


def scale(voice=0, type=FM, amp=0.5, which=0, patch=None):
    while 1:
        for i in range(24):
            if patch is None: patch = i % 20
            tone(voice=voice, type=type, amp=amp, note=40+i, which=which, patch=patch)
            time.sleep(0.5)

def off():
	for x in xrange(10):
		tone(x, amp=0, type=OFF, freq=0, which=0)
		tone(x, amp=0, type=OFF, freq=0, which=1)

def c_major(octave=2,vol=0.2,which=0):
    tone(voice=0,freq=220.5*octave,amp=vol/3.0,which=which)
    tone(voice=1,freq=138.5*octave,amp=vol/3.0,which=which)
    tone(voice=2,freq=164.5*octave,amp=vol/3.0,which=which)

if __name__ == "__main__":
	for x in xrange(3):
		c_major(vol=1, octave=x+2,which=0)
		#c_major(vol=0.1, octave=x+1,which=1)
		time.sleep(10)
	off()