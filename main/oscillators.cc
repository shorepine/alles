#include "blip/Blip_Buffer.h"

extern "C" { 
    #include "alles.h"
    #include "sineLUT.h"
}

static Blip_Buffer blipbuf;
static Blip_Synth<blip_good_quality,65535> synth;


int16_t intbuf[BLOCK_SIZE];
#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ks_buffer[VOICES][MAX_KS_BUFFER_LEN]; 


extern struct event sequencer[VOICES];

// Use the Blip_Buffer library to bandlimit signals
extern "C" void blip_the_buffer(float * ibuf, int16_t * obuf,  uint16_t len ) {
    for(uint16_t i=0;i<len;i++) {
        // Clip first
        if(ibuf[i]>32767) ibuf[i]=32767;
        if(ibuf[i]<-32768) ibuf[i]=-32768;
        synth.update(i, ibuf[i]);
    }
    blipbuf.end_frame(len);
    blipbuf.read_samples(obuf, len, 0);
}

//extern "C" void render_sine(float * buf, uint16_t len, uint8_t voice, float freq, float amp) {
extern "C" void render_sine(float * buf, uint8_t voice) { //uint16_t len, uint8_t voice, float freq, float amp) {
    float skip = sequencer[voice].freq / 44100.0 * SINE_LUT_SIZE;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(skip >= 1) { // skip compute if frequency is < 3Hz
            uint16_t u0 = sine_LUT[(uint16_t)floor(sequencer[voice].step)];
            uint16_t u1 = sine_LUT[(uint16_t)(floor(sequencer[voice].step)+1 % SINE_LUT_SIZE)];
            float x0 = (float)u0 - 32768.0;
            float x1 = (float)u1 - 32768.0;
            float frac = sequencer[voice].step - floor(sequencer[voice].step);
            float sample = x0 + ((x1 - x0) * frac);
            buf[i] = buf[i] + (sample * sequencer[voice].amp);
            sequencer[voice].step = sequencer[voice].step + skip;
            if(sequencer[voice].step >= SINE_LUT_SIZE) sequencer[voice].step = sequencer[voice].step - SINE_LUT_SIZE;
        }
    }
}

//extern "C" void render_noise(float *buf, uint16_t len, float amp) {
extern "C" void render_noise(float *buf, uint8_t voice) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] + ( (int16_t) ((esp_random() >> 16) - 32768) * sequencer[voice].amp);
    }
}

//extern "C" void render_ks(float * buf, uint16_t len, uint8_t voice, float freq, float feedback, float amp) {
extern "C" void render_ks(float * buf, uint8_t voice) {
    if(sequencer[voice].freq >= 55) { // lowest note we can play
        uint16_t buflen = floor(SAMPLE_RATE / sequencer[voice].freq);
        for(uint16_t i=0;i<BLOCK_SIZE;i++) {
            uint16_t index = floor(sequencer[voice].step);
            sequencer[voice].sample = ks_buffer[voice][index];
            ks_buffer[voice][index] = (ks_buffer[voice][index] + ks_buffer[voice][(index + 1) % buflen]) * 0.5 * sequencer[voice].feedback;
            sequencer[voice].step = (index + 1) % buflen;
            buf[i] = buf[i] + sequencer[voice].sample * sequencer[voice].amp;
        }
    }
}

//extern "C" void render_saw(float * buf, uint16_t len, uint8_t voice, float freq, float amp) {
extern "C" void render_saw(float * buf, uint8_t voice) {
    float period = 1. / (sequencer[voice].freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(sequencer[voice].step >= period || sequencer[voice].step == 0) {
            sequencer[voice].sample = DOWN;
            sequencer[voice].step = 0; // reset the period counter
        } else {
            sequencer[voice].sample = DOWN + (sequencer[voice].step * ((UP-DOWN) / period));
        }
        buf[i] = buf[i] + sequencer[voice].sample * sequencer[voice].amp;
        sequencer[voice].step++;
    }
}

//extern "C" void render_triangle(float * buf, uint16_t len, uint8_t voice, float freq, float amp) {
extern "C" void render_triangle(float * buf, uint8_t voice) {
    float period = 1. / (sequencer[voice].freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(sequencer[voice].step >= period || sequencer[voice].step == 0) {
            sequencer[voice].sample = DOWN;
            sequencer[voice].step = 0; // reset the period counter
        } else {
            if(sequencer[voice].step < (period/2.0)) {
                sequencer[voice].sample = DOWN + (sequencer[voice].step * ((UP-DOWN) / period * 2));
            } else {
                sequencer[voice].sample = UP - ((sequencer[voice].step-(period/2)) * ((UP-DOWN) / period * 2));
            }
        }
        buf[i] = buf[i] + sequencer[voice].sample * sequencer[voice].amp;
        sequencer[voice].step++;
    }
}


//extern "C" void render_pulse(float * buf, uint16_t len, uint8_t voice, float freq, float duty, float amp) {
extern "C" void render_pulse(float * buf, uint8_t voice) {
    if(sequencer[voice].duty < 0.001 || sequencer[voice].duty > 0.999) sequencer[voice].duty = 0.5;
    float period = 1. / (sequencer[voice].freq/(float)SAMPLE_RATE);
    float period2 = sequencer[voice].duty * period; // if duty is 0.5, square wave
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(sequencer[voice].step >= period || sequencer[voice].step == 0)  {
            sequencer[voice].sample = UP;
            sequencer[voice].substep = 0; // start the duty cycle counter
            sequencer[voice].step = 0;
        } 
        if(sequencer[voice].sample == UP) {
            if(sequencer[voice].substep++ > period2) {
                sequencer[voice].sample = DOWN;
            }
        }
        buf[i] = buf[i] + sequencer[voice].sample * sequencer[voice].amp;
        sequencer[voice].step++;
    }
}

//extern "C" void ks_new_note_freq(float freq, uint8_t voice) {
extern "C" void ks_new_note_freq(uint8_t voice) {
    if(sequencer[voice].freq<=0) sequencer[voice].freq = 1;
    uint16_t buflen = floor(SAMPLE_RATE / sequencer[voice].freq);
    if(buflen > MAX_KS_BUFFER_LEN) buflen = MAX_KS_BUFFER_LEN;
    // init KS buffer with noise up to max
    for(uint16_t i=0;i<buflen;i++) {
        ks_buffer[voice][i] = ( (int16_t) ((esp_random() >> 16) - 32768) );
    }
}

extern "C" void oscillators_init(void) {
    // 6ms buffer
    if ( blipbuf.set_sample_rate( SAMPLE_RATE ) )
        exit( EXIT_FAILURE );
    blipbuf.clock_rate( blipbuf.sample_rate() );
    blipbuf.bass_freq( 0 ); // makes waveforms perfectly flat
    synth.volume(1);
    synth.output(&blipbuf);
}




