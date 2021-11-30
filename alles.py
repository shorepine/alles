import socket, struct, datetime, os, time

BLOCK_SIZE = 256
SAMPLE_RATE = 44100.0
OSCS = 64
MAX_QUEUE = 400
[SINE, PULSE, SAW, TRIANGLE, NOISE, KS, PCM, ALGO, PARTIAL, PARTIALS, OFF] = range(11)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE, TARGET_FEEDBACK, TARGET_LINEAR = (1, 2, 4, 8, 16, 32, 64)
FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF = range(4)
ALLES_LATENCY_MS = 1000
UDP_PORT = 9294

sock = 0

"""
    A bunch of useful presets
"""
def preset(which,osc=0, **kwargs):
    # Reset the osc first
    reset(osc=osc)
    if(which==0): # simple note
        send(osc=osc, wave=SINE, bp0="10,1,250,0.7,500,0", bp0_target=TARGET_AMP, **kwargs)
    if(which==1): # filter bass
        send(osc=osc, filter_freq=2500, resonance=5, wave=SAW, filter_type=FILTER_LPF, bp0="100,0.5,25,0", bp0_target=TARGET_AMP+TARGET_FILTER_FREQ, **kwargs)

    # TODO -- this is a good one to test the whistle on the bps... 
    if(which==2): # long sine pad to test ADSR
        send(osc=osc, wave=SINE, bp0="0,0,500,1,1000,0.25,750,0", bp0_target=TARGET_AMP, **kwargs)

    if(which==3): # amp LFO example
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=1.5, **kwargs)
        send(osc=osc, wave=PULSE, bp0="150,1,250,0.25,250,0", bp0_target=TARGET_AMP, mod_target=TARGET_AMP, mod_source=osc+1, **kwargs)
    if(which==4): # pitch LFO going up 
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=0.25, **kwargs)
        send(osc=osc, wave=PULSE, bp0="150,1,400,0,0,0", bp0_target=TARGET_AMP, mod_target=TARGET_FREQ, mod_source=osc+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=0.25, phase=0.5, **kwargs)
        send(osc=osc, wave=SINE, vel=0, bp0="500,0,0,0", bp0_target=TARGET_AMP, mod_target=TARGET_FREQ, mod_source=osc+1, **kwargs)
    if(which==6): # noise snare
        send(osc=osc, wave=NOISE, vel=0, bp0="250,0,0,0", bp0_target=TARGET_AMP, **kwargs)
    if(which==7): # closed hat
        send(osc=osc, wave=NOISE, vel=0, bp0="25,1,75,0,0,0", bp0_target=TARGET_AMP, **kwargs)
    if(which==8): # closed hat from PCM 
        send(osc=osc, wave=PCM, vel=0, patch=0, freq=0, **kwargs)
    if(which==9): # cowbell from PCM
        send(osc=osc, wave=PCM, vel=0, patch=10, freq=0, **kwargs)
    if(which==10): # high cowbell from PCM
        send(osc=osc, wave=PCM, vel=0, patch=10, note=70, **kwargs)
    if(which==11): # snare from PCM
        send(osc=osc, wave=PCM, vel=0, patch=5, freq=0, **kwargs)
    if(which==12): # FM bass 
        send(osc=osc, wave=ALGO, vel=0, patch=21, **kwargs)
    if(which==13): # Pcm bass drum
        send(osc=osc, wave=PCM, vel=0, patch=1, freq=0, **kwargs)
    if(which==14): # filtered algo 
        send(wave=ALGO, vel=0, patch=62,filter_freq=2000,resonance=2.5,filter_type=FILTER_LPF, bp0_target=TARGET_FILTER_FREQ,bp0="1,1,500,0,0,0")


# Buffer messages sent to the synths if you call buffer(). 
# Calling buffer(0) turns off the buffering
# flush() sends whatever is in the buffer now, and is called after buffer(0) as well 
send_buffer = ""
buffer_size = 0



def millis():
    import datetime
    # Timestamp to send over to synths for global sync
    # This is a suggestion. I use ms since today started
    d = datetime.datetime.now()
    return int((datetime.datetime.utcnow() - datetime.datetime(d.year, d.month, d.day)).total_seconds()*1000)



# Removes trailing 0s and x.0000s 
def trunc(number):
    return ('%.10f' % number).rstrip('0').rstrip('.')

