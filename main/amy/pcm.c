// pcm.c

#include "amy.h"

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint32_t loopstart;
    uint32_t loopend;
    uint8_t midinote;
} pcm_map_t;

#include "pcm.h"

void pcm_note_on(uint8_t osc) {
    // If no freq given, we set it to default PCM SR. e.g. freq=11025 plays PCM at half speed, freq=44100 double speed 
    if(synth[osc].freq <= 0) synth[osc].freq = PCM_SAMPLE_RATE;
    // If patch is given, set step directly from patch's offset
    if(synth[osc].patch>=0) {
        synth[osc].step = pcm_map[synth[osc].patch].offset; // start sample
        // Use substep here as "end sample" so we don't have to add another field to the struct
        synth[osc].substep = synth[osc].step + pcm_map[synth[osc].patch].length; // end sample
    } else { // no patch # given? use phase as index into PCM buffer
        synth[osc].step = PCM_LENGTH * synth[osc].phase; // start at phase offset
        synth[osc].substep = PCM_LENGTH; // play until end of buffer and stop 
    }
}
void pcm_mod_trigger(uint8_t osc) {
    pcm_note_on(osc);
}

// TODO -- this just does one shot, no looping (will need extra loop parameter? what about sample metadata looping?) 
// TODO -- this should just be like render_LUT(float * buf, uint8_t osc, int16_t **LUT) as it's the same for sine & PCM?
void render_pcm(float * buf, uint8_t osc) {
    float skip = msynth[osc].freq / (float)SAMPLE_RATE;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        float sample = pcm[(int)(synth[osc].step)]/(float)SAMPLE_MAX; // makes it -1 to 1
        synth[osc].step = (synth[osc].step + skip);
        if(synth[osc].step >= synth[osc].substep ) { // end
            synth[osc].status=OFF;
            sample = 0;
        }
        buf[i] = (sample * msynth[osc].amp);
    }
}

float compute_mod_pcm(uint8_t osc) {
    float mod_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float skip = msynth[osc].freq / mod_sr;
    float sample = pcm[(int)(synth[osc].step)];
    synth[osc].step = (synth[osc].step + skip);
    if(synth[osc].step >= synth[osc].substep ) { // end
        synth[osc].status=OFF;
        sample = 0;
    }
    return (sample * msynth[osc].amp) / SAMPLE_MAX; // -1 .. 1
    
}
