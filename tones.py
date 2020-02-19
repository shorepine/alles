import socket, time, struct, datetime
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ttl = struct.pack('b', 1) 

sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
[SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF] = range(7)

def alles_ms():
    return int((datetime.datetime.utcnow() - datetime.datetime(2020, 2, 18)).total_seconds() * 1000)

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

def tone(voice=0, wave=SINE, patch=-1, amp=-1, note=-1, freq=-1, timestamp=-1, retries=4):
    if(timestamp < 0): timestamp = alles_ms()
    m = "t%dv%dw%d" % (timestamp, voice, wave)
    if(amp>=0): m = m + "a%f" % (amp)
    if(freq>=0): m = m + "f%f" % (freq)
    if(note>=0): m = m + "n%d" % (note)
    if(patch>=0): m = m + "p%d" % (patch)
    for x in range(retries):
        sock.sendto(m, multicast_group)


def scale(voice=0, wave=FM, amp=0.5, which=0, patch=None,forever=True, wait=0.750):
    once = True
    while (forever or once):
        once=False
        for i in range(12):
            if patch is None: patch = i % 20
            tone(voice=voice, wave=wave, amp=amp, note=40+i, which=which, patch=patch)
            time.sleep(wait)


def complex(speed=0.250):
    while 1:
        for i in range(12):
            tone(voice=0, wave=FM, amp=0.1, note=40+i, patch=15)
            time.sleep(speed)
            tone(voice=1, wave=FM, amp=0.2, note=40+i, patch=8)
            time.sleep(speed)
            tone(voice=2, wave=SINE, amp=0.5, note=40+i)
            time.sleep(speed)
            tone(voice=2, wave=SINE, amp=0, note=40+i)
            time.sleep(speed)



def off():
	for x in xrange(10):
		tone(x, amp=0, wave=OFF, freq=0)

def c_major(octave=2,vol=0.2,which=0):
    tone(voice=0,freq=220.5*octave,amp=vol/3.0)
    tone(voice=1,freq=138.5*octave,amp=vol/3.0)
    tone(voice=2,freq=164.5*octave,amp=vol/3.0)

if __name__ == "__main__":
	for x in xrange(3):
		c_major(vol=1, octave=x+2)
		time.sleep(10)
	off()