# Construct an AMY message
def message(osc=0, wave=-1, patch=-1, note=-1, vel=-1, amp=-1, freq=-1, duty=-1, feedback=-1, timestamp=None, reset=-1, phase=-1, \
        client=-1, retries=1, volume=-1, filter_freq = -1, resonance = -1, bp0="", bp1="", bp2="", bp0_target=-1, bp1_target=-1, bp2_target=-1, mod_target=-1, \
        debug=-1, mod_source=-1, eq_l = -1, eq_m = -1, eq_h = -1, filter_type= -1, algorithm=-1, ratio = -1, detune = -1, algo_source=None):

    m = ""
    if(timestamp is None): timestamp = millis()
    m = m + "t" + trunc(timestamp)
    if(osc>=0): m = m + "v" + trunc(osc)
    if(wave>=0): m = m + "w" + trunc(wave)
    if(duty>=0): m = m + "d" + trunc(duty)
    if(feedback>=0): m = m + "b" + trunc(feedback)
    if(freq>=0): m = m + "f" + trunc(freq)
    if(note>=0): m = m + "n" + trunc(note)
    if(patch>=0): m = m + "p" + trunc(patch)
    if(phase>=0): m = m + "P" + trunc(phase)
    if(detune>=0): m = m + "u" + trunc(detune)
    if(client>=0): m = m + "c" + trunc(client)
    if(amp>=0): m = m + "a" + trunc(amp)
    if(vel>=0): m = m + "l" + trunc(vel)
    if(volume>=0): m = m + "V" + trunc(volume)
    if(resonance>=0): m = m + "R" + trunc(resonance)
    if(filter_freq>=0): m = m + "F" + trunc(filter_freq)
    if(ratio>=0): m = m + "I" + trunc(ratio)
    if(algorithm>=0): m = m + "o" + trunc(algorithm)
    if(len(bp0)): m = m +"A%s" % (bp0)
    if(len(bp1)): m = m +"B%s" % (bp1)
    if(len(bp2)): m = m +"C%s" % (bp2)
    if(algo_source is not None): m = m +"O%s" % (algo_source)
    if(bp0_target>=0): m = m + "T" +trunc(bp0_target)
    if(bp1_target>=0): m = m + "W" +trunc(bp1_target)
    if(bp2_target>=0): m = m + "X" +trunc(bp2_target)
    if(mod_target>=0): m = m + "g" + trunc(mod_target)
    if(mod_source>=0): m = m + "L" + trunc(mod_source)
    if(reset>=0): m = m + "S" + trunc(reset)
    if(debug>=0): m = m + "D" + trunc(debug)
    if(eq_l>=0): m = m + "x" + trunc(eq_l)
    if(eq_m>=0): m = m + "y" + trunc(eq_m)
    if(eq_h>=0): m = m + "z" + trunc(eq_h)
    if(filter_type>=0): m = m + "G" + trunc(filter_type)
    return m+'Z'

def transmit(message, retries=1):
    for x in range(retries):
        get_sock().sendto(message.encode('ascii'), get_multicast_group())

def buffer(size=508):
    global buffer_size
    buffer_size = size
    if(buffer_size == 0):
        flush()

def flush(retries=1):
    global send_buffer
    transmit(send_buffer)
    send_buffer = ""

def send(retries=1, **kwargs):
    global send_buffer
    m = message(**kwargs)
    if(buffer_size > 0):
        if(len(send_buffer + m) > buffer_size):
            transmit(send_buffer, retries=retries)
            send_buffer = m
        else:
            send_buffer = send_buffer + m
    else:
        transmit(m,retries=retries)

"""
    Convenience functions
"""

def reset(osc=None):
    if(osc is not None):
        send(reset=osc)
    else:
        send(reset=100) # reset > ALLES_OSCS resets all oscs

def volume(volume, client = -1):
    send(client=client, volume=volume)


"""
    Run a scale through all the synth's sounds
"""
def test():
    while True:
        for wave in [SINE, SAW, PULSE, TRIANGLE, NOISE]:
            for i in range(12):
                send(osc=0, wave=wave, note=40+i, patch=i, vel=1)
                time.sleep(0.5)


"""
    Play all of the FM patches in order
"""
def play_patches(wait=0.500, patch_total = 100, **kwargs):
    once = True
    patch_count = 0
    while True:
        for i in range(24):
            patch = patch_count % patch_total
            patch_count = patch_count + 1
            send(osc=i % OSCS, note=i+50, wave=ALGO, patch=patch, vel=1, **kwargs)
            time.sleep(wait)
            send(osc=i % OSCS, vel=0)

