// algorithms.c
// FM2 and partial synths that involve combinations of oscillators



#include "amy.h"

extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by modulations & envelopes
extern struct state global; 
extern int64_t total_samples ;


// From MSFA, they encode the dx7's 32 algorithms this way:
/*
struct FmOperatorInfo {
  int in;
  int out;
};

enum FmOperatorFlags {
  OUT_BUS_ONE = 1 << 0,
  OUT_BUS_TWO = 1 << 1,
  OUT_BUS_ADD = 1 << 2,
  IN_BUS_ONE = 1 << 4,
  IN_BUS_TWO = 1 << 5,
  FB_IN = 1 << 6,
  FB_OUT = 1 << 7
};

struct FmAlgorithm {
  int ops[6];
};


const FmAlgorithm algorithms[32] = {
  { { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 } }, // 1
  { { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 } }, // 2
  { { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 } }, // 3
  { { 0x41, 0x11, 0x94, 0x01, 0x11, 0x14 } }, // 4
  { { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 } }, // 5
  { { 0x41, 0x94, 0x01, 0x14, 0x01, 0x14 } }, // 6
  { { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 } }, // 7
  { { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 } }, // 8
  { { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 } }, // 9
  { { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 } }, // 10
  { { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 } }, // 11
  { { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 } }, // 12
  { { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 } }, // 13
  { { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 } }, // 14
  { { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 } }, // 15
  { { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 } }, // 16
  { { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 } }, // 17
  { { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 } }, // 18
  { { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 } }, // 19
  { { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 } }, // 20
  { { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 } }, // 21
  { { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 } }, // 22
  { { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 } }, // 23
  { { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 } }, // 24
  { { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 } }, // 25
  { { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 } }, // 26
  { { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 } }, // 27
  { { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 } }, // 28
  { { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 } }, // 29
  { { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 } }, // 30
  { { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 } }, // 31
  { { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 } }, // 32
};
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
