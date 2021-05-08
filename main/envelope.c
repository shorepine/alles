// envelope.c
// VCA -- handle LFOs and ADSR

#include "alles.h"

extern struct event* synth;
extern struct mod_event* msynth;
extern struct mod_state mglobal;



// LFO scale is not like ADSR scale, it can also make a thing bigger, so return range is between -1 and 1, where 1 = 2x and 0 = 1x
float compute_lfo_scale(uint8_t osc) {
    int8_t source = synth[osc].lfo_source;
    if(synth[osc].lfo_target >= 1 && source >= 0) {
        if(source != osc) {  // that would be weird
            msynth[source].amp = synth[source].amp;
            msynth[source].duty = synth[source].duty;
            msynth[source].freq = synth[source].freq;
            if(synth[source].wave == NOISE) return compute_lfo_noise(source);
            if(synth[source].wave == SAW) return compute_lfo_saw(source);
            if(synth[source].wave == PULSE) return compute_lfo_pulse(source);
            if(synth[source].wave == TRIANGLE) return compute_lfo_triangle(source);
            if(synth[source].wave == SINE) return compute_lfo_sine(source);
            if(synth[source].wave == PCM) return compute_lfo_pcm(source);
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
    int64_t sysclock = esp_timer_get_time() / 1000;
    float scale = 1.0; // the overall ratio to modify the thing
    float t_a = synth[osc].adsr_a;
    float t_d = synth[osc].adsr_d;
    float S   = synth[osc].adsr_s;
    float t_r = synth[osc].adsr_r;
    float curve = 3.0;
    if(synth[osc].adsr_on_clock >= 0) { 
        int64_t elapsed = (sysclock - synth[osc].adsr_on_clock) + 1; // +1ms to avoid nans 
        if(elapsed > t_a) { // we're in sustain or decay
            scale = S + (1.0-S)*expf(-(elapsed - t_a)/(t_d / curve));
            //printf("sus/decay. elapsed %lld. aoc %lld.\n", elapsed, synth[osc].adsr_on_clock);
        } else { // attack
            scale = 1.0 - expf(-curve * (elapsed / t_a));
            //printf("attack. elapsed %lld. aoc %lld.\n", elapsed, synth[osc].adsr_on_clock);
        }
    } else if(synth[osc].adsr_off_clock >= 0) { // release
        int64_t elapsed = (sysclock - synth[osc].adsr_off_clock) + 1;
        scale = S * expf(-curve * elapsed / t_r);
        //printf("release. elapsed %lld. aoffc %lld.\n", elapsed, synth[osc].adsr_off_clock);
        if(elapsed > t_r) {
            // Turn off note
            synth[osc].status=OFF;
            synth[osc].adsr_off_clock = -1;
        }
    }
    if(scale < 0) scale = 0;
    //printf("scale %f for a %f d %f s %f r %f\n", scale, t_a, t_d, S, t_r);
    return scale;
}


