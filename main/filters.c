// filters.cc
#include "alles.h"
#include "esp_dsp.h"

extern struct mod_event *msynth; // the synth that is being modified by LFOs & envelopes

float coeffs[OSCS][5];
float delay[OSCS][2] = {{0,0}};

#define LOWEST_RATIO 0.0001


/*
multi-filters style

each osc N has a filter_source=X parameter
if set, the X osc is silent, but audio from X gets filtered by osc N before mixing
amp of X still sets amp of the signal
but amp of N sets wet/dry, where 1 = fully filtered, 0 = original source only
N.freq = filter freq
N.resonance = filter resonance
N.wave = lpf, bpf, etc
you can chain these right, so a filtered audio coming out of N can be filtered again by Y

let's start "simple" here and do filter_source 

why not even simpler, where F(filter_freq) sets filter per voice, and see how many we can do?

yeah. then we can do a more complex routing thing



==
but what about adding together voices and filtering a lot? 
what about FM, mixing?
what about partial ramps as voices
can't these all be set in .e.g python
but then how would anyone set them themselves, really







*/



void bpf_update(uint8_t osc) {
    float ratio = msynth[osc].filter_freq/((float)SAMPLE_RATE/2.0);
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    dsps_biquad_gen_bpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
}
void lpf_update(uint8_t osc) {
    float ratio = msynth[osc].filter_freq/((float)SAMPLE_RATE/2.0);
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    dsps_biquad_gen_lpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
}
/*
void lpf_process_ints(int16_t * block) {
    float input[BLOCK_SIZE];
    float output[BLOCK_SIZE];
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        input[i] = (float)block[i] / 32767.0;
    }
    dsps_biquad_f32_ae32(input, output, BLOCK_SIZE, coeffs, delay);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        block[i] = (output[i] * 32767.0);
    }

}
*/
void lpf_process(float * block, uint8_t osc) {
    float output[BLOCK_SIZE];
    lpf_update(osc);
    dsps_biquad_f32_ae32(block, output, BLOCK_SIZE, coeffs[osc], delay[osc]);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        block[i] = output[i];
    }
}

void filters_deinit() {
}

void filters_init() {

}
