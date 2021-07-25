import amy, alles_util
#from amy import send, volume, note_on, note_off, reset, millis
#from alles_util import connect, disconnect, sync
import time

#from amy import SINE, PULSE, SAW, TRIANGLE, NOISE, KS, PCM, ALGO, OFF, PARTIAL, PARTIALS
#from amy import TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE, TARGET_LINEAR
#from amy import FILTER_NONE, FILTER_LPF, FILTER_BPF, FILTER_HPF
#from amy import BLOCK_SIZE, SAMPLE_RATE, OSCS, MAX_QUEUE

"""
    A bunch of useful presets
"""
def preset(which,osc=0, **kwargs):
    # Reset the osc first
    amy.reset(osc=osc)
    if(which==0): # simple note
        amy.send(osc=osc, wave=amy.SINE, bp0="10,1,250,0.7,250,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==1): # filter bass
        amy.send(osc=osc, filter_freq=2500, resonance=5, wave=amy.SAW, filter_type=amy.FILTER_LPF, bp0="100,0.5,25,0", bp0_target=amy.TARGET_AMP+amy.TARGET_FILTER_FREQ, **kwargs)
    if(which==2): # long sine pad to test ADSR
        amy.send(osc=osc, wave=amy.SINE, bp0="0,0,500,1,1000,0.25,750,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==3): # amp LFO example
        amy.reset(osc=osc+1)
        amy.send(osc=osc+1, wave=amy.SINE, vel=0.50, freq=1.5, **kwargs)
        amy.send(osc=osc, wave=amy.PULSE, envelope="150,1,250,0.25,250,0", bp0_target=amy.TARGET_AMP, lfo_target=amy.TARGET_AMP, lfo_source=osc+1, **kwargs)
    if(which==4): # pitch LFO going up 
        amy.reset(osc=osc+1)
        amy.send(osc=osc+1, wave=amy.SINE, vel=0.50, freq=0.25, **kwargs)
        amy.send(osc=osc, wave=amy.PULSE, bp0="150,1,400,0,0,0", bp0_target=amy.TARGET_AMP, lfo_target=amy.TARGET_FREQ, lfo_source=osc+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at 0.5 phase (going down) to modify frequency of another sine wave
        amy.reset(osc=osc+1)
        amy.send(osc=osc+1, wave=amy.SINE, vel=0.50, freq=0.25, phase=0.5, **kwargs)
        amy.send(osc=osc, wave=amy.SINE, vel=0, bp0="500,0,0,0", bp0_target=amy.TARGET_AMP, lfo_target=amy.TARGET_FREQ, lfo_source=osc+1, **kwargs)
    if(which==6): # noise snare
        amy.send(osc=osc, wave=amy.NOISE, vel=0, bp0="250,0,0,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==7): # closed hat
        amy.send(osc=osc, wave=amy.NOISE, vel=0, envelope="25,1,75,0,0,0", bp0_target=amy.TARGET_AMP, **kwargs)
    if(which==8): # closed hat from PCM 
        amy.send(osc=osc, wave=amy.PCM, vel=0, patch=17, freq=22050, **kwargs)
    if(which==9): # cowbell from PCM
        amy.send(osc=osc, wave=amy.PCM, vel=0, patch=25, freq=22050, **kwargs)
    if(which==10): # high cowbell from PCM
        amy.send(osc=osc, wave=amy.PCM, vel=0, patch=25, freq=31000, **kwargs)
    if(which==11): # snare from PCM
        amy.send(osc=osc, wave=amy.PCM, vel=0, patch=5, freq=22050, **kwargs)
    if(which==12): # FM bass 
        amy.send(osc=osc, wave=amy.ALGO, vel=0, patch=15, **kwargs)
    if(which==13): # Pcm bass drum
        amy.send(osc=osc, wave=amy.PCM, vel=0, patch=20, freq=22050, **kwargs)

def reset():
    amy.reset()
"""
    Run a scale through all the synth's sounds
"""
def test():
    while True:
        for wave in [amy.SINE, amy.SAW, amy.PULSE, amy.TRIANGLE, amy.NOISE]:
            for i in range(12):
                amy.note_on(osc=0, wave=wave, note=40+i, patch=i)
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
            amy.note_on(osc=i % amy.OSCS, note=i+50, wave=amy.ALGO, patch=patch, **kwargs)
            time.sleep(wait)
            note_off(osc=i % amy.OSCS)

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
        amy.note_on(osc=osc, **kwargs, patch=note, filter_type=amy.FILTER_NONE, filter_freq=note*50, note=30+(note), client = -1)
        time.sleep(0.5)
        note =(note + 1) % 64

def eq_test():
    amy.reset()
    eqs = [ [0,0,0], [15,0,0], [0,0,15], [0,15,0],[-15,-15,15],[-15,-15,30],[-15,30,-15], [30,-15,-15] ]
    for eq in eqs:
        print("eq_l = %ddB eq_m = %ddB eq_h = %ddB" % (eq[0], eq[1], eq[2]))
        amy.send(eq_l=eq[0], eq_m=eq[1], eq_h=eq[2])
        drums(loops=2)
        time.sleep(1)
        amy.reset()
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
            amy.note_on(osc=0,filter_type=amy.FILTER_LPF, filter_freq=cur+250, resonance=res, wave=amy.PULSE, note=50+i, duty=0.50)
            amy.note_on(osc=1,filter_type=amy.FILTER_LPF, filter_freq=cur+500, resonance=res, wave=amy.PULSE, note=50+12+i, duty=0.25)
            amy.note_on(osc=2,filter_type=amy.FILTER_LPF, filter_freq=cur, resonance=res, wave=amy.PULSE, note=50+6+i, duty=0.90)
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
                amy.note_on(osc=0, note=38, vel=2.5, **kwargs)
            if(x & snare):
                amy.note_on(osc=2, vel=1.5)
            if(x & hat): 
                amy.note_on(osc=3, vel=1)
            if(x & cow): 
                amy.note_on(osc=4, vel=1)
            if(x & hicow): 
                amy.note_on(osc=5, vel=1)
            if(bassline[i]>0):
                amy.note_on(osc=7, vel=0.5, note=bassline[i]-12, **kwargs)
            else:
                amy.note_off(osc=7, **kwargs)
            time.sleep(1.0/(bpm*2/60))

"""
    A small pattern using FM + sine oscs
"""    
def complex(speed=0.250, loops=-1, **kwargs):
    while(loops != 0): # -1 means forever 
        for i in [0,2,4,5, 0, 4, 0, 2]:
            amy.note_on(osc=0, wave=amy.ALGO, vel=0.8, note=50+i, patch=15, **kwargs)
            time.sleep(speed)
            amy.note_on(osc=8, wave=amy.ALGO, vel=0.6, note=50+i, patch=8, **kwargs)
            time.sleep(speed)
            amy.note_on(osc=16, wave=amy.SINE, vel=0.5, note=62+i, patch=2, **kwargs)
            time.sleep(speed)
            amy.note_on(osc=16, wave=amy.SINE, vel=1, freq = 20, **kwargs)
            time.sleep(speed)
        loops = loops - 1


"""
    C-major chord
"""
def c_major(octave=2,wave=amy.SINE, **kwargs):
    amy.note_on(osc=0, freq=220.5*octave, wave=wave, **kwargs)
    amy.note_on(osc=1, freq=138.5*octave, wave=wave, **kwargs)
    amy.note_on(osc=2, freq=164.5*octave, wave=wave, **kwargs)



# Setup the sock on module import
alles_util.connect()


