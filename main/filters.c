// filters.cc
#include "alles.h"
#include "esp_dsp.h"

extern struct mod_state mglobal; 


void filter_process(float * block) {
	// process in place
	// Update the coeffs each tick, this is a quick process
	float coeffs[5];
	float delay[2] = {0,0};
	dsps_biquad_gen_lpf_f32(coeffs, mglobal.filter_freq/((float)SAMPLE_RATE/2.0), mglobal.resonance);
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
