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
is_local = False


def local_start():
    global is_local, stream
    # Start a local AMY session
    import amy
    import sounddevice as sd
    def callback(indata, outdata, frames, time, status):
        f = frames
        c = 0
        while(f>0):
            for i in amy.render():
                outdata[c] = i/32767.0
                c = c + 1
            f = f - 128
    is_local = True
    stream = sd.Stream(callback=callback)
    stream.start()

def local_stop():
    global stream, is_local
    stream.stop()
    is_local = False

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

# Buffer messages sent to the synths if you call buffer(). 
# Calling buffer(0) turns off the buffering
# flush() sends whatever is in the buffer now, and is called after buffer(0) as well 
send_buffer = ""
buffer_size = 0

def transmit(message, retries=1):
    global is_local
    if(is_local):
        import amy
        amy.send(message)
    for x in range(retries):
        sock.sendto(message.encode('ascii'), multicast_group)

def buffer(size=508):
    global buffer_size
    buffer_size = size
    if(buffer_size == 0):
        flush()

def flush(retries=1):
    global send_buffer
    transmit(send_buffer)
    send_buffer = ""


# Removes trailing 0s and x.0000s 
def trunc(number):
    return ('%.10f' % number).rstrip('0').rstrip('.')

def send(osc=0, wave=-1, patch=-1, note=-1, vel=-1, amp=-1, freq=-1, duty=-1, feedback=-1, timestamp=None, reset=-1, phase=-1, \
        client=-1, retries=1, volume=-1, filter_freq = -1, resonance = -1, envelope=None, adsr_target=-1, lfo_target=-1, \
        debug=-1, lfo_source=-1, eq_l = -1, eq_m = -1, eq_h = -1, filter_type= -1, algorithm=-1, algo_source=None):
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
    if(algorithm>=0): m = m + "o" + trunc(algorithm)
    if(envelope is not None): m = m +"A%s" % (envelope)
    if(algo_source is not None): m = m +"O%s" % (algo_source)
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
            transmit(send_buffer)
            send_buffer = m + '\n'
        else:
            send_buffer = send_buffer + m + '\n'
    else:
        transmit(m+'\n')






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


def cos_lut(table_size, harmonics_weights, harmonics_phases=None):
    import numpy as np
    if harmonics_phases is None:
        harmonics_phases = np.zeros(len(harmonics_weights))
    table = np.zeros(table_size)
    phases = np.arange(table_size) * 2 * np.pi / table_size
    for harmonic_number, harmonic_weight in enumerate(harmonics_weights):
        table += harmonic_weight * np.cos(
            phases * harmonic_number + harmonics_phases[harmonic_number])
    return table




# A LUTset is a list of LUTentries describing downsampled versions of the same
# basic waveform, sorted with the longest (highest-bandwidth) first.
def create_lutset(LUTentry, harmonic_weights, harmonic_phases=None, 
                                    length_factor=8, bandwidth_factor=None):
    import numpy as np
    if bandwidth_factor is None:
        bandwidth_factor = np.sqrt(0.5)
    """Create an ordered list of LUTs with decreasing harmonic content.

    These can then be used in interp_from_lutset to make an adaptive-bandwidth
    interpolation.

    Args:
        harmonic_weights: vector of amplitudes for cosine harmonic components.
        harmonic_phases: initial phases for each harmonic, in radians. Zero 
            (default) indicates cosine phase.
        length_factor: Each table's length is at least this factor times the order
            of the highest harmonic it contains. Thus, this is a lower bound on the
            number of samples per cycle for the highest harmonic. Higher factors make
            the interpolation easier.
        bandwidth_factor: Target ratio between the highest harmonics in successive
            table entries. Default is sqrt(0.5), so after two tables, bandwidth is
            reduced by 1/2 (and length with follow).

    Returns:
        A list of LUTentry objects, sorted in decreasing order of the highest 
        harmonic they contain. Each LUT's length is a power of 2, and as small as
        possible while respecting the length_factor for the highest contained 
        harmonic.
    """
    if harmonic_phases is None:
        harmonic_phases = np.zeros(len(harmonic_weights))
    # Calculate the length of the longest LUT we need. Must be a power of 2, 
    # must have at least length_factor * highest_harmonic samples.
    # Harmonic 0 (dc) doesn't count.
    float_num_harmonics = float(len(harmonic_weights))
    lutsets = []
    done = False
    # harmonic 0 is DC; there's no point in generating that table.
    while float_num_harmonics >= 2:
        num_harmonics = int(round(float_num_harmonics))
        highest_harmonic = num_harmonics - 1    # because zero doesn't count.
        lut_size = int(2 ** np.ceil(np.log(length_factor * highest_harmonic) / np.log(2)))
        lutsets.append(LUTentry(
                table=cos_lut(lut_size, harmonic_weights[:num_harmonics], 
                                harmonic_phases[:num_harmonics]),    # / lut_size,
                highest_harmonic=highest_harmonic))
        float_num_harmonics = bandwidth_factor * float_num_harmonics
    return lutsets


