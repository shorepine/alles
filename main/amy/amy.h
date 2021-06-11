
#ifndef __AMY_H
#define __AMY_H

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// Constants you can change if you want
#define OSCS 64              // # of simultaneous oscs to keep track of 
#define BLOCK_SIZE 128       // buffer block size in samples
#define EVENT_FIFO_LEN 3000  // number of events the queue can store
#ifdef ESP_PLATFORM
#define LATENCY_MS 1000      // fixed latency in milliseconds
#else
#define LATENCY_MS 10         // small latency for local mode
#endif
#define SAMPLE_RATE 44100    // playback sample rate
#define SAMPLE_MAX 32767

// buffering for i2s
#define BYTES_PER_SAMPLE 2
#define I2S_BUFFERS 4

// This can be 32 bit, int32_t -- helpful for digital output to a i2s->USB teensy3 board
#define I2S_SAMPLE_TYPE I2S_BITS_PER_SAMPLE_16BIT
typedef int16_t i2s_sample_type;

// D is how close the sample gets to the clip limit before the nonlinearity engages.  
// So D=0.1 means output is linear for -0.9..0.9, then starts clipping.
#define CLIP_D 0.1
#define MAX_RECEIVE_LEN 512  // max length of each message

#define MAX_DRIFT_MS 20000   // ms of time you can schedule ahead before synth recomputes time base
#define LINEAR_INTERP        // use linear interp for oscs
// "The cubic stuff is just showing off.  One would only ever use linear in prod." -- dpwe, May 10 2021 
//#define CUBIC_INTERP         // use cubic interpolation for oscs
// Sample values for modulation sources
#define UP    32767
#define DOWN -32768
// center frequencies for the EQ
#define EQ_CENTER_LOW 800.0
#define EQ_CENTER_MED 2500.0
#define EQ_CENTER_HIGH 7000.0

// modulation/ADSR target mask
#define TARGET_AMP 1
#define TARGET_DUTY 2
#define TARGET_FREQ 4
#define TARGET_FILTER_FREQ 8
#define TARGET_RESONANCE 16

#define FILTER_LPF 1
#define FILTER_BPF 2
#define FILTER_HPF 3
#define FILTER_NONE 0
#define SINE 0
#define PULSE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4
#define FM 5
#define KS 6
#define PCM 7
#define ALGO 8
#define OFF 9
#define EMPTY 0
#define SCHEDULED 1
#define PLAYED 2
#define AUDIBLE 3
#define IS_MOD_SOURCE 4
#define IS_ALGO_SOURCE 5

enum params{
    WAVE, PATCH, MIDI_NOTE, AMP, DUTY, FEEDBACK, FREQ, VELOCITY, PHASE, VOLUME, FILTER_FREQ, RESONANCE, 
    MOD_SOURCE, MOD_TARGET, FILTER_TYPE, EQ_L, EQ_M, EQ_H, ADSR_TARGET, ADSR_A, ADSR_D, ADSR_S, ADSR_R, ALGORITHM, 
    ALGO_SOURCE_0, ALGO_SOURCE_1, ALGO_SOURCE_2, ALGO_SOURCE_3,
    NO_PARAM
};

// Delta holds the individual changes from an event, it's sorted in order of playback time 
// this is more efficient in memory than storing entire events per message 
struct delta {
    uint32_t data; // casted to the right thing later
    enum params param; // which parameter is being changed
    uint32_t time; // what time to play / change this parameter
    int8_t osc; // which oscillator it impacts
    struct delta * next; // the next event, in time 
};


