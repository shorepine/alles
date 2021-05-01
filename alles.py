# Import all the utilities from alles_util needed to make sounds
from alles_util import send, sync, lowpass, volume, note_on, note_off, reset, connect, disconnect, millis
import time

# Some constants shared with the synth that help
ALLES_OSCILLATORS = 8
[SINE, PULSE, SAW, TRIANGLE, NOISE, FM, KS, PCM, OFF] = range(9)
TARGET_AMP, TARGET_DUTY, TARGET_FREQ, TARGET_FILTER_FREQ, TARGET_RESONANCE = (1, 2, 4, 8, 16)


"""
    A bunch of useful presets
"""
def preset(which,oscillator=0, **kwargs):
    # Reset the oscillator first
    reset(oscillator=oscillator)
    if(which==0): # simple note
        send(oscillator=oscillator, wave=SINE, envelope="10,250,0.7,250", adsr_target=TARGET_AMP, **kwargs)
    if(which==1): # filter bass
        lowpass(1000, 2)
        send(oscillator=oscillator, wave=SAW, envelope="10,100,0.5,25", adsr_target=TARGET_AMP+TARGET_FILTER_FREQ, **kwargs)
    if(which==2): # long square pad to test ADSR
        send(oscillator=oscillator, wave=PULSE, envelope="500,1000,0.25,750", adsr_target=TARGET_AMP, **kwargs)
    if(which==3): # amp LFO example
        reset(oscillator=oscillator+1)
        send(oscillator=oscillator+1, wave=SINE, vel=0.50, freq=1.5, **kwargs)
        send(oscillator=oscillator, wave=PULSE, envelope="150,250,0.25,250", adsr_target=TARGET_AMP, lfo_target=TARGET_AMP, lfo_source=oscillator+1, **kwargs)
    if(which==4): # pitch LFO going up 
        reset(oscillator=oscillator+1)
        send(oscillator=oscillator+1, wave=SINE, vel=0.50, freq=0.25, **kwargs)
        send(oscillator=oscillator, wave=PULSE, envelope="150,400,0,0", adsr_target=TARGET_AMP, lfo_target=TARGET_FREQ, lfo_source=oscillator+1, **kwargs)
    if(which==5): # bass drum
        # Uses a 0.25Hz sine wave at half phase (going down) to modify frequency of another sine wave
        reset(oscillator=oscillator+1)
        send(oscillator=oscillator+1, wave=SINE, vel=0.50, freq=0.25, phase=0.5, **kwargs)
        send(oscillator=oscillator, wave=SINE, vel=0, envelope="0,500,0,0", adsr_target=TARGET_AMP, lfo_target=TARGET_FREQ, lfo_source=oscillator+1, **kwargs)
    if(which==6): # noise snare
        send(oscillator=oscillator, wave=NOISE, vel=0, envelope="0,250,0,0", adsr_target=TARGET_AMP, **kwargs)
    if(which==7): # closed hat
        send(oscillator=oscillator, wave=NOISE, vel=0, envelope="25,75,0,0", adsr_target=TARGET_AMP, **kwargs)
    if(which==8): # closed hat from PCM 
        send(oscillator=oscillator, wave=PCM, vel=0, patch=17, freq=22050, **kwargs)
    if(which==9): # cowbell from PCM
        send(oscillator=oscillator, wave=PCM, vel=0, patch=25, freq=22050, **kwargs)
    if(which==10): # high cowbell from PCM
        send(oscillator=oscillator, wave=PCM, vel=0, patch=25, freq=31000, **kwargs)
    if(which==11): # snare from PCM
        send(oscillator=oscillator, wave=PCM, vel=0, patch=5, freq=22050, **kwargs)
    if(which==12): # FM bass 
        send(oscillator=oscillator, wave=FM, vel=0, patch=15, **kwargs)


