# fm.py
# Some code to try to convert dx7 patches into AMY commands
# Use the dx7 module if you want to A/B test AMY's FM mode against a dx7 emulation.
# AMY is not a dx7 emulator, so it's not going to be perfect or even close, especially for some of the weirder modes of the dx7
# but fun to play with!
# Get the dx7 module from https://github.com/bwhitman/learnfm
import alles, dx7
import numpy as np
import time

""" Howto

# git clone https://github.com/bwhitman/alles.git
# git clone https://github.com/bwhitman/learnfm.git
# cd learnfm/dx7core
# [edit line 129 of learnfm/dx7core/pydx7.cc to point to the folder you cloned learnfm into for compact.bin]
# make
# python setup.py install

    [ If you get an error and are on an older macOS, try:
    ARCHFLAGS="-arch x86_64" python setup.py install  ]

# cd ../../alles/main

    [ install homebrew if you haven't yet:
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)" ] 

# brew install libsoundio
# make
# ./alles -i 127.0.0.1 [you should hear the alles bleep, if not, change your speaker settings]
# [open a new terminal]
# python
>>> import fm
>>> fm.alles.connect(local_ip='127.0.0.1')
>>> fm.play_patch(234) # any number up to 31000
# You should hear Alles play it, then Raph L play it
# Or, you can grab a patch and modify it
>>> piano = fm.get_patch(29978)
>>> piano[134] = 1 # change DX7 algorithm from 5 to 2 
>>> fm.play_patch(piano) # plays modified patch on both types
"""



def setup_patch(p):
    # Take a FM patch and output AMY commands to set up the patch. Send alles.send(vel=0,osc=6,note=50) after
    
    alles.reset()
    pitch_rates, pitch_times = p["bp_pitch_rates"], p["bp_pitch_times"]
    pitchbp = "%d,%f,%d,%f,%d,%f,%d,%f,%d,%f" % (
        pitch_times[0], pitch_rates[0], pitch_times[1], pitch_rates[1], pitch_times[2], pitch_rates[2], \
        pitch_times[3], pitch_rates[3], pitch_times[4], pitch_rates[4]
    )
    
    # Set up each operator
    
    last_release_time = 0
    last_release_value = 0

    for i,op in enumerate(p["ops"]):
        freq_ratio = -1
        freq = -1
        # Set the ratio or the fixed freq
        if(op.get("fixedhz",None) is not None):
            freq = op["fixedhz"]
        else:
            freq_ratio = op["ratio"]

        bp_rates, bp_times = op["bp_opamp_rates"], op["bp_opamp_times"]
        opbp = "%d,%f,%d,%f,%d,%f,%d,%f,%d,%f" % (
            bp_times[0], bp_rates[0], bp_times[1], bp_rates[1], bp_times[2], bp_rates[2], bp_times[3], bp_rates[3],bp_times[4], bp_rates[4])
        opbpfmt = "%d,%.3f/%d,%.3f/%d,%.3f/%d,%.3f/%d,%.3f" % (
            bp_times[0], bp_rates[0], bp_times[1], bp_rates[1], bp_times[2], bp_rates[2], bp_times[3], bp_rates[3],bp_times[4], bp_rates[4])

        if(bp_times[4] > last_release_time):
            last_release_time = bp_times[4]
            last_release_value = bp_rates[4]

        print("osc %d (op %d) freq %.1f ratio %.3f beta-bp %s amp %.3f amp_mod %d" % \
            (i, (i-6)*-1, freq, freq_ratio, opbpfmt, op["opamp"], p["ampmodsens"]))

        args = {"osc":i, "freq":freq, "ratio": freq_ratio, "bp0_target":alles.TARGET_AMP+alles.TARGET_TRUE_EXPONENTIAL, "bp0":opbp, \
                "amp":op["opamp"],"phase":0.25}  # Make them all in cosine phase, to be like DX7.  Important for slow oscs

        if(op["ampmodsens"] > 0):
            # TODO: we ignore intensity of amp mod sens, just on/off
            args.update({"mod_source":7, "mod_target":alles.TARGET_AMP})

        if(freq>=0):
            args.update({"bp1":pitchbp, "bp1_target":alles.TARGET_FREQ+alles.TARGET_TRUE_EXPONENTIAL})

        alles.send(**args)


    # This is applied to the operators if their amp mod sense > 0 
    amp_lfo_amp = output_level_to_amp(p.get("lfoampmoddepth",0))

    # There's a lot of assumptions made here ! We're  multiplying an 0-7 / 7.0 sens with the 0-99 depth #, then converting to amp??? 
    pitch_lfo_amp = output_level_to_amp( (float(p.get("pitchmodsens", 0)) / 7.0) * p.get("lfopitchmoddepth",0))

    # Set up the amp LFO 
    print("osc 7 amp lfo wave %d freq %f amp %f" % (p["lfowaveform"],p["lfospeed"], amp_lfo_amp))
    alles.send(osc=7, wave=p["lfowaveform"],freq=p["lfospeed"], amp=amp_lfo_amp)

    # and the pitch one
    print("osc 8 pitch lfo wave %d freq %f amp %f" % (p["lfowaveform"],p["lfospeed"], pitch_lfo_amp))
    alles.send(osc=8, wave=p["lfowaveform"],freq=p["lfospeed"], amp=pitch_lfo_amp)

    print("not used: lfo sync %d lfo delay %d " % (p["lfosync"], p["lfodelay"]))

    ampbp = "0,1,%d,%f" % (last_release_time, last_release_value)
    print("osc 6 (main)  algo %d feedback %f pitchenv %s ampenv %s" % ( p["algo"], p["feedback"], pitchbp, ampbp))
    alles.send(osc=6, wave=alles.ALGO, algorithm=p["algo"], feedback=p["feedback"], algo_source="0,1,2,3,4,5", \
        bp0=ampbp, bp0_target=alles.TARGET_AMP+alles.TARGET_TRUE_EXPONENTIAL, \
        bp1=pitchbp, bp1_target=alles.TARGET_FREQ+alles.TARGET_TRUE_EXPONENTIAL, \
        mod_target=alles.TARGET_FREQ, mod_source=8)

