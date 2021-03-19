#include "blip/Blip_Buffer.h"

extern "C" { 
    #include "alles.h"
    #include "sineLUT.h"
}

static Blip_Buffer blipbuf;
// 10 voices * -32767 to 32767
static Blip_Synth<blip_good_quality,655350> synth;


#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ** ks_buffer; 


extern struct event *seq;
extern float volume; // grab the volume for this synth

// Use the Blip_Buffer library to bandlimit signals
extern "C" void blip_the_buffer(float * ibuf, int16_t * obuf,  uint16_t len ) {
    // OK, now we've got a bunch of 16-bit floats all added up in ibuf
    // we want some non-linear curve scaling those #s into -32767 to +32767
    // blipbuf may do this for me
    synth.volume(volume);
    for(uint16_t i=0;i<len;i++) {
        synth.update(i, ibuf[i]);
    }
    blipbuf.end_frame(len);
    blipbuf.read_samples(obuf, len, 0);
}

extern "C" void render_sine(float * buf, uint8_t voice) { 
    float skip = seq[voice].freq / 44100.0 * SINE_LUT_SIZE;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(skip >= 1) { // skip compute if frequency is < 3Hz
            uint16_t u0 = sine_LUT[(uint16_t)floor(seq[voice].step)];
            uint16_t u1 = sine_LUT[(uint16_t)(floor(seq[voice].step)+1 % SINE_LUT_SIZE)];
            float x0 = (float)u0 - 32768.0;
            float x1 = (float)u1 - 32768.0;
            float frac = seq[voice].step - floor(seq[voice].step);
            float sample = x0 + ((x1 - x0) * frac);
            buf[i] = buf[i] + (sample * seq[voice].amp);
            seq[voice].step = seq[voice].step + skip;
            if(seq[voice].step >= SINE_LUT_SIZE) seq[voice].step = seq[voice].step - SINE_LUT_SIZE;
        }
    }
}

extern "C" void render_noise(float *buf, uint8_t voice) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] + ( (int16_t) ((esp_random() >> 16) - 32768) * seq[voice].amp);
    }
}

extern "C" void render_ks(float * buf, uint8_t voice) {
    if(seq[voice].freq >= 55) { // lowest note we can play
        uint16_t buflen = floor(SAMPLE_RATE / seq[voice].freq);
        for(uint16_t i=0;i<BLOCK_SIZE;i++) {
            uint16_t index = floor(seq[voice].step);
            seq[voice].sample = ks_buffer[voice][index];
            ks_buffer[voice][index] = (ks_buffer[voice][index] + ks_buffer[voice][(index + 1) % buflen]) * 0.5 * seq[voice].feedback;
            seq[voice].step = (index + 1) % buflen;
            buf[i] = buf[i] + seq[voice].sample * seq[voice].amp;
        }
    }
}

extern "C" void render_saw(float * buf, uint8_t voice) {
    float period = 1. / (seq[voice].freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(seq[voice].step >= period || seq[voice].step == 0) {
            seq[voice].sample = DOWN;
            seq[voice].step = 0; // reset the period counter
        } else {
            seq[voice].sample = DOWN + (seq[voice].step * ((UP-DOWN) / period));
        }
        buf[i] = buf[i] + seq[voice].sample * seq[voice].amp;
        seq[voice].step++;
    }
}

extern "C" void render_triangle(float * buf, uint8_t voice) {
    float period = 1. / (seq[voice].freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(seq[voice].step >= period || seq[voice].step == 0) {
            seq[voice].sample = DOWN;
            seq[voice].step = 0; // reset the period counter
        } else {
            if(seq[voice].step < (period/2.0)) {
                seq[voice].sample = DOWN + (seq[voice].step * ((UP-DOWN) / period * 2));
            } else {
                seq[voice].sample = UP - ((seq[voice].step-(period/2)) * ((UP-DOWN) / period * 2));
            }
        }
        buf[i] = buf[i] + seq[voice].sample * seq[voice].amp;
        seq[voice].step++;
    }
}


extern "C" void render_pulse(float * buf, uint8_t voice) {
    if(seq[voice].duty < 0.001 || seq[voice].duty > 0.999) seq[voice].duty = 0.5;
    float period = 1. / (seq[voice].freq/(float)SAMPLE_RATE);
    float period2 = seq[voice].duty * period; // if duty is 0.5, square wave
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(seq[voice].step >= period || seq[voice].step == 0)  {
            seq[voice].sample = UP;
            seq[voice].substep = 0; // start the duty cycle counter
            seq[voice].step = 0;
        } 
        if(seq[voice].sample == UP) {
            if(seq[voice].substep++ > period2) {
                seq[voice].sample = DOWN;
            }
        }
        buf[i] = buf[i] + seq[voice].sample * seq[voice].amp;
        seq[voice].step++;
    }
}

extern "C" void ks_new_note_freq(uint8_t voice) {
    if(seq[voice].freq<=0) seq[voice].freq = 1;
    uint16_t buflen = floor(SAMPLE_RATE / seq[voice].freq);
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
    synth.volume(volume);
    synth.output(&blipbuf);
    ks_buffer = (float**) malloc(sizeof(float*)*VOICES);
    for(int i=0;i<VOICES;i++) ks_buffer[i] = (float*)malloc(sizeof(float)*MAX_KS_BUFFER_LEN); 
}

extern "C" void oscillators_deinit(void) {
    for(int i=0;i<VOICES;i++) free(ks_buffer[i]);
    free(ks_buffer);
}





