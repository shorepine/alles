import socket, time, struct, datetime, sys
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ttl = struct.pack('b', 1) 

sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
[SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF] = range(7)

def alles_ms():
    return int((datetime.datetime.utcnow() - datetime.datetime(2020, 2, 18)).total_seconds() * 1000)

def sync(count=10, delay_ms=100):
    # Sends sync packets to all the listeners so they can correct / get the time
    start = alles_ms()
    
    # This should also have the receiver on to get the acks back
    rsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rsock.bind(('', 3333))
    group = socket.inet_aton(multicast_group[0])
    mreq = struct.pack('4sL', group, socket.INADDR_ANY)
    rsock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    # Turn off loopback here
    rsock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
    rsock.setblocking(0)


    for i in range(count):
        sock.sendto("s%di%d" % (alles_ms(), i), multicast_group)
        try:
            data, address = rsock.recvfrom(1024)
            print "got %s from %s" % (data, address)
        except socket.error:
            pass

        time.sleep(delay_ms / 1000.0)
    
    end = alles_ms()

    for i in range(count*2):
        try:
            data, address = rsock.recvfrom(1024)
            print "got %s from %s" % (data, address)
        except socket.error:
            pass
        time.sleep(delay_ms/1000.0)

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
            tone(voice=0, wave=FM, amp=0.5, note=50+i, patch=15)
            time.sleep(speed)
            tone(voice=1, wave=FM, amp=0.3, note=50+i, patch=8)
            time.sleep(speed)
            tone(voice=2, wave=FM, amp=0.3, note=50+i, patch=2)
            time.sleep(speed)
            tone(voice=2, wave=OFF)
            time.sleep(speed)

def recv_loop():
    rsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rsock.bind(('', 3333))
    group = socket.inet_aton(multicast_group[0])
    mreq = struct.pack('4sL', group, socket.INADDR_ANY)
    rsock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    # Turn off loopback here
    rsock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)


    while True:
        print >>sys.stderr, '\nwaiting to receive message'
        data, address = rsock.recvfrom(1024)
        
        print >>sys.stderr, 'received %s bytes from %s' % (len(data), address)
        print >>sys.stderr, data



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