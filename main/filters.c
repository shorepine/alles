// filters.cc
#include "alles.h"
#include "esp_dsp.h"

extern struct mod_state mglobal; 

	float coeffs[5];
	float delay[2] = {0,0};

void filter_update() {
	dsps_biquad_gen_lpf_f32(coeffs, mglobal.filter_freq/((float)SAMPLE_RATE/2.0), mglobal.resonance);
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
	// process in place
	// Update the coeffs each tick, this is a quick process
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
