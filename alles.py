import amy
import socket, struct, datetime, os, time

sock = 0
ALLES_LATENCY_MS = 1000


"""
    A bunch of useful presets
"""
def preset(which,osc=0, **kwargs):
    # Reset the osc first
    reset(osc=osc)
    if(which==0): # simple note
        send(osc=osc, wave=amy.SINE, bp0="10,1,250,0.7,500,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==1): # filter bass
        send(osc=osc, filter_freq=2500, resonance=5, wave=amy.SAW, filter_type=amy.FILTER_LPF, bp0="100,0.5,25,0", bp0_target=amy.TARGET_AMP+amy.TARGET_FILTER_FREQ, **kwargs)
    if(which==2): # long sine pad to test ADSR
        send(osc=osc, wave=amy.SINE, bp0="0,0,500,1,1000,0.25,750,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==3): # amp LFO example
        reset(osc=osc+1)
        send(osc=osc+1, wave=amy.SINE, vel=0.50, freq=1.5, **kwargs)
        send(osc=osc, wave=amy.PULSE, envelope="150,1,250,0.25,250,0", bp0_target=amy.TARGET_AMP, lfo_target=amy.TARGET_AMP, lfo_source=osc+1, **kwargs)
    if(which==4): # pitch LFO going up 
        reset(osc=osc+1)
        send(osc=osc+1, wave=amy.SINE, vel=0.50, freq=0.25, **kwargs)
        send(osc=osc, wave=amy.PULSE, bp0="150,1,400,0,0,0", bp0_target=amy.TARGET_AMP, lfo_target=amy.TARGET_FREQ, lfo_source=osc+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        reset(osc=osc+1)
        send(osc=osc+1, wave=amy.SINE, vel=0.50, freq=0.25, phase=0.5, **kwargs)
        send(osc=osc, wave=amy.SINE, vel=0, bp0="500,0,0,0", bp0_target=amy.TARGET_AMP, lfo_target=amy.TARGET_FREQ, lfo_source=osc+1, **kwargs)
    if(which==6): # noise snare
        send(osc=osc, wave=amy.NOISE, vel=0, bp0="250,0,0,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==7): # closed hat
        send(osc=osc, wave=amy.NOISE, vel=0, envelope="25,1,75,0,0,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==8): # closed hat from PCM 
        send(osc=osc, wave=amy.PCM, vel=0, patch=17, freq=22050, **kwargs)
    if(which==9): # cowbell from PCM
        send(osc=osc, wave=amy.PCM, vel=0, patch=25, freq=22050, **kwargs)
    if(which==10): # high cowbell from PCM
        send(osc=osc, wave=amy.PCM, vel=0, patch=25, freq=31000, **kwargs)
    if(which==11): # snare from PCM
        send(osc=osc, wave=amy.PCM, vel=0, patch=5, freq=22050, **kwargs)
    if(which==12): # FM bass 
        send(osc=osc, wave=amy.ALGO, vel=0, patch=15, **kwargs)
    if(which==13): # Pcm bass drum
        send(osc=osc, wave=amy.PCM, vel=0, patch=20, freq=22050, **kwargs)

# Buffer messages sent to the synths if you call buffer(). 
# Calling buffer(0) turns off the buffering
# flush() sends whatever is in the buffer now, and is called after buffer(0) as well 
send_buffer = ""
buffer_size = 0

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
    message = amy.message(**kwargs)
    if(buffer_size > 0):
        if(len(send_buffer + message) > buffer_size):
            transmit(send_buffer, retries=retries)
            send_buffer = message
        else:
            send_buffer = send_buffer + message
    else:
        transmit(message,retries=retries)

"""
    Convenience functions
"""

def reset(osc=None):
    if(osc is not None):
        send(reset=osc)
    else:
        send(reset=100) # reset > ALLES_OSCS resets all oscs

def volume(volume, client = -1):
    send(0, client=client, volume=volume)


"""
    Run a scale through all the synth's sounds
"""
def test():
    while True:
        for wave in [amy.SINE, amy.SAW, amy.PULSE, amy.TRIANGLE, amy.NOISE]:
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
            send(osc=i % amy.OSCS, note=i+50, wave=amy.ALGO, patch=patch, vel=1, **kwargs)
            time.sleep(wait)
            send(osc=i % amy.OSCS, vel=0)

"""
    Play up to ALLES_OSCS patches at once
"""
def polyphony(max_voices=amy.OSCS,**kwargs):
    note = 0
    oscs = []
    for i in range(int(max_voices/2)):
        oscs.append(int(i))
        oscs.append(int(i+(amy.OSCS/2)))
    print(str(oscs))
    while(1):
        osc = oscs[note % max_voices]
        print("osc %d note %d filter %f " % (osc, 30+note, note*50))
        send(osc=osc, **kwargs, patch=note, filter_type=amy.FILTER_NONE, filter_freq=note*50, note=30+(note), client = -1, vel=1)
        time.sleep(0.5)
        note =(note + 1) % 64

def eq_test():
    eset()
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
            send(osc=0,filter_type=amy.FILTER_LPF, filter_freq=cur+250, resonance=res, wave=amy.PULSE, note=50+i, duty=0.50, vel=1)
            send(osc=1,filter_type=amy.FILTER_LPF, filter_freq=cur+500, resonance=res, wave=amy.PULSE, note=50+12+i, duty=0.25, vel=1)
            send(osc=2,filter_type=amy.FILTER_LPF, filter_freq=cur, resonance=res, wave=amy.PULSE, note=50+6+i, duty=0.90, vel=1)
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
def c_major(octave=2,wave=amy.SINE, **kwargs):
    send(osc=0, freq=220.5*octave, wave=wave, vel=1, **kwargs)
    send(osc=1, freq=138.5*octave, wave=wave, vel=1, **kwargs)
    send(osc=2, freq=164.5*octave, wave=wave, vel=1, **kwargs)




"""
    Connection stuff
"""


# This is your source IP -- by default your main routable network interface. 
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    s.connect(('10.255.255.255', 1))
    local_ip = s.getsockname()[0]
except Exception:
    print("Trouble getting routable IP address")
    local_ip = ""
finally:
    s.close()

# But override this if you are using multiple network interfaces, for example a dedicated router to control the synths
# This, for example, is my dev machine's 2nd network interface IP, that I have wired to a separate wifi router
if(os.uname().nodename=='colossus'):
    local_ip = '192.168.1.2'
elif(os.uname().nodename=='cedar.local'):
    local_ip = '192.168.1.3'




def get_sock():
    global sock
    return sock

def get_multicast_group():
    return ('232.10.11.12', 3333)

def connect():
    # Set up the socket for multicast send & receive
    global sock
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        print("couldn't REUSEPORT")
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    # TTL defines how many hops it can take
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 20) # 1
    # Keep loopback on if you're controlling Alles from your own desktop
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1) # 1
    sock.bind(('', 3333))
    # Set the local interface for multicast receive
    sock.setsockopt(socket.SOL_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
    # And the networks to be a member of (destination and host)
    mreq = socket.inet_aton(get_multicast_group()[0]) + socket.inet_aton(local_ip)
    sock.setsockopt(socket.SOL_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    # Don't block to receive -- not necessary and we sometimes drop packets we're waiting for
    sock.setblocking(0)

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
    start_time = amy.millis()
    last_sent = 0
    time_sent = {}
    rtt = {}
    i = 0
    while 1:
        tic = amy.millis() - start_time
        if((tic - last_sent) > delay_ms):
            time_sent[i] = amy.millis()
            #print ("sending %d at %d" % (i, time_sent[i]))
            output = "s%di%dZ" % (time_sent[i], i)
            sock.sendto(output.encode('ascii'), get_multicast_group())
            i = i + 1
            last_sent = tic
        try:
            data, address = sock.recvfrom(1024)
            data = data.decode('ascii')
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
                        rtt[int(ipv4)][int(sync_index)] = amy.millis()-time_sent[int(sync_index)]
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
connect()


