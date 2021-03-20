// filters.cc
#include "alles.h"
#include "esp_dsp.h"

extern struct event *seq;
extern struct state global; 

float coeffs[5];
float delay[2];
void filter_update() {
	// update the coeffs
	dsps_biquad_gen_lpf_f32(coeffs, global.filter_freq/((float)SAMPLE_RATE/2.0), global.resonance);
}

void filter_process(float * block) {
	// process in place
	float output[BLOCK_SIZE];
	dsps_biquad_f32_ae32(block, output, BLOCK_SIZE, coeffs, delay);
	for(uint16_t i=0;i<BLOCK_SIZE;i++) {
		block[i] = output[i];
	}
}

void filters_deinit() {
}

void filters_init() {
	filter_update();
	delay[0] = 0; delay[1] = 0;
}