"""
    Play up to ALLES_OSCS patches at once
"""
def polyphony(max_voices=OSCS,**kwargs):
    note = 0
    oscs = []
    for i in range(int(max_voices/2)):
        oscs.append(int(i))
        oscs.append(int(i+(OSCS/2)))
    print(str(oscs))
    while(1):
        osc = oscs[note % max_voices]
        print("osc %d note %d filter %f " % (osc, 30+note, note*50))
        send(osc=osc, **kwargs, patch=note, filter_type=FILTER_NONE, filter_freq=note*50, note=30+(note), client = -1, vel=1)
        time.sleep(0.5)
        note =(note + 1) % 64

def eq_test():
    reset()
    eqs = [ [0,0,0], [15,0,0], [0,0,15], [0,15,0],[-15,-15,15],[-15,-15,30],[-15,30,-15], [30,-15,-15] ]
    for eq in eqs:
        print("eq_l = %ddB eq_m = %ddB eq_h = %ddB" % (eq[0], eq[1], eq[2]))
        send(eq_l=eq[0], eq_m=eq[1], eq_h=eq[2])
        drums(loops=2)
        time.sleep(1)
        reset()
        time.sleep(0.250)

"""
    Sweep the filter
"""
def sweep(speed=0.100, res=0.5, loops = -1):
    end = 2000
    cur = 0
    while(loops != 0):
        for i in [0, 1, 4, 5, 1, 3, 4, 5]:
            cur = (cur + 100) % end
            send(osc=0,filter_type=FILTER_LPF, filter_freq=cur+250, resonance=res, wave=PULSE, note=50+i, duty=0.50, vel=1)
            send(osc=1,filter_type=FILTER_LPF, filter_freq=cur+500, resonance=res, wave=PULSE, note=50+12+i, duty=0.25, vel=1)
            send(osc=2,filter_type=FILTER_LPF, filter_freq=cur, resonance=res, wave=PULSE, note=50+6+i, duty=0.90, vel=1)
            time.sleep(speed)

"""
    An example drum machine using osc+PCM presets
"""
def drums(bpm=120, loops=-1, **kwargs):
    preset(13, osc=0, **kwargs) # sample bass drum
    preset(8, osc=3, **kwargs) # sample hat
    preset(9, osc=4, **kwargs) # sample cow
    preset(10, osc=5, **kwargs) # sample hi cow
    preset(11, osc=2, **kwargs) # sample snare
    preset(1, osc=7, **kwargs) # filter bass
    [bass, snare, hat, cow, hicow, silent] = [1, 2, 4, 8, 16, 32]
    pattern = [bass+hat, hat+hicow, bass+hat+snare, hat+cow, hat, hat+bass, snare+hat, hat]
    bassline = [50, 0, 0, 0, 50, 52, 51, 0]
    while (loops != 0):
        loops = loops - 1
        for i,x in enumerate(pattern):
            if(x & bass): 
                send(osc=0, vel=4, **kwargs)
            if(x & snare):
                send(osc=2, vel=1.5)
            if(x & hat): 
                send(osc=3, vel=1)
            if(x & cow): 
                send(osc=4, vel=1)
            if(x & hicow): 
                send(osc=5, vel=1)
            if(bassline[i]>0):
                send(osc=7, vel=0.5, note=bassline[i]-12, **kwargs)
            else:
                send(vel=0, osc=7, **kwargs)
            time.sleep(1.0/(bpm*2/60))


"""
    C-major chord
"""
def c_major(octave=2,wave=SINE, **kwargs):
    send(osc=0, freq=220.5*octave, wave=wave, vel=1, **kwargs)
    send(osc=1, freq=138.5*octave, wave=wave, vel=1, **kwargs)
    send(osc=2, freq=164.5*octave, wave=wave, vel=1, **kwargs)




"""
    Connection stuff
"""






def get_sock():
    global sock
    return sock

def get_multicast_group():
    return ('232.10.11.12', UDP_PORT)

