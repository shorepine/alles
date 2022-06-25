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
# You should hear Alles play it, then Ralph L play it
# Or, you can grab a patch and modify it
>>> piano = fm.get_patch(29978)
>>> piano[134] = 1 # change DX7 algorithm from 5 to 2 
>>> fm.play_patch(piano) # plays modified patch on both types
"""



def setup_patch(p):
    # Take a FM patch and output AMY commands to set up the patch. Send alles.send(vel=0,osc=6,note=50) after
    # Problem here, pitch values are such that 0 = -n octave, 99 = + n octave 
    # pitch level = 50 means no change (or 1 for us)
    # can our breakpoints handle negative numbers? 
    alles.reset()
    pitch_rates, pitch_times = p["bp_pitch_rates"], p["bp_pitch_times"]
    pitchbp = "%d,%f,%d,%f,%d,%f,%d,%f,%d,%f" % (
        pitch_times[0], pitch_rates[0], pitch_times[1], pitch_rates[1], pitch_times[2], pitch_rates[2], pitch_times[3], pitch_rates[3], pitch_times[4], pitch_rates[4]
    )
    # Set up each operator
    last_release_time = 0
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
            bp_times[0], bp_rates[0], bp_times[1], bp_rates[1], bp_times[2], bp_rates[2], bp_times[3], bp_rates[3],bp_times[4], bp_rates[4]
        )
        if(bp_times[4] > last_release_time):
            last_release_time = bp_times[4]

        print("osc %d (op %d) freq %f ratio %f beta-bp %s pitch-bp %s beta %f detune %d" % (i, (i-6)*-1, freq, freq_ratio, opbp, pitchbp, op["opamp"], op["detunehz"]))
        if(freq>=0):
            alles.send(osc=i, freq=freq, ratio=freq_ratio,bp0_target=alles.TARGET_AMP+alles.TARGET_LINEAR,bp0=opbp, bp1=pitchbp, bp1_target=alles.TARGET_FREQ+alles.TARGET_LINEAR, amp=op["opamp"], detune=op["detunehz"])
        else:
            alles.send(osc=i, freq=freq, ratio=freq_ratio,bp0_target=alles.TARGET_AMP+alles.TARGET_LINEAR,bp0=opbp, amp=op["opamp"], detune=op["detunehz"])

    # Set up the main carrier note
    lfo_target = 0
    # Choose the bigger one
    if(p.get("lfoampmoddepth",0) + p.get("lfopitchmoddepth",0) > 0):
        if(p.get("lfoampmoddepth",0) >= p.get("lfopitchmoddepth",0)):
            lfo_target=alles.TARGET_AMP
            lfo_amp = output_level_to_amp(p.get("lfoampmoddepth",0))
        else:
            lfo_target=alles.TARGET_FREQ
            lfo_amp = output_level_to_amp(p.get("lfopitchmoddepth",0))

    if(lfo_target>0):
        alles.send(osc=7, wave=p["lfowaveform"],freq=p["lfospeed"], amp=lfo_amp)
        alles.send(osc=6,mod_target=lfo_target, mod_source=7)
        print("osc 7 lfo wave %d freq %f amp %f target %d" % (p["lfowaveform"],p["lfospeed"], lfo_amp, lfo_target))
    ampbp = "0,1,%d,1" % (last_release_time)
    print("osc 6 (main)  algo %d feedback %f pitchenv %s ampenv %s" % ( p["algo"], p["feedback"], pitchbp, ampbp))
    print("transpose is %d" % (p["transpose"]))
    alles.send(osc=6, wave=alles.ALGO, algorithm=p["algo"], feedback=p["feedback"], algo_source="0,1,2,3,4,5", \
        bp0=ampbp, bp0_target=alles.TARGET_AMP+alles.TARGET_LINEAR, \
        bp1=pitchbp, bp1_target=alles.TARGET_FREQ+alles.TARGET_LINEAR)



def output_level_to_amp(byte):
    # Sure could be a exp curve but seems a bit custom
    # https://i.stack.imgur.com/1FQqR.jpg
    """
    From Dan:
        When doing phase modulation in LUTs, there’s the factor of lut_size (the difference between 
        phase and scaled_phase).  So 0.2 in the “phase” domain becomes 51.2 if we scale it up for a 256 pt LUT
    """
    if(byte<20): return 0
    if(byte<40): return 0.1/14
    if(byte<50): return 0.25/14
    if(byte<60): return 0.5/14
    if(byte<70): return 1.2/14
    if(byte<80): return 2.75/14
    if(byte<85): return 4./14
    if(byte<90): return 6./14
    if(byte<88): return 6.05/14
    if(byte<89): return 6.1/14
    if(byte<90): return 6.2/14
    if(byte<91): return 6.5/14
    if(byte<92): return 7./14
    if(byte<93): return 8./14
    if(byte<94): return 9./14
    if(byte<95): return 9.5/14
    if(byte<96): return 10./14
    if(byte<97): return 11./14
    if(byte<98): return 12.5/14
    if(byte<99): return 13./14
    return 1.0

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
            ms = 1000 * EG_seg_time(last_L, eglevel[i], egrate[i])
            last_L = eglevel[i]
            l = EGlevel_to_level(eglevel[i])
            if(i!=3):
                total_ms = total_ms + ms
                times[i+1] = total_ms
                rates[i+1] = l
            else:
                # Release ms counter happens separately, so don't add
                times[i+1] = 1000 * EG_seg_time(eglevel[0], eglevel[i], egrate[i])
                rates[i+1] = l
        # per dx7 spec, level[0] == level[3]
        rates[0] = rates[4]
        return (rates, times)

    def eg_to_bp_pitch(egrate, eglevel):
        rates, times = eg_to_bp(egrate, eglevel)
        for i in range(len(rates)):
          rates[i] /= 0.014328
        return (rates, times)

    def lfo_speed_to_hz(byte):
        #   https://web.archive.org/web/20200920050532/https://www.yamahasynth.com/ask-a-question/generating-specific-lfo-frequencies-on-dx
        # but this is weird, he gives 127 values, and we only get in 99
        return [0.026, 0.042, 0.084, 0.126, 0.168, 0.210, 0.252, 0.294, 0.336, 0.372, 0.412, 0.456, 0.505, 0.542,
         0.583, 0.626, 0.673, 0.711, 0.752, 0.795, 0.841, 0.880, 0.921, 0.964, 1.009, 1.049, 1.090, 1.133,
         1.178, 1.218, 1.259, 1.301, 1.345, 1.386, 1.427, 1.470, 1.514, 1.554, 1.596, 1.638, 1.681, 1.722,
         1.764, 1.807, 1.851, 1.932, 1.975, 2.018, 2.059, 2.101, 2.143, 2.187, 2.227, 2.269, 2.311, 2.354,
         2.395,2.437,2.480,2.523,2.564,2.606,2.648,2.691,2.772,2.854,2.940,3.028,3.108,3.191,3.275,3.362,3.444,3.528,
         3.613,3.701,3.858,4.023,4.194,4.372,4.532,4.698,4.870,5.048,5.206,5.369,5.537,5.711,6.024,6.353,6.701,7.067,
         7.381,7.709,8.051,8.409,8.727,9.057,9.400,9.756,10.291,10.855,11.450,12.077,12.710,13.376,14.077,14.815,15.440,
         16.249,17.100,17.476,18.538,19.663,20.857,22.124,23.338,24.620,25.971,27.397,28.902,30.303,31.646,33.003,34.364,
         37.037,39.682][byte]


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

    def coarse_fine_fixed_hz(coarse, fine):
        coarse = coarse & 3
        return 10 ** (coarse + fine / 100)
    
    def coarse_fine_ratio(coarse,fine):
        coarse = coarse & 31
        if(coarse == 0):
            coarse = 0.5
        return coarse * (1 + fine / 100)

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
        op["opamp"] = output_level_to_amp(opstruct["opamp"])
        if(opstruct["tuning"] == "fixed"):
            op["fixedhz"] = coarse_fine_fixed_hz(opstruct["coarse"], opstruct["fine"])
        else:
            op["ratio"] = coarse_fine_ratio(opstruct["coarse"], opstruct["fine"])
        op["detunehz"] = opstruct["detune"]
        ops.append(op)
    patch["ops"] = ops

    (patch["bp_pitch_rates"], patch["bp_pitch_times"]) = (
        eg_to_bp_pitch(patchstruct["pitch_rate"], patchstruct["pitch_level"]))
    patch["algo"] = patchstruct["algo"] - 1
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
def play_patch(patch, midinote=50, length_s = 2, keyup_s = 1):
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
    
    # Render Ralph
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



