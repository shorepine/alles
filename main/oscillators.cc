#include "blip/Blip_Buffer.h"

extern "C" { 
    #include "alles.h"
    #include "sineLUT.h"
    #include "pcm.h"
}

static Blip_Buffer blipbuf;
// 10 voices * -32767 to 32767
static Blip_Synth<blip_good_quality,655350> blipsynth;


// TODO -- i could save a lot of heap by only mallocing this when needed 
#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ** ks_buffer; 


extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by LFOs & envelopes
extern struct state global; 



// Use the Blip_Buffer library to bandlimit signals
extern "C" void blip_the_buffer(float * ibuf, int16_t * obuf,  uint16_t len ) {
    // OK, now we've got a bunch of 16-bit floats all added up in ibuf
    // we want some non-linear curve scaling those #s into -32767 to +32767
    // blipbuf may do this for me
    blipsynth.volume(global.volume);
    for(uint16_t i=0;i<len;i++) {
        blipsynth.update(i, ibuf[i]);
    }
    blipbuf.end_frame(len);
    blipbuf.read_samples(obuf, len, 0);
}

extern "C" void pcm_note_on(uint8_t voice) {
    // If no freq given, we set it to default PCM SR. e.g. freq=11025 plays PCM at half speed, freq=44100 double speed 
    if(synth[voice].freq <= 0) synth[voice].freq = PCM_SAMPLE_RATE;
    // If patch is given, set step directly from patch's offset
    if(synth[voice].patch>=0) {
        synth[voice].step = offset_map[synth[voice].patch*2]; // start sample
        // Use substep here as "end sample" so we don't have to add another field to the struct
        synth[voice].substep = synth[voice].step + offset_map[synth[voice].patch*2 + 1]; // end sample
    } else { // no patch # given? use phase as index into PCM buffer
        synth[voice].step = PCM_LENGTH * synth[voice].phase; // start at phase offset
        synth[voice].substep = PCM_LENGTH; // play until end of buffer and stop 
    }
}

// TODO -- this just does one shot, no looping (will need extra loop parameter? what about sample metadata looping?) 
// TODO -- this should just be like render_LUT(float * buf, uint8_t voice, int16_t **LUT) as it's the same for sine & PCM?
extern "C" void render_pcm(float * buf, uint8_t voice) {
    float skip = msynth[voice].freq / (float)SAMPLE_RATE;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        float sample = pcm[(int)(synth[voice].step)];
        synth[voice].step = (synth[voice].step + skip);
        if(synth[voice].step >= synth[voice].substep ) { // end
            synth[voice].status=OFF;
            sample = 0;
        }
        buf[i] = buf[i] + (sample * msynth[voice].amp);
    }
}


extern "C" void sine_note_on(uint8_t voice) {
    synth[voice].step = (float)SINE_LUT_SIZE * synth[voice].phase;
}

extern "C" void render_sine(float * buf, uint8_t voice) { 
    float skip = msynth[voice].freq / 44100.0 * SINE_LUT_SIZE;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        uint16_t u0 = sine_LUT[(uint16_t)floor(synth[voice].step)];
        uint16_t u1 = sine_LUT[(uint16_t)(floor(synth[voice].step)+1 % SINE_LUT_SIZE)];
        float x0 = (float)u0 - 32768.0;
        float x1 = (float)u1 - 32768.0;
        float frac = synth[voice].step - floor(synth[voice].step);
        float sample = x0 + ((x1 - x0) * frac);
        buf[i] = buf[i] + (sample * msynth[voice].amp);
        synth[voice].step = synth[voice].step + skip;
        if(synth[voice].step >= SINE_LUT_SIZE) synth[voice].step = synth[voice].step - SINE_LUT_SIZE;
    }
}

extern "C" void render_noise(float *buf, uint8_t voice) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] + ( (int16_t) ((esp_random() >> 16) - 32768) * msynth[voice].amp);
    }
}

extern "C" void render_ks(float * buf, uint8_t voice) {
    if(msynth[voice].freq >= 55) { // lowest note we can play
        uint16_t buflen = floor(SAMPLE_RATE / msynth[voice].freq);
        for(uint16_t i=0;i<BLOCK_SIZE;i++) {
            uint16_t index = floor(synth[voice].step);
            synth[voice].sample = ks_buffer[voice][index];
            ks_buffer[voice][index] = (ks_buffer[voice][index] + ks_buffer[voice][(index + 1) % buflen]) * 0.5 * synth[voice].feedback;
            synth[voice].step = (index + 1) % buflen;
            buf[i] = buf[i] + synth[voice].sample * msynth[voice].amp;
        }
    }
}

