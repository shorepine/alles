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
float compute_adsr_scale(uint8_t voice) {
	// get the scale out of a voice
	int64_t sysclock = esp_timer_get_time() / 1000;
	float scale = 1.0; // the overall ratio to modify the thing
	float t_a = synth[voice].adsr_a;
	float t_d = synth[voice].adsr_d;
	float S   = synth[voice].adsr_s;
	float t_r = synth[voice].adsr_r;
	float curve = 3.0;
	if(synth[voice].adsr_on_clock >= 0) { 
		int64_t elapsed = (sysclock - synth[voice].adsr_on_clock) + 1; // +1 to avoid nans 
		if(elapsed > t_a) { // we're in sustain or decay
			scale = S + (1.0-S)*expf(-(elapsed - t_a)/(t_d / curve));
			//printf("sus/decay. elapsed %lld. aoc %lld.\n", elapsed, synth[voice].adsr_on_clock);
		} else { // attack
			scale = 1.0 - expf(-curve * (elapsed / t_a));
			//printf("attack. elapsed %lld. aoc %lld.\n", elapsed, synth[voice].adsr_on_clock);
		}
	} else if(synth[voice].adsr_off_clock >= 0) { // release
		int64_t elapsed = (sysclock - synth[voice].adsr_off_clock) + 1;
		scale = S * expf(-curve * elapsed / t_r);
		//printf("release. elapsed %lld. aoffc %lld.\n", elapsed, synth[voice].adsr_off_clock);
		if(elapsed > t_r) {
			// Turn off note
			synth[voice].status=OFF;
			synth[voice].adsr_off_clock = -1;
		}
	}
	if(scale < 0) scale = 0;
	//printf("scale %f for a %f d %f s %f r %f\n", scale, t_a, t_d, S, t_r);
	return scale;
}


