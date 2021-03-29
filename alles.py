import socket, time, struct, datetime, sys, re, os

# Setup stuff -- this is the multicast IP & port all the synths listen on
multicast_group = ('232.10.11.12', 3333)
# This is your source IP -- by default your main network interface. 
local_ip = socket.gethostbyname(socket.gethostname())

# But override this if you are using multiple network interfaces, for example a dedicated router to control the synths
# This, for example, is my dev machine's 2nd network interface IP, that I have wired to a separate wifi router
if(os.uname().nodename=='colossus'):
    local_ip = '192.168.1.2'

# Some constants shared with the synth that help
ALLES_LATENCY_MS = 1000
[SINE, PULSE, SAW, TRIANGLE, NOISE, FM, KS, OFF] = range(8)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE = (1, 2, 4, 8, 16)


def set_preset(which,voice=0, client=-1):
    if(which==0): # simple note
        send(voice=voice, wave=SINE, envelope="10,250,0.7,250", adsr_target=TARGET_AMP,timestamp=-1)
    if(which==1): # filter bass
        filter(1000, 2)
        send(voice=voice, wave=SAW, envelope="10,100,0.5,25", adsr_target=TARGET_AMP+TARGET_FILTER_FREQ,timestamp=-1)
    if(which==2): # long square pad to test ADSR
        send(voice=voice, wave=PULSE, envelope="500,1000,0.25,750", adsr_target=TARGET_AMP, timestamp=-1)

def setup_sock():
    # Set up the socket for multicast send & receive
    global sock
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

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
    # This is a suggestion. I use ms since today started
    d = datetime.datetime.now()
    return int((datetime.datetime.utcnow() - datetime.datetime(d.year, d.month, d.day)).total_seconds()*1000)


def decode_battery_mask(mask):
    state = "unknown"
    level = 0
    if (mask & 0x01): state = "charging"
    if (mask & 0x02): state = "charged"
    if (mask & 0x04): state = "discharging"
    if (mask & 0x08): state = "low"
    if (mask & 0x10): level = 4
    if (mask & 0x20): level = 3 
    if (mask & 0x40): level = 2
    if (mask & 0x80): level = 1
    return(state, level)


def sync(count=10, delay_ms=100):
    global sock
    # Sends sync packets to all the listeners so they can correct / get the time
    clients = {}
    client_map = {}
    battery_map = {}
    start_time = alles_ms()
    last_sent = 0
    time_sent = {}
    rtt = {}
    i = 0
    while 1:
        tic = alles_ms() - start_time
        if((tic - last_sent) > delay_ms):
            time_sent[i] = alles_ms()
            #print ("sending %d at %d" % (i, time_sent[i]))
            output = "s%di%d" % (time_sent[i], i)
            sock.sendto(output.encode('ascii'), multicast_group)
            i = i + 1
            last_sent = tic
        try:
            data, address = sock.recvfrom(1024)
            data = data.decode('ascii')
            if(data[0] == '_'):
                try:
                    [_, client_time, sync_index, client_id, ipv4, battery] = re.split(r'[sicry]',data)
                except ValueError:
                    print("What! %s" % (data))
                if(int(sync_index) <= i): # skip old ones from a previous run
                    #print ("recvd at %d:  %s %s %s %s" % (alles_ms(), client_time, sync_index, client_id, ipv4))
                    # ping sets client index to -1, so make sure this is a sync response 
                    if(int(sync_index) >= 0):
                        client_map[int(ipv4)] = int(client_id)
                        battery_map[int(ipv4)] = battery
                        rtt[int(ipv4)] = rtt.get(int(ipv4), {})
                        rtt[int(ipv4)][int(sync_index)] = alles_ms()-time_sent[int(sync_index)]
        except socket.error:
            pass

        # Wait for at least (client latency) to get any straggling UDP packets back 
        delay_period = 1 + (ALLES_LATENCY_MS / delay_ms)
        if((i-delay_period) > count):
            break
    print (str(rtt))
    # Compute average rtt in ms and reliability (number of rt packets we got)
    for ipv4 in rtt.keys():
        hit = 0
        total_rtt_ms = 0
        for i in range(count):
            ms = rtt[ipv4].get(i, None)
            if ms is not None:
                total_rtt_ms = total_rtt_ms + ms
                hit = hit + 1
        clients[client_map[ipv4]] = {}
        clients[client_map[ipv4]]["reliability"] = float(hit)/float(count)
        clients[client_map[ipv4]]["avg_rtt"] = float(total_rtt_ms) / float(hit) # todo compute std.dev
        clients[client_map[ipv4]]["ipv4"] = ipv4
        clients[client_map[ipv4]]["battery"] = decode_battery_mask(int(battery_map[ipv4]))
    # Return this as a map for future use
    return clients


