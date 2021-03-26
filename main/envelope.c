// envelope.c
// VCA -- handle LFOs and ADSR

#include "alles.h"

extern struct event* synth;
extern struct mod_event* msynth;
extern struct mod_state mglobal;


// Re-trigger an LFO source voice on note on. 
void retrigger_lfo_source(uint8_t voice) {
	synth[voice].step = 0;
	synth[voice].substep = 0;
	synth[voice].sample = DOWN;
}

// LFO scale is not like ADSR scale, it can also make a thing bigger, so return range is between -1 and 1, where 1 = 2x and 0 = 1x
float compute_lfo_scale(uint8_t voice) {
	int8_t source = synth[voice].lfo_source;
	if(synth[voice].lfo_target >= 1 && source >= 0) {
		if(source != voice) {  // that would be weird
			// Render the wave. you only need / get the first sample, so maybe there's a faster way to do this
			msynth[source].amp = synth[source].amp;
			msynth[source].duty = synth[source].duty;
			msynth[source].freq = synth[source].freq;
			float floatblock[BLOCK_SIZE];
			for(uint16_t i=0;i<BLOCK_SIZE;i++) { floatblock[i] = 0; }
			if(synth[source].wave == NOISE) render_noise(floatblock, source);
			if(synth[source].wave == SAW) render_saw(floatblock, source);
			if(synth[source].wave == PULSE) render_pulse(floatblock, source);
			if(synth[source].wave == TRIANGLE) render_triangle(floatblock, source);
			if(synth[source].wave == SINE) render_sine(floatblock, source);
            return floatblock[0] / 16384.0; // will be between -1 and 1
  		}
	}
	return 0; // 0 is no change, unlike ADSR scale
}

// dpwe approved exp ADSR curves:
//def attack(t, attack):
//    return 1-exp(-3*(t/attack))
//def decay_and_sustain(t, attack, decay, S):
//    return S + (1-S)*exp(-(t - attack)/(decay / 3))
//def release(t, release, S):
//    return S*exp(-3 * t / release)
float compute_adsr_scale_exp(uint8_t voice) {
	// get the scale out of a voice
	int64_t sysclock = esp_timer_get_time() / 1000;
	float scale = 1.0; // the overall ratio to modify the thing
	float t_a = synth[voice].adsr_a;
	float t_d = synth[voice].adsr_d;
	float S   = synth[voice].adsr_s;
	float t_r = synth[voice].adsr_r;
	float curve = 3.0;
	if(synth[voice].adsr_on_clock >= 0) { 
		int64_t elapsed = sysclock - synth[voice].adsr_on_clock;
		if(elapsed > t_a) { // we're in sustain or decay
			scale = S + (1.0-S)*expf(-(elapsed - t_a)/(t_d / curve));
		} else { // attack
			scale = 1.0 - expf(-curve * (elapsed / t_a));
		}
	} else if(synth[voice].adsr_off_clock >= 0) { // release
		int64_t elapsed = sysclock - synth[voice].adsr_off_clock;
		scale = S * expf(-curve * elapsed / t_r);
		if(elapsed > t_r) {
			// Turn off note
			synth[voice].status=OFF;
			synth[voice].adsr_off_clock = -1;
		}
	}
	if(scale < 0) scale = 0;
	return scale;
}


float compute_adsr_scale(uint8_t voice) {
	// get the scale out of a voice
	int64_t sysclock = esp_timer_get_time() / 1000;
	float scale = 1.0; // the overall ratio to modify the thing

	if(synth[voice].adsr_on_clock >= 0) { 
		// figure out where we are in the curve
		int64_t elapsed = sysclock - synth[voice].adsr_on_clock;
		if(elapsed > (synth[voice].adsr_d+synth[voice].adsr_a)) { // we're in sustain
			scale = synth[voice].adsr_s;
		} else if(elapsed > synth[voice].adsr_a) { // decay
			// compute the decay scale
			elapsed = elapsed - synth[voice].adsr_a; // time since d started 
			float elapsed_ratio = ((float) elapsed / (float)synth[voice].adsr_d);
			scale = 1.0 - ((1.0-synth[voice].adsr_s) * elapsed_ratio);
		} else { // attack
			// compute the attack scale
			scale = (float)elapsed / (float)synth[voice].adsr_a; 
		}
	} else if(synth[voice].adsr_off_clock >= 0) {
		int64_t elapsed = sysclock - synth[voice].adsr_off_clock;
		if(elapsed > synth[voice].adsr_r) {
			scale = 0; // note is done
			synth[voice].status=OFF; // or else it'll just start playing as an oscillator again
			// Turn off release clock
			synth[voice].adsr_off_clock = -1;
		} else {
			// compute the release scale
			scale = synth[voice].adsr_s - (((float) elapsed / (float) synth[voice].adsr_r) * synth[voice].adsr_s); 
		}
	}
	if(scale < 0) scale = 0;
	return scale;
}

