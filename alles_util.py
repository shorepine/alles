# alles_util.py
import socket, time, struct, datetime, sys, re, os

# Setup stuff -- this is the multicast IP & port all the synths listen on
multicast_group = ('232.10.11.12', 3333)

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
    local_ip = '192.168.1.2'


sock = 0
ALLES_LATENCY_MS = 1000


def connect():
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

def disconnect():
    global sock
    # Remove ourselves from membership
    mreq = socket.inet_aton(multicast_group[0]) + socket.inet_aton(local_ip)
    sock.setsockopt(socket.SOL_IP, socket.IP_DROP_MEMBERSHIP, mreq)
    sock.close()

def millis():
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
            print("Been %d seconds" % (time.time()-tic))
            clients = len(sync().keys())
            complex(loops=1)
            time.sleep(1)
            off()
            time.sleep(60)
    except KeyboardInterrupt:
            pass
    print("Took %d seconds to stop" %(time.time() - tic))


def reset(osc=None):
    if(osc is not None):
        send(reset=osc)
    else:
        send(reset=100) # reset > ALLES_OSCS resets all oscs

def volume(volume, client = -1):
    send(0, client=client, volume=volume)



def note_on(vel=1, **kwargs):
    send(vel=vel, **kwargs)

def note_off(**kwargs):
    send(vel=0, **kwargs)


send_buffer = ""
buffer_size = 0


def buffer(size=508):
    global buffer_size
    buffer_size = size
    if(buffer_size == 0):
        flush()

def flush(retries=1):
    global send_buffer
    for x in range(retries):
        sock.sendto(send_buffer.encode('ascii'), multicast_group)
    send_buffer = ""


# Removes trailing 0s and x.0000s 
def trunc(number):
    return ('%.10f' % number).rstrip('0').rstrip('.')

def send(osc=0, wave=-1, patch=-1, note=-1, vel=-1, amp=-1, freq=-1, duty=-1, feedback=-1, timestamp=None, reset=-1, phase=-1, \
        client=-1, retries=1, volume=-1, filter_freq = -1, resonance = -1, envelope=None, adsr_target=-1, lfo_target=-1, \
        debug=-1, lfo_source=-1, eq_l = -1, eq_m = -1, eq_h = -1, filter_type= -1):
    global sock, send_buffer, buffer_size
    if(timestamp is None): timestamp = millis()
    m = "t" + trunc(timestamp)
    if(osc>=0): m = m + "v" + trunc(osc)
    if(wave>=0): m = m + "w" + trunc(wave)
    if(duty>=0): m = m + "d" + trunc(duty)
    if(feedback>=0): m = m + "b" + trunc(feedback)
    if(freq>=0): m = m + "f" + trunc(freq)
    if(note>=0): m = m + "n" + trunc(note)
    if(patch>=0): m = m + "p" + trunc(patch)
    if(phase>=0): m = m + "P" + trunc(phase)
    if(client>=0): m = m + "c" + trunc(client)
    if(amp>=0): m = m + "a" + trunc(amp)
    if(vel>=0): m = m + "l" + trunc(vel)
    if(volume>=0): m = m + "V" + trunc(volume)
    if(resonance>=0): m = m + "R" + trunc(resonance)
    if(filter_freq>=0): m = m + "F" + trunc(filter_freq)
    if(envelope is not None): m = m +"A%s" % (envelope)
    if(adsr_target>=0): m = m + "T" +trunc(adsr_target)
    if(lfo_target>=0): m = m + "g" + trunc(lfo_target)
    if(lfo_source>=0): m = m + "L" + trunc(lfo_source)
    if(reset>=0): m = m + "S" + trunc(reset)
    if(debug>=0): m = m + "D" + trunc(debug)
    if(eq_l>=0): m = m + "x" + trunc(eq_l)
    if(eq_m>=0): m = m + "y" + trunc(eq_m)
    if(eq_h>=0): m = m + "z" + trunc(eq_h)
    if(filter_type>=0): m = m + "G" + trunc(filter_type)

    if(buffer_size > 0):
        if(len(send_buffer + m + '\n') > buffer_size):
            #print("buffer %d hit, sending %s" % (buffer_size, send_buffer))
            for x in range(retries):
                sock.sendto(send_buffer.encode('ascii'), multicast_group)
            send_buffer = m + '\n'
        else:
            send_buffer = send_buffer + m + '\n'
            #print("didn't send, send buffer now %s" % (send_buffer))
    else:
        send_buffer = m + '\n'
        for x in range(retries):
            sock.sendto(send_buffer.encode('ascii'), multicast_group)