def output_level_to_amp(byte):
    """Convert 0-99 log-scale DX7 level into a real value."""
    # Same as eglevel_to_level.
    return 2 ** ((byte - 99) / 8)

def get_patch(patch_number):
    # returns a patch (as in patches.h) from 
    # unpacked.bin generated by dx7db, see https://github.com/bwhitman/learnfm
    f = bytes(open("unpacked.bin", mode="rb").read())
    patch_data = f[patch_number*156:patch_number*156+156]
    #name = ''.join([i if (ord(i) < 128 and ord(i) > 31) else ' ' for i in str(patch_data[145:155])])
    return bytearray(patch_data)

def unpack_patch(bytestream):
    """Simply reformat the bytestream into parameters."""
    bytestream = bytes(bytestream)
    byteno = 0

    def nextbyte(count=1):
        nonlocal byteno
        if count > 1:
            # Return a list.
            return [nextbyte() for _ in range(count)]
        b = bytestream[byteno]
        byteno += 1
        # Return a bare byte.
        return b

    ops = []
    # Starts at op 6
    for i in range(6, 0, -1):
        op = {"opnum": i}
        op["rate"] = nextbyte(4)
        op["level"] = nextbyte(4)
        op["breakpoint"] = nextbyte()
        op["bp_depths"] = nextbyte(2)
        op["bp_curves"] = nextbyte(2)
        op["kbdratescaling"] = nextbyte()
        op["ampmodsens"] = nextbyte()
        op["keyvelsens"] = nextbyte()
        op["opamp"] = nextbyte()
        op["tuning"] = "fixed" if nextbyte() == 1 else "ratio"
        op["coarse"] = nextbyte()
        op["fine"] = nextbyte()
        op["detune"] = nextbyte()
        ops.append(op)
    patch = {"ops": ops}
    patch["pitch_rate"] = nextbyte(4)
    patch["pitch_level"] = nextbyte(4)
    patch["algo"] = 1 + nextbyte()
    patch["feedback"] = nextbyte()
    patch["oscsync"] = nextbyte()
    patch["lfospeed"] = nextbyte()
    patch["lfodelay"] = nextbyte()
    patch["lfopitchmoddepth"] = nextbyte()
    patch["lfoampmoddepth"] = nextbyte()
    patch["lfosync"] = nextbyte()
    patch["lfowaveform"] = nextbyte()
    patch["pitchmodsens"] = nextbyte()
    patch["transpose"] = nextbyte()
    patch["name"] =  ''.join(chr(i) for i in nextbyte(10))
    return patch

