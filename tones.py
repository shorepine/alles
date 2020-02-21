import socket, time, struct, datetime, sys, re
multicast_group = ('232.10.11.12', 3333)
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ttl = struct.pack('b', 1) 

sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)
[SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF] = range(7)

def alles_ms():
    return int((datetime.datetime.utcnow() - datetime.datetime(2020, 2, 18)).total_seconds() * 1000)


def sync(count=10, delay_ms=100):
    # Sends sync packets to all the listeners so they can correct / get the time
    # This should also have the receiver on to get the acks back
    rsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rsock.bind(('', 3333))
    group = socket.inet_aton(multicast_group[0])
    mreq = struct.pack('4sL', group, socket.INADDR_ANY)
    rsock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    # Turn off loopback here -- although it doesn't seem to work
    rsock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
    # We're not going to block because only % of things come back
    rsock.setblocking(0)

    clients = {}
    start_time = alles_ms()
    last_sent = 0
    time_sent = {}
    rtt = {}
    i = 0
    while 1:
        tic = alles_ms() - start_time
        if((tic - last_sent) > delay_ms):
            time_sent[i] = alles_ms()
            sock.sendto("s%di%d" % (time_sent[i], i), multicast_group)
            i = i + 1
            last_sent = tic
        try:
            data, address = rsock.recvfrom(1024)
            if(data[0] == '_'):
                [_, client_time, sync_index, client_id] = re.split(r'[sic]',data)
                rtt[int(client_id)] = rtt.get(int(client_id), {})
                rtt[int(client_id)][int(sync_index)] = alles_ms()-time_sent[int(sync_index)]
        except socket.error:
            pass

        # Wait for at least 500ms (client latency) to get any straggling UDP packets back 
        delay_period = 1 + (500 / delay_ms)
        if((i-delay_period) > count):
            break

    # Compute average rtt in ms and reliability (number of rt packets we got)
    for client in rtt.keys():
        hit = 0
        total_rtt_ms = 0
        for i in range(count):
            ms = rtt[client].get(i, None)
            if ms is not None:
                total_rtt_ms = total_rtt_ms + ms
                hit = hit + 1
        clients[client] = {}
        clients[client]["reliability"] = float(hit)/float(count)
        clients[client]["avg_rtt"] = float(total_rtt_ms) / float(hit)

    rsock.close()
    # Return this as a map for future use
    return clients


def tone(voice=0, wave=SINE, patch=-1, amp=-1, note=-1, freq=-1, timestamp=-1, client=-1, retries=4):
    if(timestamp < 0): timestamp = alles_ms()
    m = "t%dv%dw%d" % (timestamp, voice, wave)
    if(amp>=0): m = m + "a%f" % (amp)
    if(freq>=0): m = m + "f%f" % (freq)
    if(note>=0): m = m + "n%d" % (note)
    if(patch>=0): m = m + "p%d" % (patch)
    if(client>0): m = m + "c%d" % (client)
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