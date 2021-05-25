// filters.cc
#include "alles.h"
#include "esp_dsp.h"

extern struct mod_event *msynth; 
extern struct event *synth; 
extern struct state global;

#define LOWEST_RATIO 0.001

float coeffs[OSCS][5];
float delay[OSCS][2];

float eq_coeffs[3][5];
float eq_delay[3][2];

void update_filter(uint8_t osc) {
    // reset the delay for a filter
    // normal mod / adsr will just change the coeffs
    delay[osc][0] = 0; delay[osc][1] = 0;
}

void filters_init() {
    // update the parametric filters 
    dsps_biquad_gen_lpf_f32(eq_coeffs[0], EQ_CENTER_LOW /(float)SAMPLE_RATE, 0.707);
    dsps_biquad_gen_bpf_f32(eq_coeffs[1], EQ_CENTER_MED /(float)SAMPLE_RATE, 1.000);
    dsps_biquad_gen_hpf_f32(eq_coeffs[2], EQ_CENTER_HIGH/(float)SAMPLE_RATE, 0.707);
    for(uint8_t i=0;i<OSCS;i++) { delay[i][0] = 0; delay[i][1] = 0; }
    eq_delay[0][0] = 0; eq_delay[0][1] = 0;
    eq_delay[1][0] = 0; eq_delay[1][1] = 0;
    eq_delay[2][0] = 0; eq_delay[2][1] = 0;
}

void parametric_eq_process(float *block) {
    float output[3][BLOCK_SIZE];
    dsps_biquad_f32_ae32(block, output[0], BLOCK_SIZE, eq_coeffs[0], eq_delay[0]);
    dsps_biquad_f32_ae32(block, output[1], BLOCK_SIZE, eq_coeffs[1], eq_delay[1]);
    dsps_biquad_f32_ae32(block, output[2], BLOCK_SIZE, eq_coeffs[2], eq_delay[2]);

    for(uint16_t i=0;i<BLOCK_SIZE;i++)
        block[i] = (output[0][i] * global.eq[0]) - (output[1][i] * global.eq[1]) + (output[2][i] * global.eq[2]);
}



void filter_process(float * block, uint8_t osc) {
    float output[BLOCK_SIZE];
    float ratio = msynth[osc].filter_freq/(float)SAMPLE_RATE;
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    if(synth[osc].filter_type==FILTER_LPF) dsps_biquad_gen_lpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    if(synth[osc].filter_type==FILTER_BPF) dsps_biquad_gen_bpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    if(synth[osc].filter_type==FILTER_HPF) dsps_biquad_gen_hpf_f32(coeffs[osc], ratio, msynth[osc].resonance);
    dsps_biquad_f32_ae32(block, output, BLOCK_SIZE, coeffs[osc], delay[osc]);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        block[i] = output[i];
    }
}

void filters_deinit() {
}