def repack_patch(patch):
    """Convert a decoded patch dict back to a bytestream."""
    bytestream = []
    for op in patch["ops"]:
        # Assume ordering is right in ops list.
        bytestream.extend(op["rate"])
        bytestream.extend(op["level"])
        bytestream.append(op["breakpoint"])
        bytestream.extend(op["bp_depths"])
        bytestream.extend(op["bp_curves"])
        bytestream.append(op["kbdratescaling"])
        bytestream.append(op["ampmodsens"])
        bytestream.append(op["keyvelsens"])
        bytestream.append(op["opamp"])
        bytestream.append(1 if op["tuning"] == "fixed" else 0)
        bytestream.append(op["coarse"])
        bytestream.append(op["fine"])
        bytestream.append(op["detune"])
    bytestream.extend(patch["pitch_rate"])
    bytestream.extend(patch["pitch_level"])
    bytestream.append(patch["algo"] - 1)
    bytestream.append(patch["feedback"])
    bytestream.append(patch["oscsync"])
    bytestream.append(patch["lfospeed"])
    bytestream.append(patch["lfodelay"])
    bytestream.append(patch["lfopitchmoddepth"])
    bytestream.append(patch["lfoampmoddepth"])
    bytestream.append(patch["lfosync"])
    bytestream.append(patch["lfowaveform"])
    bytestream.append(patch["pitchmodsens"])
    bytestream.append(patch["transpose"])
    bytestream.extend(ord(c) for c in patch["name"])
    return bytes(bytestream)

def dx7level_to_linear(dx7level):
    """Map the dx7 0..99 levels to linear amplitude."""
    return 2 ** ((dx7level - 99)/8)

def linear_to_dx7level(linear):
    """Map a linear amplitude to the dx7 0..99 scale."""
    return np.log2(np.maximum(dx7level_to_linear(0), linear)) * 8 + 99
    
def calc_loglin_eg_breakpoints(rates, levels):
    """Convert the DX7 rates/levels into (time, target) pairs (for alles)"""
    # This is the part we precompute in fm.py to get breakpoints to send to alles.
    current_level = 0 # it seems, not levels[-1]
    cumulated_time = 0
    breakpoints = [(cumulated_time, current_level)]

    MIN_LEVEL = 34
    ATTACK_RANGE = 75

    def level_to_time(level, t_const):
        """Return the time at which a paradigmatic DX7 attack envelope will reach a level (0..99 range)"""
        # Return the t0 that solves level = MIN_LEVEL + ATTACK_RANGE * (1 - exp(-t0 / t_const))
        return -t_const * np.log((MIN_LEVEL + ATTACK_RANGE - np.maximum(MIN_LEVEL, level))/ATTACK_RANGE)

    for segment, (rate, target_level) in enumerate(zip(rates, levels)):
        if target_level > current_level:   # Attack segment
            # The attack envelopes L(t) appear to be ~ 34 + 75 * (1 - exp(t / t_const)), starting from L = 34
            # i.e. they are rising exponentials (as in analog ADSR, but here in the log(amp) domain) 
            # with an asymptote at 109 (i.e., 10 higher than the highest possible amp).
            # The time constant depends on the R (rate) parameter, and is well fit by:
            t_const = 0.008 * (2 ** ((65 - rate)/6))
            # Total time for this segment is t1 - t0 where t0 and t1 solve
            # effective_start = 34 + 75 * (1 - np.exp(-t0 / t_const)) = 109 - 75 exp(-t0 / t_c)
            # target_level = 34 + 75 * (1 - np.exp(-t1 / t_const)) = 109 - 75 exp(-t1 / t_c)
            # so t1 - t0 = -t_c * [log((34 + 75 - target_level)/75) - log((34 + 75 - effective_start)/75)]
            effective_start_level = np.maximum(current_level, MIN_LEVEL)
            t0 = level_to_time(effective_start_level, t_const)
            segment_duration = level_to_time(target_level, t_const) - t0
            #print("eff_st=", effective_start_level, "t_c=", t_const, "t0=", t0, "dur=", segment_duration)
            # Now alles's task will be to recover t0 and t_const from (time, target) pairs
        else:
            # Decay segment.
            # "A falling segment takes 3.5 mins"
            # so delta = 99 in 210 seconds -> level_change_per_sec =  0.5
            # I think just offset everything by 0.5, avoids div0.          
            level_change_per_sec = -0.5 - 8 * (2 ** ((rate - 24) / 6))
            segment_duration = (target_level - current_level) / level_change_per_sec
            #print("lcps=", level_change_per_sec, "dur=", segment_duration)
        cumulated_time += segment_duration
        breakpoints.append((cumulated_time, dx7level_to_linear(target_level)))
        current_level = target_level
    return breakpoints

