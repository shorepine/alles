import time
import random
import alles


def dir_drums(bpm=120, loops=-1, **kwargs):
    alles.preset(13, osc=0, **kwargs) # sample bass drum
    alles.preset(8, osc=3, **kwargs) # sample hat
    alles.preset(9, osc=4, **kwargs) # sample cow
    alles.preset(10, osc=5, **kwargs) # sample hi cow
    alles.preset(11, osc=2, **kwargs) # sample snare
    alles.preset(1, osc=7, **kwargs) # filter bass
    [bass, snare, hat, cow, hicow, silent] = [1, 2, 4, 8, 16, 32]
    pattern = [bass+hat, hat+hicow, bass+hat+snare, hat+cow, hat, hat+bass, snare+hat, hat]
    basslines = [[60, 0, 0, 0, 60, 62, 61, 0],[66, 0, 0, 0, 66, 68, 67, 0],[67, 0, 0, 0, 67, 69, 68, 0],
        [60, 60, 60, 60, 60, 60, 0, 60], [68, 68, 68, 68, 68, 68, 0, 68]]

    next_sleepytime_in = 4

    while (loops != 0):
        loops = loops - 1
        random.shuffle(pattern)
        bassline = random.choice(basslines)
        sleepytime = 1.0/(bpm*2.0/60)

        for i,x in enumerate(pattern):
            if(x & bass):
                alles.send(osc=0, vel=4, **kwargs)
            if(x & snare):
                alles.send(osc=2, vel=1.5)
            if(x & hat):
                alles.send(osc=3, vel=1)
            if(x & cow):
                alles.send(osc=4, vel=1)
            if(x & hicow):
                alles.send(osc=5, vel=1)
            if(bassline[i]>0):
                alles.send(osc=7, vel=0.5, note=bassline[i]-12, **kwargs)
            else:
                alles.send(vel=0, osc=7, **kwargs)

            if (next_sleepytime_in == 0):
                sleepytime_multi = random.choice([[1.0,8], [0.5,4], [0.333,3], [0.25,4]])
                sleepytime *= sleepytime_multi[0]
                next_sleepytime_in = sleepytime_multi[1]

            next_sleepytime_in -= 1
            time.sleep(sleepytime)