def generate_patches_header(how_many = 1000):
    # Generate a list of baked-in DX7 patches from our database of 31,000 patches
    # You're limited to the flash size on your board, which on mine are 2MB
    # You can pick and choose the ones you want, i'm just choosing the first N
    p = open("main/patches.h", "w")
    p.write("// Automatically generated by alles.generate_patches_header()\n")
    p.write("#ifndef __PATCHES_H\n#define __PATCHES_H\n#define PATCHES %d\n\n" % (how_many))
    p.write("const char patches[%d] = {\n" % (how_many * 156))

    # unpacked.bin generated by dx7db, see https://github.com/bwhitman/learnfm
    f = bytes(open("unpacked.bin", mode="rb").read())
    if(how_many > len(f)/156): how_many = len(f)/156
    for patch in range(how_many):
        patch_data = f[patch*156:patch*156+156]
        # Convert the name to something printable
        name = ''.join([i if (ord(i) < 128 and ord(i) > 31) else ' ' for i in str(patch_data[145:155])])
        p.write("    /* [%03d] %s */ " % (patch, name))
        for x in patch_data:        
            p.write("%d," % (x))
        p.write("\n")
    p.write("};\n\n#endif  // __PATCHES_H\n")
    p.close()


def generate_pcm_header(sf2_filename, pcm_sample_rate = 22050):
    # Given an sf2 file, extract some pcm and write pcm.h
    from sf2utils.sf2parse import Sf2File
    import resampy
    import numpy as np
    p = open("main/pcm.h", "w")
    p.write("// Automatically generated by alles.generate_pcm_header()\n")
    p.write("#ifndef __PCM_H\n#define __PCM_H\n")
    offsets = []
    offset = 0
    int16s = []
    sf2 = Sf2File(open(sf2_filename, 'rb'))
    for sample in sf2.samples:
        try:
            if(sample.is_mono):
                s = {}
                s["name"] = sample.name
                floaty =(np.frombuffer(bytes(sample.raw_sample_data),dtype='int16'))/32768.0
                resampled = resampy.resample(floaty, sample.sample_rate, pcm_sample_rate)
                samples = np.int16(resampled*32768)
                int16s.append(samples)
                s["offset"] = offset 
                s["length"] = samples.shape[0]
                offset = offset + samples.shape[0]
                offsets.append(s)
        except AttributeError:
            pass
    all_samples = np.hstack(int16s)
    p.write("#define PCM_SAMPLES %d\n#define PCM_LENGTH %d\n#define PCM_SAMPLE_RATE %d\n" % (len(offsets), all_samples.shape[0]), pcm_sample_rate)
    p.write("const uint32_t offset_map[%d] = {\n" % (len(offsets)*2))
    for o in offsets:
        p.write("    %d, %d, /* %s */\n" %(o["offset"], o["length"], o["name"]))
    p.write("};\n")

    p.write("const int16_t pcm[%d] = {\n" % (all_samples.shape[0]))
    column = 15
    count = 0
    for i in range(int(all_samples.shape[0]/column)):
        p.write("    %s,\n" % (",".join([str(d).ljust(6) for d in all_samples[i*column:(i+1)*column]])))
        count = count + column
    print("count %d all_samples.shape %d" % (count, all_samples.shape[0]))
    if(count != all_samples.shape[0]):
        p.write("    %s\n" % (",".join([str(d).ljust(6) for d in all_samples[count:]])))
    p.write("};\n\n#endif  // __PCM_H\n")


# N equal-amplitude cosines make a band-limited impulse.
def cosines(num_cosines, args):
    import numpy as np
    num_points = len(args)
    vals = np.zeros(num_points)
    for i in range(num_cosines):
        vals += np.cos((i + 1) * args)
    return vals / float(num_cosines)

def make_lut(basename, function, tab_size=1024):
    import numpy as np
    filename = "main/{:s}_{:d}.h".format(basename, tab_size) 
    cpp_base = basename.upper()
    cpp_flag = "__" + cpp_base + "_H"
    size_sym = cpp_base + "_SIZE"
    mask_sym = cpp_base + "_MASK"

    sins = function(np.arange(tab_size) / tab_size * 2 * np.pi)
    row_len = 8
    with open(filename, "w") as f:
        f.write("// {:s} - lookup table\n".format(filename))
        f.write("// tab_size = {:d}\n".format(tab_size))
        f.write("// function = {:s}\n".format(function.__name__))
        f.write("\n")
        f.write("#ifndef {:s}\n".format(cpp_flag))
        f.write("#define {:s}\n".format(cpp_flag))
        f.write("#define {:s} {:d}\n".format(size_sym, tab_size))
        f.write("#define {:s} 0x{:x}\n".format(mask_sym, tab_size - 1))
        f.write("const int16_t {:s}[{:s}] = {{\n".format(basename, size_sym))
        for base in np.arange(0, tab_size, row_len):
            for offset in np.arange(row_len):
                val = int(round(32767 * sins[base + offset]))
                _ = f.write("{:d},".format(val))
            _ = f.write("\n")
        f.write("};\n")
        f.write("\n")
        f.write("#endif\n")

    print("wrote", filename)

def make_luts(tab_size=1024, num_harmonics=32):
    import numpy as np
    from functools import partial
    basename = "impulse{:d}".format(num_harmonics)
    function = partial(cosines, num_harmonics)
    function.__name__ = basename
    make_lut(basename, function, tab_size=tab_size)
    make_lut("sinLUT", np.sin, tab_size=tab_size)