# Given a patch byte stream, return a json object that describes it
def decode_patch(p):
    def EGlevel_to_level(eglevel):
        """DX7 EG levels are 8 steps/doubling; 99=1.0."""
        return 2 ** ((eglevel - 99) / 8)

    def EG_seg_time(L0, L1, R):
        """How long will it take to get from L0 to L1 at rate R (all 0..99)?"""
        # L is 8 steps per doubling
        # R is 6 steps per doubling of time, with 24 = 1 sec per doubling of amplitude
        doublings = np.abs((L0 & -2) - (L1 & -2)) / 8  # LSB of levels is ignored
        doublings_per_sec = 2 ** ((R - 24) / 6)
        return doublings / doublings_per_sec

    def eg_to_bp(egrate, eglevel):
        breakpoints = calc_loglin_eg_breakpoints(egrate, eglevel)
        rates = []
        times = []
        for time, level in breakpoints:
            times.append(int(1000 * time))
            rates.append(level)
        return rates, times
    
    def eg_to_bp_orig(egrate, eglevel):
        # http://www.audiocentralmagazine.com/wp-content/uploads/2012/04/dx7-envelope.png
        # or https://yamahasynth.com/images/RefaceSynthBasics/EG_RatesLevels.png
        # rate seems to be "speed", so higher rate == less time
        # level is probably exp, but so is our ADSR? 
        #print ("Input rate %s level %s" %(egrate, eglevel))

        # We're adding a (0,0) at the start - this will become level 4
        times = [0,0,0,0,0]
        rates = [0,0,0,0,0]

        total_ms = 0
        last_L = eglevel[-1]
        for i in range(4):
            # Segment 0 (attack) is a special case, it's 4x faster (24 steps higher).
            ms = 1000 * EG_seg_time(last_L, eglevel[i], egrate[i] + 12*(i==0))
            last_L = eglevel[i]
            l = EGlevel_to_level(eglevel[i])
            if(i!=3):
                total_ms = total_ms + ms
                times[i+1] = total_ms
                rates[i+1] = l
            else:
                # Release ms counter happens separately, so don't add
                times[i+1] = 1000 * EG_seg_time(eglevel[0], eglevel[i], egrate[i])
                # Chop the release at ALLES_MAX_DRIFT 
                if(times[i+1] > alles.ALLES_MAX_DRIFT_MS):
                    times[i+1] = alles.ALLES_MAX_DRIFT_MS
                rates[i+1] = l
        # per dx7 spec, level[0] == level[3]
        rates[0] = rates[4]
        return (rates, times)

    def eg_to_bp_pitch(egrate, eglevel):
        rates, times = eg_to_bp_orig(egrate, eglevel)
        for i in range(len(rates)):
          rates[i] /= 0.014328
        return (rates, times)

    def lfo_speed_to_hz(byte):
        # Measured values from TX802, linear fit by eye
        if byte == 0:
            return 0.064
        if byte <= 64:
            return byte / 6.0
        if byte <= 85:
            return byte - 64.0 * 5.0/6.0
        # Byte > 85
        return 31.67 + (byte - 85.0) * 1.33
    
    def lfo_speed_to_hz_old(byte):
        #   https://web.archive.org/web/20200920050532/https://www.yamahasynth.com/ask-a-question/generating-specific-lfo-frequencies-on-dx
        # but this is weird, he gives 127 values, and we only get in 99
        def linear_expand(count, first, last):
            ret = []
            for i in range(count):
                chop = (last-first) / (count+1)
                ret.append(first+chop*(i+1))
            return ret

        # better one, with 99 numbers, from measuring it on a tx802
        lfo_to_hz = [0]*100
        # fill in some measured landmarks
        lfo_to_hz[0] = 0.064
        lfo_to_hz[1] = 0.18
        lfo_to_hz[2] = 0.321
        lfo_to_hz[3] = 0.452
        lfo_to_hz[4:7] = linear_expand(3, 0.452, 1.15)
        lfo_to_hz[7] =  1.15
        lfo_to_hz[8:15] = linear_expand(7, 1.15, 2.43)
        lfo_to_hz[15] = 2.43
        lfo_to_hz[16:25] = linear_expand(9, 2.43, 4.06)
        lfo_to_hz[25] = 4.06
        lfo_to_hz[26:50] = linear_expand(24, 4.06, 8.13)
        lfo_to_hz[50] = 8.13
        lfo_to_hz[51:60] = linear_expand(9, 8.13, 10.31)
        lfo_to_hz[60] = 10.31
        lfo_to_hz[61:70] = linear_expand(9, 10.31, 16.39)
        lfo_to_hz[70] = 16.39
        lfo_to_hz[71:80] = linear_expand(9, 16.39, 27.00)
        lfo_to_hz[80] = 27.00
        lfo_to_hz[81:85] = linear_expand(4, 27, 33.33)
        lfo_to_hz[85] = 33.33
        lfo_to_hz[86:90] = linear_expand(4, 33.33, 43.48)
        lfo_to_hz[90] = 43.48
        lfo_to_hz[91:99] = linear_expand(8, 43.48, 66)
        lfo_to_hz[99] = 66 # we didn't / couldn't measure this but let's set this as the end
        return lfo_to_hz[byte]


    def lfo_wave(byte):
        if(byte == 0): return alles.TRIANGLE
        if(byte == 1): return alles.TRIANGLE # saw down TODO
        if(byte == 2): return alles.TRIANGLE # up, TODO 
        if(byte == 3): return alles.PULSE 
        if(byte == 4): return alles.SINE
        if(byte == 5): return alles.NOISE
        return None

    def curve(byte):
        # What is this curve for? Pi
        if(byte==0): return "-lin"
        if(byte==1): return "-exp"
        if(byte==2): return "+exp"
        if(byte==3): return "+lin"
        return "unknown"

    def coarse_fine_fixed_hz(coarse, fine, detune=7):
        coarse = coarse & 3
        return 10 ** (coarse + (fine + ((detune - 7) / 8)) / 100 + )
    
    def coarse_fine_ratio(coarse, fine, detune=7):
        coarse = coarse & 31
        if(coarse == 0):
            coarse = 0.5
        return coarse * (1 + (fine + ((detune - 7) / 8)) / 100)

    patch = {}
    patchstruct = unpack_patch(bytes(p))
    ops = []
    # Starts at op 6
    c = 0
    for i in range(6):
        opstruct = patchstruct["ops"][i]
        op = {}
        # TODO, this should be computed after scaling 
        (op["bp_opamp_rates"], op["bp_opamp_times"]) = (
            eg_to_bp(opstruct["rate"], opstruct["level"]))
        op["breakpoint"] = opstruct["breakpoint"]
        # left depth, right depth -- this + the curve scales the op rates left and right of note # specified in breakpoint
        op["bp_depths"] = opstruct["bp_depths"]
        # curve type (l , r)
        op["bp_curves"] = opstruct["bp_curves"]
        op["kbdratescaling"] = opstruct["kbdratescaling"]
        op["ampmodsens"] = opstruct["ampmodsens"]
        op["keyvelsens"] = opstruct["keyvelsens"]
        # DX7 operators output -2..2 (i.e., as phase modulators they can shift +/- 2 cycles)
        op["opamp"] = 2 * output_level_to_amp(opstruct["opamp"])
        if(opstruct["tuning"] == "fixed"):
            op["fixedhz"] = coarse_fine_fixed_hz(opstruct["coarse"], opstruct["fine"], opstruct["detune"])
        else:
            op["ratio"] = coarse_fine_ratio(opstruct["coarse"], opstruct["fine"], opstruct["detune"])
        ops.append(op)
    patch["ops"] = ops

    (patch["bp_pitch_rates"], patch["bp_pitch_times"]) = (
        eg_to_bp_pitch(patchstruct["pitch_rate"], patchstruct["pitch_level"]))
    patch["algo"] = patchstruct["algo"] 
    # Empirically matched by ear.
    patch["feedback"] = 0.00125 * (2 ** patchstruct["feedback"])
    patch["oscsync"] = patchstruct["oscsync"]
    patch["lfospeed"] = lfo_speed_to_hz(patchstruct["lfospeed"])
    patch["lfodelay"] = patchstruct["lfodelay"]
    patch["lfopitchmoddepth"] = patchstruct["lfopitchmoddepth"]
    patch["lfoampmoddepth"] = patchstruct["lfoampmoddepth"]
    patch["lfosync"] = patchstruct["lfosync"]
    patch["lfowaveform"] = lfo_wave(patchstruct["lfowaveform"])
    patch["pitchmodsens"] = patchstruct["pitchmodsens"]
    patch["transpose"] = patchstruct["transpose"]
    patch["name"] = patchstruct["name"]
    return patch


