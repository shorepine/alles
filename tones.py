import socket, time, struct, datetime, sys, re

# Setup stuff -- this is the multicast IP & port all the synths listen on
multicast_group = ('232.10.11.12', 3333)
# This is your source IP -- by default your main network interface. 
local_ip = socket.gethostbyname(socket.gethostname())
# But override this if you are using multiple network interfaces, for example a dedicated router to control the synths
local_ip = '192.168.1.2'

[SINE, SQUARE, SAW, TRIANGLE, NOISE, FM, OFF] = range(7)

def setup_sock():
    # Set up the socket for multicast send & receive
    global sock
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    # TTL defines how many hops it can take
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 20)
    # Loopback or not, I don't need it
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)
    sock.bind(('', 3333))
    # Set the local interface for multicast receive
    sock.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
    # And the networks to be a member of (destination and host)
    mreq = socket.inet_aton(multicast_group[0]) + socket.inet_aton(local_ip)
    sock.setsockopt(socket.SOL_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    # Don't block to receive -- not necessary and we sometimes drop packets we're waiting for
    sock.setblocking(0)

def shutdown_sock():
    global sock
    # Remove ourselves from membership
    mreq = socket.inet_aton(multicast_group[0]) + socket.inet_aton(local_ip)
    sock.setsockopt(socket.SOL_IP, socket.IP_DROP_MEMBERSHIP, mreq)
    sock.close()

def alles_ms():
    # Timestamp to send over to synths for global sync
    return int((datetime.datetime.utcnow() - datetime.datetime(2020, 2, 18)).total_seconds() * 1000)


def sync(count=10, delay_ms=100):
    global sock
    # Sends sync packets to all the listeners so they can correct / get the time
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
            #print "sending %d at %d" % (i, time_sent[i])
            sock.sendto("s%di%d" % (time_sent[i], i), multicast_group)
            i = i + 1
            last_sent = tic
        try:
            data, address = sock.recvfrom(1024)
            if(data[0] == '_'):
                [_, client_time, sync_index, client_id] = re.split(r'[sic]',data)
                if(int(sync_index) <= i): # skip old ones from a previous run
                    rtt[int(client_id)] = rtt.get(int(client_id), {})
                    rtt[int(client_id)][int(sync_index)] = alles_ms()-time_sent[int(sync_index)]
                    #print "recvd at %d:  %s %s %s" % (alles_ms(), client_time, sync_index, client_id)
                    #print str(rtt)
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
    # Return this as a map for future use
    return clients


def tone(voice=0, wave=SINE, patch=-1, amp=-1, note=-1, freq=-1, timestamp=-1, client=-1, retries=1):
    global sock
    if(timestamp < 0): timestamp = alles_ms()
    m = "t%dv%dw%d" % (timestamp, voice, wave)
    if(amp>=0): m = m + "a%f" % (amp)
    if(freq>=0): m = m + "f%f" % (freq)
    if(note>=0): m = m + "n%d" % (note)
    if(patch>=0): m = m + "p%d" % (patch)
    if(client>0): m = m + "c%d" % (client)
    for x in range(retries):
        sock.sendto(m, multicast_group)

def beating_tones(wave=SINE, vol=0.5, cycle_len_ms = 20000, resolution_ms=100):
    start_f = 220
    end_f = 880
    clients = sync()
    # Increase freq slowly from like 220 to 440 but in phase on each one? 
    tic = 0
    beat = 0
    offset = float(end_f-start_f) / (len(clients.keys())+1)
    while 1:
        for i,client_id in enumerate(clients.keys()):
            distance = float(tic) / cycle_len_ms # 0 - 1
            base_freq = start_f + (distance * (end_f-start_f))
            freq = base_freq
            #freq =  (offset * i) + base_freq
            if(freq > end_f): freq = freq - (end_f - start_f)
            #print "%d %f" % (i, freq)
            tone(wave=wave, client=client_id, amp=0.5*vol, freq=freq, retries=1)
            #if(beat % 4 == 0):
            #    tone(voice=1, wave=FM, amp=0.5*vol, note=50+i, patch=8, client=client_id, retries=1)
        beat = beat + 1
        tic = tic + resolution_ms
        if(tic > cycle_len_ms): tic = 0
        time.sleep(resolution_ms / 1000.0)



def scale(voice=0, wave=FM, amp=0.5,  patch=None,forever=True, wait=0.750):
    once = True
    while (forever or once):
        once=False
        for i in range(12):
            if patch is None: patch = i % 20
            tone(voice=voice, wave=wave, amp=amp, note=40+i, patch=patch)
            time.sleep(wait)


def complex(speed=0.250, vol=1, client =-1):
    while 1:
        for i in [0,2,4,5, 0, 4, 0, 2]:
            tone(voice=0, wave=FM, amp=0.5*vol, note=50+i, patch=15, client=client)
            time.sleep(speed)
            tone(voice=1, wave=FM, amp=0.4*vol, note=50+i, patch=8, client=client)
            time.sleep(speed)
            tone(voice=2, wave=SINE, amp=0.3*vol, note=50+i, patch=2, client=client)
            time.sleep(speed)
            tone(voice=2, wave=OFF,client=client)
            time.sleep(speed)

def off():
	for x in xrange(10):
		tone(x, amp=0, wave=OFF, freq=0)

def c_major(octave=2,vol=0.2):
    tone(voice=0,freq=220.5*octave,amp=vol/3.0)
    tone(voice=1,freq=138.5*octave,amp=vol/3.0)
    tone(voice=2,freq=164.5*octave,amp=vol/3.0)

setup_sock()


if __name__ == "__main__":
	for x in xrange(3):
		c_major(vol=1, octave=x+2)
		time.sleep(10)
	off()