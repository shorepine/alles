// algorithms.c
// FM2 and partial synths that involve combinations of oscillators
#include "amy.h"

typedef struct {
    float freq;
    float freq_ratio;
    float amp;
    float amp_rate[4];
    float amp_time[4];
    float detune;
} operator_parameters_t;


typedef struct  {
    uint8_t algo;
    float feedback;
    float pitch_rate[4];
    uint16_t pitch_time[4];
    float lfo_freq;
    int8_t lfo_wave;
    float lfo_amp;
    int8_t lfo_target;
    operator_parameters_t ops[MAX_ALGO_OPS];
} algorithms_parameters_t;

#include "fm.h"
extern uint8_t debug_on;
// Thank you MFSA for the DX7 op structure , borrowed here \/ \/ \/ 
enum FmOperatorFlags {
    OUT_BUS_ONE = 1 << 0,
    OUT_BUS_TWO = 1 << 1,
    OUT_BUS_ADD = 1 << 2,
    // there is no 1 << 3
    IN_BUS_ONE = 1 << 4,
    IN_BUS_TWO = 1 << 5,
    FB_IN = 1 << 6,
    FB_OUT = 1 << 7
};
struct FmAlgorithm { uint8_t ops[MAX_ALGO_OPS]; };
struct FmAlgorithm algorithms[32] = {
    // 6     5     4     3     2      1   
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
// End of MSFA stuff

float zeros[BLOCK_SIZE];


// a = 0
void zero(float *a) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        a[i] = 0;
    }
}


// b = a + b
void add(float *a, float*b) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        b[i] = (a[i] + b[i]);
    }
}

// b = a 
void copy(float *a, float*b) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        b[i] = a[i];
    }
}

void render_mod(float *in, float*out, uint8_t osc, float feedback_level, uint8_t algo_osc) {

    /*if(osc == 1) {
        debug_on = 1;
    } else {
        debug_on = 0;
    }
    */
    hold_and_modify(osc);
    // out = buf
    // in = mod
    // so render_mod is mod, buf (out)
    if(synth[osc].wave == SINE) render_fm_sine(out, osc, in, feedback_level, algo_osc);
}

void note_on_mod(uint8_t osc, uint8_t algo_osc) {
    synth[osc].note_on_clock = total_samples;
    synth[osc].status = IS_ALGO_SOURCE; // to ensure it's rendered
    if(synth[osc].wave==SINE) fm_sine_note_on(osc, algo_osc);
}

void algo_note_off(uint8_t osc) {
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        if(synth[osc].algo_source[i] >=0 ) {
            uint8_t o = synth[osc].algo_source[i];
            synth[o].note_on_clock = -1;
            synth[o].note_off_clock = total_samples; 
        }
    }
    // osc note off, start release
    synth[osc].note_on_clock = -1;
    synth[osc].note_off_clock = total_samples;          
}

void algo_setup_patch(uint8_t osc) {
    algorithms_parameters_t p = fm_patches[synth[osc].patch % ALGO_PATCHES];
    synth[osc].algorithm = p.algo;
    synth[osc].feedback = p.feedback;
    synth[osc].breakpoint_target[1] = TARGET_FREQ+TARGET_LINEAR;
    synth[osc].mod_source = -1;
    synth[osc].mod_target = 0;
    float time_ratio = 1;
    if(synth[osc].ratio >=0 ) time_ratio = synth[osc].ratio;
    for(uint8_t i=0;i<4;i++) {
        synth[osc].breakpoint_values[1][i] = p.pitch_rate[i];
        synth[osc].breakpoint_times[1][i] = ms_to_samples((int)((float)p.pitch_time[i]/time_ratio));
    }
    if(p.lfo_target > 0) {
        synth[osc].mod_target = p.lfo_target;
        synth[osc+MAX_ALGO_OPS+1].freq = p.lfo_freq * time_ratio;
        synth[osc+MAX_ALGO_OPS+1].wave = p.lfo_wave;
        synth[osc+MAX_ALGO_OPS+1].status = IS_MOD_SOURCE;
        synth[osc+MAX_ALGO_OPS+1].amp = p.lfo_amp;
        synth[osc].mod_source = osc+MAX_ALGO_OPS+1;
    }

    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        synth[osc].algo_source[i] = osc+i+1;
        operator_parameters_t op = p.ops[i];
        synth[osc+i+1].freq = op.freq;
        if(synth[osc+i+1].freq < 0) synth[osc+i+1].freq = 0;
        synth[osc+i+1].status = IS_ALGO_SOURCE;
        synth[osc+i+1].ratio = op.freq_ratio;
        synth[osc+i+1].amp = op.amp;
        synth[osc+i+1].breakpoint_target[0] = TARGET_AMP+TARGET_LINEAR;
        for(uint8_t j=0;j<4;j++) {
            synth[osc+i+1].breakpoint_values[0][j] = op.amp_rate[j];
            synth[osc+i+1].breakpoint_times[0][j] =  ms_to_samples((int)((float)op.amp_time[j]/time_ratio));
            if(op.freq>0) { // set pitch BP for non ratio ops
                synth[osc+i+1].breakpoint_target[1] = TARGET_FREQ+TARGET_LINEAR;
                synth[osc+i+1].breakpoint_values[1][j] = p.pitch_rate[j];
                synth[osc+i+1].breakpoint_times[1][j] = ms_to_samples((int)((float)p.pitch_time[j]/time_ratio));              
            }
        }
        synth[osc+i+1].detune = op.detune;
    }
}