// Events are used to parse from ASCII UDP strings into, and also as each oscillators current internal state 
struct event {
    // todo -- clean up types here - many don't need to be signed anymore, and time doesn't need to be int64
    int64_t time;
    int8_t osc;
    int16_t wave;
    int16_t patch;
    int16_t midi_note;
    float amp;
    float duty;
    float feedback;
    float freq;
    uint8_t status;
    float velocity;
    float phase;
    float step;
    float substep;
    float sample;
    float volume;
    float filter_freq;
    float resonance;
    int8_t mod_source;
    int8_t mod_target;
    int8_t algorithm;
    int8_t adsr_target;
    int8_t filter_type;
    int64_t adsr_on_clock;
    int64_t adsr_off_clock;
    int32_t adsr_a;
    int32_t adsr_d;
    float adsr_s;
    int32_t adsr_r;
    int8_t algo_source[4];
    // State variable for the impulse-integrating oscs.
    float lpf_state;
    // Constant offset to add to sawtooth before integrating.
    float dc_offset;
    // Decay alpha of LPF filter (e.g. 0.99 or 0.999).
    float lpf_alpha;
    // Selected lookup table and size.
    const float *lut;
    int16_t lut_size;
    float eq_l;
    float eq_m;
    float eq_h;
    float feedback_level;
    float last_two[2];
};

// events, but only the things that mods/env can change. one per osc
struct mod_event {
    float amp;
    float duty;
    float freq;
    float filter_freq;
    float resonance;
};

struct event default_event();
void add_event(struct event e);
void render_task(uint8_t start, uint8_t end, uint8_t core);
void show_debug(uint8_t type) ;
void oscs_deinit() ;
void reset_oscs() ;

// global synth state
struct state {
    float volume;
    float eq[3];
    uint16_t event_qsize;
    int16_t next_event_write;
    struct delta * event_start; // start of the sorted list
};


int8_t oscs_init();
void parse_adsr(struct event * e, char* message) ;
void parse_algorithm(struct event * e, char* message) ;
void hold_and_modify(uint8_t osc) ;
int16_t * fill_audio_buffer_task();
void parse_task();
void parse_message(char * message);
void start_amy();
void stop_amy();

// bandlimted oscs
/*extern void lpf_buf(float *buf, float decay, float *state);
extern float render_lut(float * buf, float step, float skip, float amp, const float* lut, int16_t lut_size);
extern float render_lut_with_feedback(float * buf, float step, float skip, float amp, const float* lut, int16_t lut_size, float feedback_level);
extern void clear_buf(float *buf);
extern void cumulate_buf(const float *from, float *dest);
*/

extern void ks_init();
extern void ks_deinit();
extern void algo_init();
extern void render_ks(float * buf, uint8_t osc); 
extern void render_sine(float * buf, uint8_t osc); 
extern void render_fm_sine(float * buf, uint8_t osc, float *mod); 
extern void render_pulse(float * buf, uint8_t osc); 
extern void render_saw(float * buf, uint8_t osc);
extern void render_triangle(float * buf, uint8_t osc); 
extern void render_noise(float * buf, uint8_t osc); 
extern void render_pcm(float * buf, uint8_t osc);
extern void render_algo(float * buf, uint8_t osc) ;

extern float compute_mod_pulse(uint8_t osc);
extern float compute_mod_noise(uint8_t osc);
extern float compute_mod_sine(uint8_t osc);
extern float compute_mod_saw(uint8_t osc);
extern float compute_mod_triangle(uint8_t osc);
extern float compute_mod_pcm(uint8_t osc);

extern void ks_note_on(uint8_t osc); 
extern void ks_note_off(uint8_t osc);
extern void sine_note_on(uint8_t osc); 
extern void fm_sine_note_on(uint8_t osc); 
extern void saw_note_on(uint8_t osc); 
extern void triangle_note_on(uint8_t osc); 
extern void pulse_note_on(uint8_t osc); 
extern void pcm_note_on(uint8_t osc);
extern void algo_note_on(uint8_t osc);
extern void algo_note_off(uint8_t osc) ;
extern void sine_mod_trigger(uint8_t osc);
extern void saw_mod_trigger(uint8_t osc);
extern void triangle_mod_trigger(uint8_t osc);
extern void pulse_mod_trigger(uint8_t osc);
extern void pcm_mod_trigger(uint8_t osc);

// filters
extern void filters_init();
extern void filters_deinit();
extern void filter_process(float * block, uint8_t osc);
extern void parametric_eq_process(float *block);
extern void update_filter(uint8_t osc);

// envelopes
extern float compute_adsr_scale(uint8_t osc);
extern float compute_mod_scale(uint8_t osc);
extern void retrigger_mod_source(uint8_t osc);



#endif