"""
    Run a scale through all the synth's sounds
"""
def test():
    while True:
        for wave in [SINE, SAW, PULSE, TRIANGLE, FM, NOISE]:
            for i in range(12):
                note_on(oscillator=0, wave=wave, note=40+i, patch=i)
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
            note_on(oscillator=i % ALLES_OSCILLATORS, note=i+50, wave=FM, patch=patch, **kwargs)
            time.sleep(wait)
            note_off(oscillator=i % ALLES_OSCILLATORS)

"""
    Play up to ALLES_OSCILLATORS FM patches at once
"""
def polyphony():
    oscillator = 0
    note = 0
    while(1):
        note_on(oscillator=oscillator, wave=FM, patch=note, note=50+note, client = -1)
        time.sleep(0.5)
        oscillator =(oscillator + 1) % ALLES_OSCILLATORS
        note =(note + 1) % 24

"""
    Sweep the filter
"""
def sweep(speed=0.100, res=0.5, loops = -1):
    end = 2000
    cur = 0
    while(loops != 0):
        for i in [0, 1, 4, 5, 1, 3, 4, 5]:
            cur = (cur + 100) % end
            lowpass(cur, res)
            note_on(oscillator=0,wave=PULSE, note=50+i, duty=0.50)
            note_on(oscillator=1,wave=PULSE, note=50+12+i, duty=0.25)
            note_on(oscillator=2,wave=PULSE, note=50+6+i, duty=0.90)
            time.sleep(speed)

"""
    An example drum machine using oscillator+PCM presets
"""
def drums(bpm=120, **kwargs):
    preset(5, oscillator=0, **kwargs) # sine bass drum
    preset(8, oscillator=3, **kwargs) # sample hat
    preset(9, oscillator=4, **kwargs) # sample cow
    preset(10, oscillator=5, **kwargs) # sample hi cow
    preset(11, oscillator=2, **kwargs) # sample snare
    #preset(12, oscillator=7, **kwargs) # FM bass
    [bass, snare, hat, cow, hicow, silent] = [1, 2, 4, 8, 16, 32]
    pattern = [bass+hat, hat+hicow, bass+hat+snare, hat+cow, hat, hat+bass, snare+hat, hat]
    bassline = [50, 0, 0, 0, 50, 52, 51, 0]
    while True:
        for i,x in enumerate(pattern):
            if(x & bass): note_on(oscillator=0, note=50, vel=1.5, **kwargs)
            if(x & snare): note_on(oscillator=2, vel=1.5, **kwargs)
            if(x & hat): note_on(oscillator=3, vel=1, **kwargs)
            if(x & cow): note_on(oscillator=4, vel=1, **kwargs)
            if(x & hicow): note_on(oscillator=5, vel=1, **kwargs)
            #if(bassline[i]>0):
            #    note_on(oscillator=7, note=bassline[i], vel=0.25, **kwargs)
            #else:
            #    note_off(oscillator=7, **kwargs)
            time.sleep(1.0/(bpm*2/60))

"""
    A small pattern using FM + sine oscillators
"""    
def complex(speed=0.250, loops=-1, **kwargs):
    while(loops != 0): # -1 means forever 
        for i in [0,2,4,5, 0, 4, 0, 2]:
            note_on(oscillator=0, wave=FM, vel=0.8, note=50+i, patch=15, **kwargs)
            time.sleep(speed)
            note_on(oscillator=1, wave=FM, vel=0.6, note=50+i, patch=8, **kwargs)
            time.sleep(speed)
            note_on(oscillator=2, wave=SINE, vel=0.5, note=62+i, patch=2, **kwargs)
            time.sleep(speed)
            note_on(oscillator=2, wave=SINE, vel=1, freq = 20, **kwargs)
            time.sleep(speed)
        loops = loops - 1


"""
    C-major chord
"""
def c_major(octave=2,wave=SINE, **kwargs):
    note_on(oscillator=0, freq=220.5*octave, wave=wave, **kwargs)
    note_on(oscillator=1, freq=138.5*octave, wave=wave, **kwargs)
    note_on(oscillator=2, freq=164.5*octave, wave=wave, **kwargs)



# Setup the sock on module import
connect()