void algo_note_on(uint8_t osc) {    
    debug_on = 1;
    // trigger all the source operator voices
    if(synth[osc].patch >= 0) { 
        algo_setup_patch(osc);
    }
    for(uint8_t i=0;i<MAX_ALGO_OPS;i++) {
        if(synth[osc].algo_source[i] >=0 ) {
            note_on_mod(synth[osc].algo_source[i], osc);
        }
    }            
}

void algo_init() {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) zeros[i] = 0;
}




void render_algo(float * buf, uint8_t osc) { 
    float scratch[5][BLOCK_SIZE];

    struct FmAlgorithm algo = algorithms[synth[osc].algorithm];

    // starts at op 6
    float *in_buf, *out_buf;

    // TODO, i think i need at most 2 of these buffers, maybe 3?? 
    zero(scratch[0]);
    zero(scratch[1]);
    zero(scratch[2]);
    zero(scratch[3]);
    zero(scratch[4]);
    for(uint8_t op=0;op<MAX_ALGO_OPS;op++) {
        if(synth[osc].algo_source[op] >=0 && synth[synth[osc].algo_source[op]].status == IS_ALGO_SOURCE) {
            if(debug_on) {
                printf("------------------\n");
                printf("op %d ", 6-op);
                if(algo.ops[op] & FB_IN) printf(" FB_IN");
                if(algo.ops[op] & IN_BUS_ONE) printf(" IN_BUS_ONE");
                if(algo.ops[op] & IN_BUS_TWO) printf(" IN_BUS_TWO");
                if(algo.ops[op] & OUT_BUS_ONE) printf(" OUT_BUS_ONE");
                if(algo.ops[op] & OUT_BUS_TWO) printf(" OUT_BUS_TWO");
                if(algo.ops[op] & OUT_BUS_ADD) printf(" OUT_BUS_ADD");
                printf("\n");
            }
            float feedback_level = 0;
            if(algo.ops[op] & FB_IN) { 
                feedback_level = synth[osc].feedback; 
                if(debug_on)printf("op %d fb is %f\n", 6-op, feedback_level);
            } // main algo voice stores feedback, not the op 
            
            if(algo.ops[op] & IN_BUS_ONE) { 
                in_buf = scratch[0]; 
                if(debug_on)printf("op %d in_buf = s0\n", 6-op);
            } else if(algo.ops[op] & IN_BUS_TWO) { 
                in_buf = scratch[1]; 
                if(debug_on)printf("op %d in_buf = s1\n", 6-op);
            } else {
                // no in_buf
                in_buf = zeros;
                if(debug_on)printf("op %d in_buf = zeros\n", 6-op);
            }

            if(!(algo.ops[op] & OUT_BUS_ADD)) {
                if(algo.ops[op] & OUT_BUS_ONE) { 
                    zero(scratch[2]);
                    out_buf = scratch[2]; 
                    if(debug_on)printf("op %d out_buf = zerod out s2\n", 6-op);
                }
                if(algo.ops[op] & OUT_BUS_TWO) { 
                    zero(scratch[3]);
                    out_buf = scratch[3]; 
                    if(debug_on)printf("op %d out_buf = zerod out s3\n", 6-op);
                }
            } else {
                zero(scratch[4]);
                out_buf = scratch[4]; 
                if(debug_on)printf("op %d out_buf = zerod out s4\n", 6-op);
            }

            if(debug_on)printf("op %d rendering a sine modded with in_buf into out_buf\n", 6-op);
            render_mod(in_buf, out_buf, synth[osc].algo_source[op], feedback_level, osc);

            if(!(algo.ops[op] & OUT_BUS_ADD)) {
                if((algo.ops[op] & OUT_BUS_ONE) ) {
                    if(debug_on)printf("op %d copy out_buf into s0\n", 6-op);
                    copy(out_buf, scratch[0]);
                }

                if((algo.ops[op] & OUT_BUS_TWO) ) {
                    if(debug_on)printf("op %d copy out_buf into s1\n", 6-op);
                    copy(out_buf, scratch[1]);
                }
            } else {
                if(algo.ops[op] & OUT_BUS_ONE) {
                    add(scratch[4], scratch[0]);
                    if(debug_on)printf("op %d s0 = s4 + s0\n", 6-op) ;
                } else if(algo.ops[op] & OUT_BUS_TWO) {
                    add(scratch[4], scratch[1]); 
                    if(debug_on)printf("op %d s1 = s4 + s1\n", 6-op) ;
                } else {
                    add(scratch[4], buf);
                    if(debug_on)printf("op %d audio_buf = s4 + audio_buf\n", 6-op) ;
                }


            }
        }
    }
    if(debug_on)printf("transmitting buf out\n");
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] * msynth[osc].amp;
    }
    debug_on = 0;
}