##### AB testing / debug stuff here

# Send a message to brian's tx802 and hear it from the live stream
# do it like
# import fm
# fm._token = "thing brian gives you"
# fm.tx802_patch(fm.get_patch(43)) # or whatever patch data you create
# fm.tx802_note_on(50)
# fm.tx802_note_off(50)
# if it gets stuck, just send tx802_patch again, it'll stop the synth

_token = ""
def send_to_tx802(message):
    import uuid, urllib, json
    room_id="!ETtPRVnMRSWWCmsHQJ:duraflame.rosaline.org"
    event_type="m.room.message"
    data={"msgtype":"m.text","body":message}
    url="https://duraflame.rosaline.org/_matrix/client/v3/rooms/%s/send/%s/%s" % (room_id, event_type, str(uuid.uuid4()))
    r=urllib.request.Request(url, data=bytes(json.dumps(data).encode('utf-8')), method='PUT')
    r.add_header('Authorization',"Bearer %s" %(_token))
    urllib.request.urlopen(r)

def tx802_note_on(note_number):
    import base64
    send_to_tx802("noteon " + str(note_number) + " " + base64.b64encode(b"nothing").decode('ascii'))

def tx802_note_off(note_number):
    import base64
    send_to_tx802("noteoff " + str(note_number) + " " + base64.b64encode(b"nothing").decode('ascii'))