def note_on(voice=-1, wave=-1, freq=-1, note=-1, client=-1, vel=1, patch=-1):
    send(voice=voice, wave=wave, freq=freq, client=client, note=note, patch=patch, vel=vel)

def note_off(voice=-1, client=-1):
    send(voice=voice, client=client, vel=0)


def send(voice=0, wave=-1, patch=-1, note=-1, vel=-1, freq=-1, duty=-1, feedback=-1, timestamp=None, reset=-1, \
        client=-1, retries=1, volume=-1, filter_freq = -1, resonance = -1, envelope=None, adsr_target=-1, lfo_target=-1, lfo_source=-1):
    global sock
    if(timestamp is None): timestamp = alles_ms()
    m = "t%d" % (timestamp)
    if(voice>=0): m = m + "v%d" % (voice)
    if(wave>=0): m = m + "w%d" % (wave)
    if(duty>=0): m = m + "d%f" % (duty)
    if(feedback>=0): m = m + "b%f" % (feedback)
    if(freq>=0): m = m + "f%f" % (freq)
    if(note>=0): m = m + "n%d" % (note)
    if(patch>=0): m = m + "p%d" % (patch)
    if(client>=0): m = m + "c%d" % (client)
    if(vel>=0): m = m + "l%f" % (vel)
    if(volume>=0): m = m + "V%f" % (volume)
    if(resonance>=0): m = m + "R%f" % (resonance)
    if(filter_freq>=0): m = m + "F%f" % (filter_freq)
    if(envelope is not None): m = m +"A%s" %(envelope)
    if(adsr_target>=0): m = m + "T%d" % (adsr_target)
    if(lfo_target>=0): m = m + "g%d" % (lfo_target)
    if(lfo_source>=0): m = m + "L%d" % (lfo_source)
    if(reset>=0): m = m + "S%d" % (reset)
    for x in range(retries):
        sock.sendto(m.encode('ascii'), multicast_group)

def beating_tones(wave=SINE, vol=0.5, cycle_len_ms = 20000, resolution_ms=100):
    start_f = 220
    end_f = 880
    clients = sync()
    # Increase freq slowly from like 220 to 440 but in phase on each one? 
    tic = 0
    beat = 0
    while 1:
        for i,client_id in enumerate(clients.keys()):
            distance = float(tic) / cycle_len_ms # 0 - 1
            base_freq = start_f + (distance * (end_f-start_f))
            freq = base_freq
            if(freq > end_f): freq = freq - (end_f - start_f)
            send(wave=wave, client=client_id, vel=vol, freq=freq, retries=1)
        beat = beat + 1
        tic = tic + resolution_ms
        if(tic > cycle_len_ms): tic = 0
        time.sleep(resolution_ms / 1000.0)

def battery_test():
    tic = time.time()
    clients = 1
    try:
        while clients:
            print("Been %d seconds" % (time.time()-tic))
            clients = len(sync().keys())
            complex(loops=1)
            time.sleep(1)
            off()
            time.sleep(60)
    except KeyboardInterrupt:
            pass
    print("Took %d seconds to stop" %(time.time() - tic))


def test():
    # Plays a test suite
    try:
        while(True):
            for wave in [SINE, SAW, PULSE, TRIANGLE, FM, NOISE]:
                for i in range(12):
                    send(voice=0, wave=wave, vel=0.9, note=40+i, patch=i)
                    time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    off()

def circle(wave=KS, amp=0.5, clients=6):
    i = 0
    warble=-40
    direction =1
    while 1:
        send(voice=0,wave=wave, freq=440+warble, amp=amp, client=i)
        i = (i + 1) % clients
        warble = warble + direction
        if warble > 40:
            direction = -1
        if warble < -40:
            direction = 1
        time.sleep(0.25)

