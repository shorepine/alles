import socket, time, struct, datetime
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ttl = struct.pack('b', 1)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
[SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF] = range(7)

def timestamp_ms():
    return int((datetime.datetime.utcnow() - datetime.datetime(1970, 1, 1)).total_seconds() * 1000)

def sync(count=10, delay_ms=100):
    # Sends sync packets to all the listeners so they can correct / get the time
    # I'm guessing: 
    start = timestamp_ms()
    for i in range(count):
        sock.sendto("s%d" % (timestamp_ms()), multicast_group)
        time.sleep(delay_ms / 1000.0)
    end = timestamp_ms()
    ms_per_call = ((end-start-(count * delay_ms)) / float(count))
    print "Total %d ms. Expected %d ms. Difference %d ms. Calls take %2.2fms extra." % (end-start, count*delay_ms, end-start-(count*delay_ms), ms_per_call)

def tone(voice=0, type=SINE, patch=-1, amp=-1, note=-1, freq=-1, which=0):
    m = "v%dw%d" % (voice,type)
    if(amp>=0): m = m + "a%f" % (amp)
    if(freq>=0): m = m + "f%f" % (freq)
    if(note>=0): m = m + "n%d" % (note)
    if(patch>=0): m = m + "p%d" % (patch)
    sock.sendto(m, multicast_group)


def scale(voice=0, type=FM, amp=0.5, which=0, patch=None,forever=True):
    once = True
    while (forever or once):
        once=False
        for i in range(12):
            if patch is None: patch = i % 20
            tone(voice=voice, type=type, amp=amp, note=40+i, which=which, patch=patch)
            time.sleep(0.1)

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