def write_lutset_to_h(filename, variable_base, lutset):
    """Savi out a lutset as a C-compatible header file."""
    num_luts = len(lutset)
    with open(filename, "w") as f:
        f.write("// Automatically-generated LUTset\n")
        f.write("#ifndef LUTSET_{:s}_DEFINED\n".format(variable_base.upper()))
        f.write("#define LUTSET_{:s}_DEFINED\n".format(variable_base.upper()))
        f.write("\n")
        # Define the structure.
        f.write("#ifndef LUTENTRY_DEFINED\n")
        f.write("#define LUTENTRY_DEFINED\n")
        f.write("typedef struct {\n")
        f.write("    const float *table;\n")
        f.write("    int table_size;\n")
        f.write("    int highest_harmonic;\n")
        f.write("} lut_entry;\n")
        f.write("#endif // LUTENTRY_DEFINED\n")
        f.write("\n")
        # Define the content of the individual tables.
        samples_per_row = 8
        for i in range(num_luts):
            table_size = len(lutset[i].table)
            f.write("const float {:s}_lutable_{:d}[{:d}] = {{\n".format(
                variable_base, i, table_size))
            for row_start in range(0, table_size, samples_per_row):
                for sample_index in range(row_start, 
                        min(row_start + samples_per_row, table_size)):
                    f.write("{:f},".format(lutset[i].table[sample_index]))
                f.write("\n")
            f.write("};\n")
            f.write("\n")
        # Define the table of LUTs.
        f.write("lut_entry {:s}_lutset[{:d}] = {{\n".format(
            variable_base, num_luts + 1))
        for i in range(num_luts):
            f.write("    {{{:s}_lutable_{:d}, {:d}, {:d}}},\n".format(
                variable_base, i, len(lutset[i].table), 
                lutset[i].highest_harmonic))
        # Final entry is null to indicate end of table.
        f.write("    {NULL, 0, 0},\n")
        f.write("};\n")
        f.write("\n")
        f.write("#endif // LUTSET_x_DEFINED\n")
    print("wrote", filename)



def make_clipping_lut(filename):
    import numpy as np
    # Soft clipping lookup table scratchpad.
    SAMPLE_MAX = 32767
    LIN_MAX = 29491  #// int(round(0.9 * 32768))
    NONLIN_RANGE = 4915  # // size of nonlinearity lookup table = round(1.5 * (INT16_MAX - LIN_MAX))

    clipping_lookup_table = np.arange(LIN_MAX + NONLIN_RANGE)

    for x in range(NONLIN_RANGE):
        x_dash = float(x) / NONLIN_RANGE
        clipping_lookup_table[x + LIN_MAX] = LIN_MAX + int(np.floor(NONLIN_RANGE * (x_dash - x_dash * x_dash * x_dash / 3.0)))

    with open(filename, "w") as f:
        f.write("// Automatically generated.\n// Clipping lookup table\n")
        f.write("#define LIN_MAX %d\n" % LIN_MAX)
        f.write("#define NONLIN_RANGE %d\n" % NONLIN_RANGE)
        f.write("#define NONLIN_MAX (LIN_MAX + NONLIN_RANGE)\n")
        f.write("const uint16_t clipping_lookup_table[NONLIN_RANGE] = {\n")
        samples_per_row = 8
        for row_start in range(0, NONLIN_RANGE, samples_per_row):
            for sample in range(row_start, min(NONLIN_RANGE, row_start + samples_per_row)):
                f.write("%d," % clipping_lookup_table[LIN_MAX + sample])
            f.write("\n")
        f.write("};\n")
    print("wrote", filename)


def make_luts():
    import numpy as np
    import collections
    # Implement the multiple lookup tables.
    # A LUT is stored as an array of values (table) and the harmonic number of the
    # highest harmonic they contain (i.e., the number of cycles it completes in the
    # entire table, so must be <= len(table)/2.)
    LUTentry = collections.namedtuple('LUTentry', ['table', 'highest_harmonic'])

    # Impulses.
    impulse_lutset = create_lutset(LUTentry, np.ones(128))
    write_lutset_to_h('main/impulse_lutset.h', 'impulse', impulse_lutset)

    # Triangle wave lutset
    n_harms = 64
    coefs = (np.arange(n_harms) % 2) * (
        np.maximum(1, np.arange(n_harms, dtype=float))**(-2))
    triangle_lutset = create_lutset(LUTentry, coefs, np.arange(len(coefs)) * -np.pi / 2)
    write_lutset_to_h('main/triangle_lutset.h', 'triangle', triangle_lutset)

    # Sinusoid "lutset" (only one table)
    sine_lutset = create_lutset(LUTentry, np.array([0, 1]),  harmonic_phases = -np.pi / 2 * np.ones(2), length_factor=256)
    write_lutset_to_h('main/sine_lutset.h', 'sine', sine_lutset)

    # Clipping LUT
    make_clipping_lut('main/clipping_lookup_table.h')







