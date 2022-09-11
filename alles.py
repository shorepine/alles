import socket, struct, datetime, os, time, sys
sys.path.append('amy')
from amy import *

ALLES_LATENCY_MS = 1000
ALLES_MAX_DRIFT_MS = 20000
UDP_PORT = 9294
sock = 0


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

