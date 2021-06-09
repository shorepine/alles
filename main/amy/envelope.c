// envelope.c
// VCA -- handle modulation and ADSR

#include "amy.h"

extern struct event* synth;
extern struct mod_event* msynth;
extern struct mod_state mglobal;
extern int64_t total_samples;

// modulation scale is not like ADSR scale, it can also make a thing bigger, so return range is between -1 and 1, where 1 = 2x and 0 = 1x
float compute_mod_scale(uint8_t osc) {
    int8_t source = synth[osc].mod_source;
    if(synth[osc].mod_target >= 1 && source >= 0) {
        if(source != osc) {  // that would be weird
            msynth[source].amp = synth[source].amp;
            msynth[source].duty = synth[source].duty;
            msynth[source].freq = synth[source].freq;
            msynth[source].filter_freq = synth[source].filter_freq;
            msynth[source].resonance = synth[source].resonance;
            if(synth[source].wave == NOISE) return compute_mod_noise(source);
            if(synth[source].wave == SAW) return compute_mod_saw(source);
            if(synth[source].wave == PULSE) return compute_mod_pulse(source);
            if(synth[source].wave == TRIANGLE) return compute_mod_triangle(source);
            if(synth[source].wave == SINE) return compute_mod_sine(source);
            if(synth[source].wave == PCM) return compute_mod_pcm(source);
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
float compute_adsr_scale(uint8_t osc) {
    // get the scale out of a osc
    //int64_t sysclock = esp_timer_get_time() / 1000;
    float scale = 1.0; // the overall ratio to modify the thing
    int32_t t_a = synth[osc].adsr_a;
    int32_t t_d = synth[osc].adsr_d;
    float   S   = synth[osc].adsr_s;
    int32_t t_r = synth[osc].adsr_r;
    float curve = 3.0;
    if(synth[osc].adsr_on_clock >= 0) { 
        int64_t elapsed = (total_samples - synth[osc].adsr_on_clock) + 1; // +1 sample to avoid nans 
        if(elapsed > t_a) { // we're in sustain or decay
            scale = S + (1.0-S)*expf(-((float)elapsed - (float)t_a)/((float)t_d / curve));
        } else { // attack
            scale = 1.0 - expf(-curve * ((float)elapsed / (float)t_a));
        }
    } else if(synth[osc].adsr_off_clock >= 0) { // release
        int64_t elapsed = (total_samples - synth[osc].adsr_off_clock) + 1;
        scale = S * expf(-curve * (float)elapsed / (float)t_r);
        if(elapsed > t_r) {
            // Turn off note
            synth[osc].status=OFF;
            synth[osc].adsr_off_clock = -1;
        }
    }
    if(scale < 0) scale = 0;
    return scale;
}

