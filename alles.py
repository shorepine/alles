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



def play_patches(voice=0, wave=FM, amp=0.5 ,forever=True, wait=0.750, patch_total = 100):
    once = True
    patch_count = 0
    while (forever or once):
        once=False
        for i in range(12):
            patch = patch_count % patch_total
            patch_count = patch_count + 1
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


def unpack_packed_patch(p):
    # Input is a 128 byte thing from compact.bin
    # Output is a 156 byte thing that the synth knows about
    o = [0]*156
    for op in xrange(6):
        o[op*21:op*21 + 11] = p[op*17:op*17+11]
        leftrightcurves = p[op*17+11]
        o[op * 21 + 11] = leftrightcurves & 3
        o[op * 21 + 12] = (leftrightcurves >> 2) & 3
        detune_rs = p[op * 17 + 12]
        o[op * 21 + 13] = detune_rs & 7
        o[op * 21 + 20] = detune_rs >> 3
        kvs_ams = p[op * 17 + 13]
        o[op * 21 + 14] = kvs_ams & 3
        o[op * 21 + 15] = kvs_ams >> 2
        o[op * 21 + 16] = p[op * 17 + 14]
        fcoarse_mode = p[op * 17 + 15]
        o[op * 21 + 17] = fcoarse_mode & 1
        o[op * 21 + 18] = fcoarse_mode >> 1
        o[op * 21 + 19] = p[op * 17 + 16]
    
    o[126:126+9] = p[102:102+9]
    oks_fb = p[111]
    o[135] = oks_fb & 7
    o[136] = oks_fb >> 3
    o[137:137+4] = p[112:112+4]
    lpms_lfw_lks = p[116]
    o[141] = lpms_lfw_lks & 1
    o[142] = (lpms_lfw_lks >> 1) & 7
    o[143] = lpms_lfw_lks >> 4
    o[144:144+11] = p[117:117+11]
    o[155] = 0x3f

    maxes =  [
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc6
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc5
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc4
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc3
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc2
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, # osc1
        3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
        99, 99, 99, 99, 99, 99, 99, 99, # pitch eg rate & level 
        31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48, # algorithm etc
        126, 126, 126, 126, 126, 126, 126, 126, 126, 126, # name
        127 # operator on/off
    ]
    for i in xrange(156):
        if(o[i] > maxes[i]): o[i] = maxes[i]
        if(o[i] < 0): o[i] = 0

    return o

def generate_patches_header(how_many = 20):
    p = open("main/patches.h", "w")
    p.write("// Automatically generated by alles.generate_patches_header()\n")
    p.write("#ifndef __PATCHES_H\n#define __PATCHES_H\n#define PATCHES %d\n\n" % (how_many))
    p.write("const char patches[%d] = {\n" % (how_many * 156))

    f = bytearray(open("compact.bin").read())
    num_patches = len(f)/128
    for patch in range(how_many):
        patch_data = f[patch*128:patch*128+128]
        #name = str(patch_data[118:128])
        name = ''.join([i if (ord(i) < 128 and ord(i) > 31) else ' ' for i in str(patch_data[118:128])])
        p.write("\t/* %s */ " % (name))
        unpacked = unpack_packed_patch(patch_data)
        for x in unpacked:        
            p.write("%d," % (x))
        p.write("\n")
    p.write("};\n\n#endif  // __PATCHES_H\n")
    p.close()

setup_sock()


if __name__ == "__main__":
	for x in xrange(3):
		c_major(vol=1, octave=x+2)
		time.sleep(10)
	off()