def tx802_patch(patch_data):
    import base64
    # fm.get_patch() now returns 156 bytes, not 155, because of the live operator on/off byte i guess... strip it
    if(len(patch_data)==156):
        patch_data = patch_data[:-1]
    send_to_tx802("patch " + str(0) + " " + base64.b64encode(patch_data).decode('ascii'))


# Play a numpy array on a mac without having to use an external library
def play_np_array(np_array, samplerate=alles.SAMPLE_RATE):
    import wave, tempfile , os, struct
    tf = tempfile.NamedTemporaryFile()
    obj = wave.open(tf,'wb')
    obj.setnchannels(1) # mono
    obj.setsampwidth(2)
    obj.setframerate(samplerate)
    for i in range(np_array.shape[0]):
        value = int(np_array[i] * 32767.0)
        data = struct.pack('<h', value)
        obj.writeframesraw( data )
    obj.close()
    os.system("afplay " + tf.name)
    tf.close()


# Use learnfm's dx7 to render a dx7 note from MSFA
def dx7_render(patch, midinote, velocity, samples, keyup_sample):
    if(type(patch)==int):
        s = dx7.render(patch, midinote, velocity, samples, keyup_sample)
    else:
        s = dx7.render_patchdata(patch, midinote, velocity, samples, keyup_sample)
    return np.array(s)/32767.0


