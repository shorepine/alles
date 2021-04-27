// filters.cc
#include "alles.h"
#include "esp_dsp.h"

extern struct mod_state mglobal; 

float coeffs[5];
float delay[2] = {0,0};

#define LOWEST_RATIO 0.0001

void filter_update() {
    float ratio = mglobal.filter_freq/((float)SAMPLE_RATE/2.0);
    if(ratio < LOWEST_RATIO) ratio = LOWEST_RATIO;
    dsps_biquad_gen_lpf_f32(coeffs, ratio, mglobal.resonance);
    //printf("filtering ff %f res %f coeffs %f %f %f %f %f\n", mglobal.filter_freq, mglobal.resonance, coeffs[0], coeffs[1], coeffs[2], coeffs[3], coeffs[4]);    
}

void filter_process_ints(int16_t * block) {
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

void filter_process(float * block) {
    float output[BLOCK_SIZE];
    dsps_biquad_f32_ae32(block, output, BLOCK_SIZE, coeffs, delay);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        block[i] = output[i];
    }
}

void filters_deinit() {
}

void filters_init() {

}
