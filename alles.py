# Import all the utilities from alles_util needed to make sounds
from alles_util import send, sync, volume, note_on, note_off, reset, connect, disconnect, millis, flush, buffer
import time

# Some constants shared with the synth that help
ALLES_OSCS = 64
ALLES_MAX_QUEUE = 400
[SINE, PULSE, SAW, TRIANGLE, NOISE, FM, KS, PCM, ALGO, OFF] = range(10)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE = (1, 2, 4, 8, 16)
FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF = range(4)


"""
    A bunch of useful presets
"""
def preset(which,osc=0, **kwargs):
    # Reset the osc first
    reset(osc=osc)
    if(which==0): # simple note
        send(osc=osc, wave=SINE, envelope="10,250,0.7,250", adsr_target=TARGET_AMP, **kwargs)
    if(which==1): # filter bass
        send(osc=osc, filter_freq=2500, resonance=5, wave=SAW, filter_type=FILTER_LPF, envelope="0,100,0.5,25", adsr_target=TARGET_AMP+TARGET_FILTER_FREQ, **kwargs)
    if(which==2): # long square pad to test ADSR
        send(osc=osc, wave=PULSE, envelope="500,1000,0.25,750", adsr_target=TARGET_AMP, **kwargs)
    if(which==3): # amp LFO example
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=1.5, **kwargs)
        send(osc=osc, wave=PULSE, envelope="150,250,0.25,250", adsr_target=TARGET_AMP, lfo_target=TARGET_AMP, lfo_source=osc+1, **kwargs)
    if(which==4): # pitch LFO going up 
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=0.25, **kwargs)
        send(osc=osc, wave=PULSE, envelope="150,400,0,0", adsr_target=TARGET_AMP, lfo_target=TARGET_FREQ, lfo_source=osc+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        reset(osc=osc+1)
        send(osc=osc+1, wave=SINE, vel=0.50, freq=0.25, phase=0.5, **kwargs)
        send(osc=osc, wave=SINE, vel=0, envelope="0,500,0,0", adsr_target=TARGET_AMP, lfo_target=TARGET_FREQ, lfo_source=osc+1, **kwargs)
    if(which==6): # noise snare
        send(osc=osc, wave=NOISE, vel=0, envelope="0,250,0,0", adsr_target=TARGET_AMP, **kwargs)
    if(which==7): # closed hat
        send(osc=osc, wave=NOISE, vel=0, envelope="25,75,0,0", adsr_target=TARGET_AMP, **kwargs)
    if(which==8): # closed hat from PCM 
        send(osc=osc, wave=PCM, vel=0, patch=17, freq=22050, **kwargs)
    if(which==9): # cowbell from PCM
        send(osc=osc, wave=PCM, vel=0, patch=25, freq=22050, **kwargs)
    if(which==10): # high cowbell from PCM
        send(osc=osc, wave=PCM, vel=0, patch=25, freq=31000, **kwargs)
    if(which==11): # snare from PCM
        send(osc=osc, wave=PCM, vel=0, patch=5, freq=22050, **kwargs)
    if(which==12): # FM bass 
        send(osc=osc, wave=FM, vel=0, patch=15, **kwargs)


"""
    Run a scale through all the synth's sounds
"""
def test():
    while True:
        for wave in [SINE, SAW, PULSE, TRIANGLE, FM, NOISE]:
            for i in range(12):
                note_on(osc=0, wave=wave, note=40+i, patch=i)
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
            note_on(osc=i % ALLES_OSCS, note=i+50, wave=FM, patch=patch, **kwargs)
            time.sleep(wait)
            note_off(osc=i % ALLES_OSCS)

"""
    Play up to ALLES_OSCS patches at once
"""
def polyphony(max_voices=ALLES_OSCS,**kwargs):
    note = 0
    oscs = []
    for i in range(int(max_voices/2)):
        oscs.append(int(i))
        oscs.append(int(i+(ALLES_OSCS/2)))
    print(str(oscs))
    while(1):
        osc = oscs[note % max_voices]
        print("osc %d note %d filter %f " % (osc, 30+note, note*50))
        note_on(osc=osc, **kwargs, patch=note, filter_type=FILTER_NONE, filter_freq=note*50, note=30+(note), client = -1)
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
            note_on(osc=0,filter_type=FILTER_LPF, filter_freq=cur+250, resonance=res, wave=PULSE, note=50+i, duty=0.50)
            note_on(osc=1,filter_type=FILTER_LPF, filter_freq=cur+500, resonance=res, wave=PULSE, note=50+12+i, duty=0.25)
            note_on(osc=2,filter_type=FILTER_LPF, filter_freq=cur, resonance=res, wave=PULSE, note=50+6+i, duty=0.90)
            time.sleep(speed)

"""
    An example drum machine using osc+PCM presets
"""
def drums(bpm=120, loops=-1, **kwargs):
    preset(5, osc=0, **kwargs) # sine bass drum
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
                note_on(osc=0, note=38, vel=2.5, **kwargs)
            if(x & snare):
                note_on(osc=2, vel=1.5)
            if(x & hat): 
                note_on(osc=3, vel=1)
            if(x & cow): 
                note_on(osc=4, vel=1)
            if(x & hicow): 
                note_on(osc=5, vel=1)
            if(bassline[i]>0):
                note_on(osc=7, vel=0.5, note=bassline[i]-12, **kwargs)
            else:
                note_off(osc=7, **kwargs)
            time.sleep(1.0/(bpm*2/60))

"""
    A small pattern using FM + sine oscs
"""    
def complex(speed=0.250, loops=-1, **kwargs):
    while(loops != 0): # -1 means forever 
        for i in [0,2,4,5, 0, 4, 0, 2]:
            note_on(osc=0, wave=FM, vel=0.8, note=50+i, patch=15, **kwargs)
            time.sleep(speed)
            note_on(osc=1, wave=FM, vel=0.6, note=50+i, patch=8, **kwargs)
            time.sleep(speed)
            note_on(osc=2, wave=SINE, vel=0.5, note=62+i, patch=2, **kwargs)
            time.sleep(speed)
            note_on(osc=2, wave=SINE, vel=1, freq = 20, **kwargs)
            time.sleep(speed)
        loops = loops - 1


"""
    C-major chord
"""
def c_major(octave=2,wave=SINE, **kwargs):
    note_on(osc=0, freq=220.5*octave, wave=wave, **kwargs)
    note_on(osc=1, freq=138.5*octave, wave=wave, **kwargs)
    note_on(osc=2, freq=164.5*octave, wave=wave, **kwargs)



# Setup the sock on module import
connect()