def connect(local_ip=None):
    # Set up the socket for multicast send & receive
    global sock

    # If not given, find your source IP -- by default your main routable network interface. 
    if(local_ip is None):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(('10.255.255.255', 1))
            local_ip = s.getsockname()[0]
        except Exception:
            print("Trouble getting routable IP address, using localhost. If wrong, do alles.connect(local_ip='ip.address')")
            local_ip = "127.0.0.1"
        finally:
            s.close()


    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    except AttributeError:
        print("couldn't REUSEPORT")
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    # TTL defines how many hops it can take
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255) # 1
    # Keep loopback on if you're controlling Alles from your own desktop
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1) # 1
    sock.bind(('', UDP_PORT))
    # Set the local interface for multicast receive
    sock.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
    # And the networks to be a member of (destination and host)
    mreq = socket.inet_aton(get_multicast_group()[0]) + socket.inet_aton(local_ip)
    sock.setsockopt(socket.SOL_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    # Don't block to receive -- not necessary and we sometimes drop packets we're waiting for
    sock.setblocking(0)
    print("Connected to %s as local IP for multicast IF" % (local_ip))

def disconnect():
    global sock
    # Remove ourselves from membership
    mreq = socket.inet_aton(get_multicast_group()[0]) + socket.inet_aton(local_ip)
    sock.setsockopt(socket.SOL_IP, socket.IP_DROP_MEMBERSHIP, mreq)
    sock.close()



def decode_battery_mask(mask):
    state = "unknown"
    level = 0
    if (mask & 0x01): state = "charging"
    if (mask & 0x02): state = "charged"
    if (mask & 0x04): state = "discharging"
    if (mask & 0x10): level = 4
    if (mask & 0x20): level = 3 
    if (mask & 0x40): level = 2
    if (mask & 0x80): level = 1
    return(state, level)


def sync(count=10, delay_ms=100):
    global sock
    import re
    # Sends sync packets to all the listeners so they can correct / get the time
    clients = {}
    client_map = {}
    battery_map = {}
    start_time = millis()
    last_sent = 0
    time_sent = {}
    rtt = {}
    i = 0
    while 1:
        tic = millis() - start_time
        if((tic - last_sent) > delay_ms):
            time_sent[i] = millis()
            #print ("sending %d at %d" % (i, time_sent[i]))
            output = "s%di%dZ" % (time_sent[i], i)
            sock.sendto(output.encode('ascii'), get_multicast_group())
            i = i + 1
            last_sent = tic
        try:
            data, address = sock.recvfrom(1024)
            data = data.decode('ascii')
            #print("received %s from %s" % (data, address))
            if(data[0] == '_'):
                data = data[:-1]
                try:
                    [_, client_time, sync_index, client_id, ipv4, battery] = re.split(r'[sicry]',data)
                except ValueError:
                    print("What! %s" % (data))
                if(int(sync_index) <= i): # skip old ones from a previous run
                    #print ("recvd at %d:  %s %s %s %s" % (millis(), client_time, sync_index, client_id, ipv4))
                    # ping sets client index to -1, so make sure this is a sync response 
                    if(int(sync_index) >= 0):
                        client_map[int(ipv4)] = int(client_id)
                        battery_map[int(ipv4)] = battery
                        rtt[int(ipv4)] = rtt.get(int(ipv4), {})
                        rtt[int(ipv4)][int(sync_index)] = millis()-time_sent[int(sync_index)]
        except socket.error:
            pass

        # Wait for at least (client latency) to get any straggling UDP packets back 
        delay_period = 1 + (ALLES_LATENCY_MS / delay_ms)
        if((i-delay_period) > count):
            break
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



def battery_test():
    tic = time.time()
    clients = 1
    try:
        while clients:
            reset()
            print("Been %d seconds" % (time.time()-tic))
            clients = len(sync().keys())
            drums(loops=1)
            time.sleep(2)
            reset()
            time.sleep(60)
    except KeyboardInterrupt:
            pass
    print("Took %d seconds to stop" %(time.time() - tic))


# Setup the sock on module import
# I have some convenience hardcoded IPs for machines I work on here
try:
    if(os.uname().nodename.startswith('colossus')):
        connect(local_ip="192.168.1.2")
    elif(os.uname().nodename.startswith('convolve')):
        connect(local_ip = '192.168.1.3')
    elif(os.uname().nodename.startswith('cedar')):
        connect(local_ip = '192.168.1.3')
    else:
        connect(local_ip=None)
except OSError:
    try:
        connect(local_ip=None)
    except OSError:
        print("Couldn't connect. Try manually with alles.connect('local_ip_address')")

