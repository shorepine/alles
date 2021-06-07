// algorithms.c
// FM2 and partial synths that involve combinations of oscillators

#include "amy.h"


extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by modulations & envelopes
extern struct state global; 
extern int64_t total_samples ;
// a struct for operators/algorithms?
// need a->b, (a+b), (a+b)->c, etc
// sort of like an in-order tree of things, each that work on the output of the last

/*

1,2,3,4
1+2,3,4
((1,2)+3),4
1,(2+3),4 
1,2,(3+4)
1,2,3,+4
(1+2+3),4

0:
MIX(0)
MOD(1)
MOD(2)
MOD(3)

meh, let's just do this manually for now with a big switch/case

*/

void mix(float *a, float*b, float *out) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        out[i] = (a[i] + b[i]) * 0.5;
    }
}

void render_mod(float *in, float*out, int8_t osc) {
    hold_and_modify(osc);
    if(synth[osc].wave == NOISE) render_noise(out, osc);
    if(synth[osc].wave == SAW) render_saw(out, osc, in);
    if(synth[osc].wave == PULSE) render_pulse(out, osc, in);
    if(synth[osc].wave == TRIANGLE) render_triangle(out, osc, in);
    if(synth[osc].wave == SINE) render_sine(out, osc, in);
}

void note_on_mod(int8_t osc) {
    synth[osc].adsr_on_clock = total_samples;
    if(synth[osc].wave==SINE) sine_note_on(osc);
    if(synth[osc].wave==SAW) saw_note_on(osc);
    if(synth[osc].wave==TRIANGLE) triangle_note_on(osc);
    if(synth[osc].wave==PULSE) pulse_note_on(osc);
    if(synth[osc].wave==PCM) pcm_note_on(osc);
}

void algo_note_off(uint8_t osc) {
    if(synth[osc].algorithm>=0 && synth[osc].algorithm<4) {
        for(uint8_t i=0;i<4;i++) {
            uint8_t o = synth[osc].algo_source[i];
            synth[o].adsr_on_clock = -1;
            synth[o].adsr_off_clock = total_samples; // esp_timer_get_time() / 1000;
        }            
    }
    if(synth[osc].algorithm==4) {
        for(uint8_t i=0;i<2;i++) {
            uint8_t o = synth[osc].algo_source[i];
            synth[o].adsr_on_clock = -1;
            synth[o].adsr_off_clock = total_samples; // esp_timer_get_time() / 1000;
        }            
    }
}

void algo_note_on(uint8_t osc) {    
    // trigger all the source operator voices
    if(synth[osc].algorithm>=0 && synth[osc].algorithm<4) {
        for(uint8_t i=0;i<4;i++) {
            note_on_mod(synth[osc].algo_source[i]);
        }            
    }
    if(synth[osc].algorithm==4) {
        note_on_mod(synth[osc].algo_source[0]);
        note_on_mod(synth[osc].algo_source[1]);        
    }
}

void render_algo(float * buf, uint8_t osc) { 
    float scratch[2][BLOCK_SIZE];
    switch(synth[osc].algorithm) {
        case 0:
            // 0->1->2->C
            render_mod(NULL, scratch[0], synth[osc].algo_source[0]);
            render_mod(scratch[0], scratch[1], synth[osc].algo_source[1]);
            render_mod(scratch[1], scratch[0], synth[osc].algo_source[2]);
            render_mod(scratch[0], buf, synth[osc].algo_source[3]);
            // Then we use osc's amp, freq, etc 
            break;
        case 1:
            //(0+1)->2->C
            render_mod(NULL, scratch[0], synth[osc].algo_source[0]);
            render_mod(NULL, scratch[1], synth[osc].algo_source[1]);
            mix(scratch[0], scratch[1], scratch[0]);
            render_mod(scratch[0], scratch[1], synth[osc].algo_source[2]);
            render_mod(scratch[1], buf, osc);
            break;
        case 2:
            // ((0->1)+2)->C
            render_mod(NULL, scratch[0], synth[osc].algo_source[0]);
            render_mod(scratch[0], scratch[1], synth[osc].algo_source[1]);
            render_mod(NULL, scratch[0], synth[osc].algo_source[2]);
            mix(scratch[1], scratch[0], scratch[1]);
            render_mod(scratch[1], buf, osc);
            break;
        case 3:
            // 0->(1+2)->C
            render_mod(NULL, scratch[0], synth[osc].algo_source[0]);
            render_mod(scratch[0], scratch[1], synth[osc].algo_source[1]);
            render_mod(scratch[0], buf, synth[osc].algo_source[2]);
            mix(scratch[1], buf, scratch[0]);
            render_mod(scratch[0], buf, osc);
            break;
        case 4:
            // 0->C
            render_mod(NULL, scratch[0], synth[osc].algo_source[0]);
            render_mod(scratch[0], buf, synth[osc].algo_source[1]);
            break;
    }
    // freq and etc can go elsehwere
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] * msynth[osc].amp;
    }
}
