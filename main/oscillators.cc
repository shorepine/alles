#include "blip/Blip_Buffer.h"

extern "C" { 
    #include "alles.h"
    #include "sineLUT.h"
}

static Blip_Buffer blipbuf;
static Blip_Synth<blip_good_quality,65535> synth;

float step[VOICES];
float substep[VOICES];
float sample[VOICES];
float UP = 16383; 
float DOWN = -16384;
int16_t intbuf[BLOCK_SIZE];
#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ks_buffer[VOICES][MAX_KS_BUFFER_LEN]; 


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

extern "C" void render_sine(float * buf, uint16_t len, uint8_t voice, float freq, float amp) {
    float skip = freq / 44100.0 * SINE_LUT_SIZE;
    for(uint16_t i=0;i<len;i++) {
        if(skip >= 1) { // skip compute if frequency is < 3Hz
            uint16_t u0 = sine_LUT[(uint16_t)floor(step[voice])];
            uint16_t u1 = sine_LUT[(uint16_t)(floor(step[voice])+1 % SINE_LUT_SIZE)];
            float x0 = (float)u0 - 32768.0;
            float x1 = (float)u1 - 32768.0;
            float frac = step[voice] - floor(step[voice]);
            float sample = x0 + ((x1 - x0) * frac);
            buf[i] = buf[i] + (sample * amp);
            step[voice] = step[voice] + skip;
            if(step[voice] >= SINE_LUT_SIZE) step[voice] = step[voice] - SINE_LUT_SIZE;
        }
    }
}

extern "C" void render_noise(float *buf, uint16_t len, float amp) {
    for(uint16_t i=0;i<len;i++) {
        buf[i] = buf[i] + ( (int16_t) ((esp_random() >> 16) - 32768) * amp);
    }
}

extern "C" void render_ks(float * buf, uint16_t len, uint8_t voice, float freq, float feedback, float amp) {
    if(freq >= 55) { // lowest note we can play
        uint16_t buflen = floor(SAMPLE_RATE / freq);
        for(uint16_t i=0;i<len;i++) {
            uint16_t index = floor(step[voice]);
            sample[voice] = ks_buffer[voice][index];
            ks_buffer[voice][index] = (ks_buffer[voice][index] + ks_buffer[voice][(index + 1) % buflen]) * 0.5 * feedback;
            step[voice] = (index + 1) % buflen;
            buf[i] = buf[i] + sample[voice] * amp;
        }
    }
}

extern "C" void render_saw(float * buf, uint16_t len, uint8_t voice, float freq, float amp) {
    float period = 1. / (freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<len;i++) {
        if(step[voice] >= period || step[voice] == 0) {
            sample[voice] = DOWN;
            step[voice] = 0; // reset the period counter
        } else {
            sample[voice] = DOWN + (step[voice] * ((UP-DOWN) / period));
        }
        buf[i] = buf[i] + sample[voice] * amp;
        step[voice]++;
    }
}

extern "C" void render_triangle(float * buf, uint16_t len, uint8_t voice, float freq, float amp) {
    float period = 1. / (freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<len;i++) {
        if(step[voice] >= period || step[voice] == 0) {
            sample[voice] = DOWN;
            step[voice] = 0; // reset the period counter
        } else {
            if(step[voice] < (period/2.0)) {
                sample[voice] = DOWN + (step[voice] * ((UP-DOWN) / period * 2));
            } else {
                sample[voice] = UP - ((step[voice]-(period/2)) * ((UP-DOWN) / period * 2));
            }
        }
        buf[i] = buf[i] + sample[voice] * amp;
        step[voice]++;
    }
}


extern "C" void render_pulse(float * buf, uint16_t len, uint8_t voice, float freq, float duty, float amp) {
    if(duty < 0.001 || duty > 0.999) duty = 0.5;
    float period = 1. / (freq/(float)SAMPLE_RATE);
    float period2 = duty * period; // if duty is 0.5, square wave
    for(uint16_t i=0;i<len;i++) {
        if(step[voice] >= period || step[voice] == 0)  {
            sample[voice] = UP;
            substep[voice] = 0; // start the duty cycle counter
            step[voice] = 0;
        } 
        if(sample[voice] == UP) {
            if(substep[voice]++ > period2) {
                sample[voice] = DOWN;
            }
        }
        buf[i] = buf[i] + sample[voice] * amp;
        step[voice]++;
    }
}

extern "C" void ks_new_note_freq(float freq, uint8_t voice) {
    if(freq<=0) freq = 1;
    uint16_t buflen = floor(SAMPLE_RATE / freq);
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
    for(uint8_t voice=0;voice<VOICES;voice++) { 
        sample[voice] = DOWN; step[voice] = 0; substep[voice] =0; 
    }

}




