extern "C" { 
    #include "alles.h"
    #include "sinLUT_1024.h"
    #include "impulse32_1024.h"
    #include "pcm.h"
}

// TODO -- i could save a lot of heap by only mallocing this when needed 
#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ** ks_buffer; 


extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by LFOs & envelopes
extern struct state global; 


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


// This is copying from Pure Data's tabread4~.
extern "C" float render_lut(float * buf, float step, float skip, float amp, const int16_t* lut, int16_t lut_size) { 
    // We assume lut_size == 2^R for some R, so (lut_size - 1) consists of R '1's in binary.
    int lut_mask = lut_size - 1;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
      uint16_t base_index = (uint16_t)floor(step);
      float frac = step - (float)base_index;
      float a = (float)lut[(base_index - 1) & lut_mask];
      float b = (float)lut[(base_index + 0) & lut_mask];
      float c = (float)lut[(base_index + 1) & lut_mask];
      float d = (float)lut[(base_index + 2) & lut_mask];
      // linear interpolation.
      //float sample = b + ((c - b) * frac);
      // cubic interpolation (TTEM p.46).
      //      float sample = 
      //	- frac * (frac - 1) * (frac - 2) / 6.0 * a
      //	+ (frac + 1) * (frac - 1) * (frac - 2) / 2.0 * b
      //	- (frac + 1) * frac * (frac - 2) / 2.0 * c
      //	+ (frac + 1) * frac * (frac - 1) / 6.0 * d;
      // Miller's optimization - https://github.com/pure-data/pure-data/blob/master/src/d_array.c#L440
      float cminusb = c - b;
      float sample = b + frac * (cminusb - 0.1666667f * (1.-frac) * ((d - a - 3.0f * cminusb) * frac + (d + 2.0f*a - 3.0f*b)));
      buf[i] += sample * amp;
      step += skip;
      if(step >= lut_size) step -= lut_size;
    }
    return step;
}

extern "C" void pulse_note_on(uint8_t voice) {
    // So i reset step to some phase math, right? yeah
    synth[voice].step = (float)IMPULSE32_SIZE * synth[voice].phase;
}

extern "C" void lpf_buf(float *buf, float scale) {
  // Implement first-order low-pass (leaky integrator).
  static float lastbuf = 0;
  for (uint16_t i = 0; i < BLOCK_SIZE; ++i) {
    buf[i] *= scale;
    buf[i] += 0.99 * (lastbuf - buf[i]);
    lastbuf = buf[i];
  }
}

extern "C" void render_pulse(float * buf, uint8_t voice) {
    float duty = msynth[voice].duty;
    if (duty < 0.01) duty = 0.01;
    if (duty > 0.99) duty = 0.99;
    float skip = msynth[voice].freq / 44100.0 * IMPULSE32_SIZE;
    float pwm_step = synth[voice].step + msynth[voice].duty * IMPULSE32_SIZE;
    if (pwm_step >= IMPULSE32_SIZE)  pwm_step -= IMPULSE32_SIZE;
    synth[voice].step = render_lut(buf, synth[voice].step, skip, msynth[voice].amp, impulse32, IMPULSE32_SIZE);
    render_lut(buf, pwm_step, skip, -msynth[voice].amp, impulse32, IMPULSE32_SIZE);
    lpf_buf(buf, 400);
}

extern "C" void sine_note_on(uint8_t voice) {
    // So i reset step to some phase math, right? yeah
    synth[voice].step = (float)SINLUT_SIZE * synth[voice].phase;
}

extern "C" void render_sine(float * buf, uint8_t voice) { 
    float skip = msynth[voice].freq / 44100.0 * SINLUT_SIZE;
    synth[voice].step = render_lut(buf, synth[voice].step, skip, msynth[voice].amp, sinLUT, SINLUT_SIZE);
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
    // TODO -- i could save a lot of heap by only mallocing this when needed 
    ks_buffer = (float**) malloc(sizeof(float*)*VOICES);
    for(int i=0;i<VOICES;i++) ks_buffer[i] = (float*)malloc(sizeof(float)*MAX_KS_BUFFER_LEN); 
}

extern "C" void oscillators_deinit(void) {
    for(int i=0;i<VOICES;i++) free(ks_buffer[i]);
    free(ks_buffer);
}