extern "C" void saw_note_on(uint8_t voice) {
    float period = 1. / (synth[voice].freq/(float)SAMPLE_RATE);
    synth[voice].step = period * synth[voice].phase;
}
extern "C" void render_saw(float * buf, uint8_t voice) {
    float period = 1. / (msynth[voice].freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(synth[voice].step >= period || synth[voice].step == 0) {
            synth[voice].sample = DOWN;
            synth[voice].step = 0; // reset the period counter
        } else {
            synth[voice].sample = DOWN + (synth[voice].step * ((UP-DOWN) / period));
        }
        buf[i] = buf[i] + synth[voice].sample * msynth[voice].amp;
        synth[voice].step++;
    }
}

extern "C" void triangle_note_on(uint8_t voice) {
    float period = 1. / (synth[voice].freq/(float)SAMPLE_RATE);
    synth[voice].step = period * synth[voice].phase;
}
extern "C" void render_triangle(float * buf, uint8_t voice) {
    float period = 1. / (msynth[voice].freq/(float)SAMPLE_RATE);
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(synth[voice].step >= period || synth[voice].step == 0) {
            synth[voice].sample = DOWN;
            synth[voice].step = 0; // reset the period counter
        } else {
            if(synth[voice].step < (period/2.0)) {
                synth[voice].sample = DOWN + (synth[voice].step * ((UP-DOWN) / period * 2));
            } else {
                synth[voice].sample = UP - ((synth[voice].step-(period/2)) * ((UP-DOWN) / period * 2));
            }
        }
        buf[i] = buf[i] + synth[voice].sample * msynth[voice].amp;
        synth[voice].step++;
    }
}

extern "C" void pulse_note_on(uint8_t voice) {
    float period = 1. / (synth[voice].freq/(float)SAMPLE_RATE);
    synth[voice].step = period * synth[voice].phase;
}

extern "C" void render_pulse(float * buf, uint8_t voice) {
    if(msynth[voice].duty < 0.001 || msynth[voice].duty > 0.999) msynth[voice].duty = 0.5;
    float period = 1. / (msynth[voice].freq/(float)SAMPLE_RATE);
    float period2 = msynth[voice].duty * period; // if duty is 0.5, square wave
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        if(synth[voice].step >= period || synth[voice].step == 0)  {
            synth[voice].sample = UP;
            synth[voice].substep = 0; // start the duty cycle counter
            synth[voice].step = 0;
        } 
        if(synth[voice].sample == UP) {
            if(synth[voice].substep++ > period2) {
                synth[voice].sample = DOWN;
            }
        }
        buf[i] = buf[i] + synth[voice].sample * msynth[voice].amp;
        synth[voice].step++;
    }
}

extern "C" void ks_note_on(uint8_t voice) {
    if(msynth[voice].freq<=0) msynth[voice].freq = 1;
    uint16_t buflen = floor(SAMPLE_RATE / msynth[voice].freq);
    if(buflen > MAX_KS_BUFFER_LEN) buflen = MAX_KS_BUFFER_LEN;
    // init KS buffer with noise up to max
    for(uint16_t i=0;i<buflen;i++) {
        ks_buffer[voice][i] = ( (int16_t) ((esp_random() >> 16) - 32768) );
    }
}

extern "C" void ks_note_off(uint8_t voice) {
    msynth[voice].amp = 0;
}


extern "C" void oscillators_init(void) {
    // 6ms buffer
    if ( blipbuf.set_sample_rate( SAMPLE_RATE ) )
        exit( EXIT_FAILURE );
    blipbuf.clock_rate( blipbuf.sample_rate() );
    blipbuf.bass_freq( 0 ); // makes waveforms perfectly flat
    blipsynth.volume(global.volume);
    blipsynth.output(&blipbuf);
    // TODO -- i could save a lot of heap by only mallocing this when needed 
    ks_buffer = (float**) malloc(sizeof(float*)*VOICES);
    for(int i=0;i<VOICES;i++) ks_buffer[i] = (float*)malloc(sizeof(float)*MAX_KS_BUFFER_LEN); 
}

extern "C" void oscillators_deinit(void) {
    for(int i=0;i<VOICES;i++) free(ks_buffer[i]);
    free(ks_buffer);
}