def play_patches(voice=0, wave=FM, amp=0.5 ,forever=True, vel=100, wait=0.750, duty=0.5, patch_total = 100):
    once = True
    patch_count = 0
    while (forever or once):
        once=False
        for i in range(24):
            patch = patch_count % patch_total
            patch_count = patch_count + 1
            send(voice=i % 10, wave=wave, amp=amp, vel=vel, note=40+i, patch=patch, duty=duty)
            time.sleep(wait)


def polyphony():
    voice = 0
    note = 0
    while(1):
        send(voice=voice, wave=FM, patch=note, note=50+note, client = -1)
        time.sleep(0.5)
        voice =(voice + 1) % 9
        note =(note + 1) % 24


def sweep(speed=0.100, res=0.5, loops = -1):
    end = 2000
    cur = 0
    while(loops != 0):
        for i in [0, 1, 4, 5, 1, 3, 4, 5]:
            cur = (cur + 100) % end
            filter(cur, res)
            send(voice=0,wave=PULSE, note=50+i, duty=0.50)
            send(voice=1,wave=PULSE, note=50+12+i, duty=0.25)
            send(voice=2,wave=PULSE, note=50+6+i, duty=0.90)
            time.sleep(speed)


def complex(speed=0.250, vol=1, client =-1, loops=-1):
    while(loops != 0): # -1 means forever 
        for i in [0,2,4,5, 0, 4, 0, 2]:
            note_on(voice=0, wave=FM, vel=0.8, note=50+i, patch=15, client=client)
            time.sleep(speed)
            note_on(voice=1, wave=FM, vel=0.6, note=50+i, patch=8, client=client)
            time.sleep(speed)
            note_on(voice=2, wave=SINE, vel=0.5, note=62+i, patch=2, client=client)
            time.sleep(speed)
            note_on(voice=2, wave=SINE, vel=1, freq = 20, client=client)
            time.sleep(speed)
        loops = loops - 1

def reset(voice=None):
    # Turn off amp per voice and back on again with no wave
    if(voice):
        send(reset=voice)
    else:
        send(reset=100)

def volume(volume, client = -1):
    send(0, client=client, volume=volume, timestamp=-1)

def filter(center, q, client = -1):
    send(0, filter_freq = center, resonance = q, client = client, timestamp=-1)

def c_major(octave=2,wave=SINE, vol=0.2):
    send(voice=0,freq=220.5*octave,amp=vol/3.0, wave=wave)
    send(voice=1,freq=138.5*octave,amp=vol/3.0, wave=wave)
    send(voice=2,freq=164.5*octave,amp=vol/3.0, wave=wave)

def many_voices_fast(wave=KS, vol=0.5):
    # debug the weird underwater effect of many KS voices
    while 1:
        for voice in range(10):
            send(voice=(voice % 10), note=50+voice, amp=vol/10, wave=wave)
            time.sleep(0.2)



def generate_patches_header(how_many = 1000):
    # Generate a list of baked-in DX7 patches from our database of 31,000 patches
    # You're limited to the flash size on your board, which on mine are 2MB
    # You can pick and choose the ones you want, i'm just choosing the first N
    p = open("main/patches.h", "w")
    p.write("// Automatically generated by alles.generate_patches_header()\n")
    p.write("#ifndef __PATCHES_H\n#define __PATCHES_H\n#define PATCHES %d\n\n" % (how_many))
    p.write("const char patches[%d] = {\n" % (how_many * 156))

    # unpacked.bin generated by dx7db, see https://github.com/bwhitman/learnfm
    f = bytearray(open("unpacked.bin").read())
    if(how_many > len(f)/156): how_many = len(f)/156
    for patch in range(how_many):
        patch_data = f[patch*156:patch*156+156]
        # Conver the name to something printable
        name = ''.join([i if (ord(i) < 128 and ord(i) > 31) else ' ' for i in str(patch_data[145:155])])
        p.write("\t/* %s */ " % (name))
        for x in patch_data:        
            p.write("%d," % (x))
        p.write("\n")
    p.write("};\n\n#endif  // __PATCHES_H\n")
    p.close()



# Setup the sock on module import
setup_sock()