# Play our version vs the MSFA version to A/B test
def play_patch(patch, midinote=50, length_s = 4, keyup_s = 2):
    # You can pass in a patch # (0-31000 or so) or a 156 byte patch, which you can modify
    if(type(patch)==int):
        dx7_patch = bytes(dx7.unpack(patch))
    else:
        dx7_patch = bytes(patch)

    p = decode_patch(dx7_patch)
    print(str(p["name"]))

    print("AMY:")
    setup_patch(p)

    alles.send(osc=6,vel=4,note=midinote,timestamp=alles.millis())
    alles.send(osc=6,vel=0,timestamp=alles.millis() + (length_s-keyup_s)*1000)
    # Catch up to latency
    time.sleep(length_s + alles.ALLES_LATENCY_MS/1000)
    alles.reset()
    time.sleep(0.5)
    # Render Raph
    print("MSFA:")
    them_samples = dx7_render(dx7_patch, midinote, 90, int(length_s*alles.SAMPLE_RATE),int(keyup_s*alles.SAMPLE_RATE))
    play_np_array(them_samples)



#### Header file stuff below


def generate_fm_header(patches, **kwargs):
    # given a list of patch numbers, output a fm.h
    out = open("main/amy/fm.h", "w")
    out.write("// Automatically generated by fm.generate_fm_header()\n#ifndef __FM_H\n#define __FM_H\n#define ALGO_PATCHES %d\n" % (len(patches)))
    all_patches = []
    ids = []
    for patch in patches:
        ids.append(patch)
        p = header_patch(decode_patch(get_patch(patch)))
        all_patches.append(p)

    out.write("const algorithms_parameters_t fm_patches[ALGO_PATCHES] = {\n")
    for idx,p in enumerate(all_patches):
        out.write("\t{ %d, %f, {%f, %f, %f, %f}, {%d, %d, %d, %d}, %f, %d, %f, %d, {\n" % 
            (p[1], p[2], p[3][0], p[3][1], p[3][2], p[3][3], p[4][0], p[4][1], p[4][2], p[4][3], p[5], p[6], p[7], p[8]))
        for i in range(6):
            out.write("\t\t\t{%f, %f, %f, {%f, %f, %f, %f}, {%d, %d, %d, %d}, %f}, /* op %d */\n" % 
                (p[9][i][0], p[9][i][1], p[9][i][2], p[9][i][3][0], p[9][i][3][1], p[9][i][3][2], p[9][i][3][3], 
                    p[9][i][4][0], p[9][i][4][1], p[9][i][4][2], p[9][i][4][3], p[9][i][5], (i-6)*-1 ))
        out.write("\t\t},\n\t}, /* %s (%d) */ \n" % (p[0], ids[idx]))
    out.write("};\n#endif // __FM_H\n")
    out.close()

# spit out all the params of a patch for a header file
def header_patch(p):
    os  = []
    for i,op in enumerate(p["ops"]):
        freq_ratio = -1
        freq = -1
        if(op.get("fixedhz",None) is not None):
            freq = op["fixedhz"]
        else:
            freq_ratio = op["ratio"]
        o_data = [freq, freq_ratio, op["opamp"], op["bp_opamp_rates"], op["bp_opamp_times"],op["detunehz"]]
        os.append(o_data)
    lfo_target , lfo_freq, lfo_wave, lfo_amp, = (-1, -1, -1, -1)
    # Choose the bigger one
    if(p.get("lfoampmoddepth",0) + p.get("lfopitchmoddepth",0) > 0):
        lfo_freq = p["lfospeed"]
        lfo_wave = p["lfowaveform"]
        if(p.get("lfoampmoddepth",0) >= p.get("lfopitchmoddepth",0)):
            lfo_target=alles.TARGET_AMP
            lfo_amp = output_level_to_amp(p.get("lfoampmoddepth",0))
        else:
            lfo_target=alles.TARGET_FREQ
            lfo_amp = output_level_to_amp(p.get("lfopitchmoddepth",0))
    return (p["name"], p["algo"], p["feedback"], p["bp_pitch_rates"], p["bp_pitch_times"], lfo_freq, lfo_wave, lfo_amp, lfo_target, os)



