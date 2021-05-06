#include "alles.h"
#include "sinLUT_1024.h"
#include "impulse32_1024.h"
#include "pcm.h"

// TODO -- alloc these when needed or figure out a different way, or remove KS 
#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ** ks_buffer; 


extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by LFOs & envelopes
extern struct state global; 
extern float *scratchbuf;



/* Dan Ellis libblosca functions */

// This is copying from Pure Data's tabread4~.
float render_lut(float * buf, float step, float skip, float amp, const int16_t* lut, int16_t lut_size) { 
    // We assume lut_size == 2^R for some R, so (lut_size - 1) consists of R '1's in binary.
    int lut_mask = lut_size - 1;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        uint16_t base_index = (uint16_t)floor(step);
        float frac = step - (float)base_index;
        float b = (float)lut[(base_index + 0) & lut_mask];
        float c = (float)lut[(base_index + 1) & lut_mask];
#ifdef LINEAR_INTERP
        // linear interpolation.
        float sample = b + ((c - b) * frac);
#else /* !LINEAR_INTERP => CUBIC_INTERP */
        float a = (float)lut[(base_index - 1) & lut_mask];
        float d = (float)lut[(base_index + 2) & lut_mask];
        // cubic interpolation (TTEM p.46).
        //      float sample = 
        //    - frac * (frac - 1) * (frac - 2) / 6.0 * a
        //    + (frac + 1) * (frac - 1) * (frac - 2) / 2.0 * b
        //    - (frac + 1) * frac * (frac - 2) / 2.0 * c
        //    + (frac + 1) * frac * (frac - 1) / 6.0 * d;
        // Miller's optimization - https://github.com/pure-data/pure-data/blob/master/src/d_array.c#L440
        float cminusb = c - b;
        float sample = b + frac * (cminusb - 0.1666667f * (1.-frac) * ((d - a - 3.0f * cminusb) * frac + (d + 2.0f*a - 3.0f*b)));
#endif /* LINEAR_INTERP */
        buf[i] += sample * amp;
        step += skip;
        if(step >= lut_size) step -= lut_size;
    }
    return step;
}

void lpf_buf(float *buf, float decay, float *state) {
    // Implement first-order low-pass (leaky integrator).
    float s = *state;
    for (uint16_t i = 0; i < BLOCK_SIZE; ++i) {
      buf[i] = decay * s + buf[i];
      *state = buf[i];
    }
}

void clear_buf(float *buf) {
    // Clear a block-sized buf.
    memset(buf, 0, BLOCK_SIZE * sizeof(float));
}

void cumulate_buf(const float *from, float *dest) {
    for (uint16_t i = 0; i < BLOCK_SIZE; ++i) {
        dest[i] += from[i];
    }
}

/* PCM */

void pcm_note_on(uint8_t oscillator) {
    // If no freq given, we set it to default PCM SR. e.g. freq=11025 plays PCM at half speed, freq=44100 double speed 
    if(synth[oscillator].freq <= 0) synth[oscillator].freq = PCM_SAMPLE_RATE;
    // If patch is given, set step directly from patch's offset
    if(synth[oscillator].patch>=0) {
        synth[oscillator].step = offset_map[synth[oscillator].patch*2]; // start sample
        // Use substep here as "end sample" so we don't have to add another field to the struct
        synth[oscillator].substep = synth[oscillator].step + offset_map[synth[oscillator].patch*2 + 1]; // end sample
    } else { // no patch # given? use phase as index into PCM buffer
        synth[oscillator].step = PCM_LENGTH * synth[oscillator].phase; // start at phase offset
        synth[oscillator].substep = PCM_LENGTH; // play until end of buffer and stop 
    }
}
void pcm_lfo_trigger(uint8_t oscillator) {
    pcm_note_on(oscillator);
}

// TODO -- this just does one shot, no looping (will need extra loop parameter? what about sample metadata looping?) 
// TODO -- this should just be like render_LUT(float * buf, uint8_t oscillator, int16_t **LUT) as it's the same for sine & PCM?
void render_pcm(float * buf, uint8_t oscillator) {
    float skip = msynth[oscillator].freq / (float)SAMPLE_RATE;
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        float sample = pcm[(int)(synth[oscillator].step)];
        synth[oscillator].step = (synth[oscillator].step + skip);
        if(synth[oscillator].step >= synth[oscillator].substep ) { // end
            synth[oscillator].status=OFF;
            sample = 0;
        }
        buf[i] = buf[i] + (sample * msynth[oscillator].amp);
    }
}

float compute_lfo_pcm(uint8_t oscillator) {
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float skip = msynth[oscillator].freq / lfo_sr;
    float sample = pcm[(int)(synth[oscillator].step)];
    synth[oscillator].step = (synth[oscillator].step + skip);
    if(synth[oscillator].step >= synth[oscillator].substep ) { // end
        synth[oscillator].status=OFF;
        sample = 0;
    }
    return (sample * msynth[oscillator].amp) / 16384.0; // -1 .. 1
    
}

/* Pulse wave */

void pulse_note_on(uint8_t oscillator) {
    // So i reset step to some phase math, right? yeah
    synth[oscillator].step = (float)IMPULSE32_SIZE * synth[oscillator].phase;
    synth[oscillator].lpf_state[0] = 0;
}

void render_pulse(float * buf, uint8_t oscillator) {
    // LPF time constant should be ~ 10x oscillator period, so droop is minimal.
    float period_samples = (float)SAMPLE_RATE / msynth[oscillator].freq;
    synth[oscillator].lpf_alpha = 1.0 - 1.0 / (10.0 * period_samples);
    float duty = msynth[oscillator].duty;
    if (duty < 0.01) duty = 0.01;
    if (duty > 0.99) duty = 0.99;
    float skip = msynth[oscillator].freq / (float)SAMPLE_RATE * IMPULSE32_SIZE;
    // Scale the impulse proportional to the skip so its integral remains ~constant.
    // 0.5 is to compensate for typical skip of ~2.
    float amp = msynth[oscillator].amp * skip * 0.1;  // was 0.5
    float pwm_step = synth[oscillator].step + duty * IMPULSE32_SIZE;
    if (pwm_step >= IMPULSE32_SIZE)  pwm_step -= IMPULSE32_SIZE;
    clear_buf(scratchbuf);
    synth[oscillator].step = render_lut(scratchbuf, synth[oscillator].step, skip, amp, impulse32, IMPULSE32_SIZE);
    render_lut(scratchbuf, pwm_step, skip, -amp, impulse32, IMPULSE32_SIZE);
    lpf_buf(scratchbuf, synth[oscillator].lpf_alpha, &synth[oscillator].lpf_state[0]);
    // Accumulate into actual output buffer.
    cumulate_buf(scratchbuf, buf);
}

void pulse_lfo_trigger(uint8_t oscillator) {
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float period = 1. / (synth[oscillator].freq/lfo_sr);
    synth[oscillator].step = period * synth[oscillator].phase;
}

float compute_lfo_pulse(uint8_t oscillator) {
    // do BW pulse gen at SR=44100/64
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    if(msynth[oscillator].duty < 0.001 || msynth[oscillator].duty > 0.999) msynth[oscillator].duty = 0.5;
    float period = 1. / (msynth[oscillator].freq/(float)lfo_sr);
    float period2 = msynth[oscillator].duty * period; // if duty is 0.5, square wave
    if(synth[oscillator].step >= period || synth[oscillator].step == 0)  {
        synth[oscillator].sample = UP;
        synth[oscillator].substep = 0; // start the duty cycle counter
        synth[oscillator].step = 0;
    } 
    if(synth[oscillator].sample == UP) {
        if(synth[oscillator].substep++ > period2) {
            synth[oscillator].sample = DOWN;
        }
    }
    synth[oscillator].step++;
    return (synth[oscillator].sample * msynth[oscillator].amp)/16384.0; // -1 .. 1
}


/* Saw wave */

void saw_note_on(uint8_t oscillator) {
    synth[oscillator].step = (float)IMPULSE32_SIZE * synth[oscillator].phase;
    synth[oscillator].lpf_state[0] = 0;
}

void render_saw(float * buf, uint8_t oscillator) {
    // For saw, we *want* the lpf to droop across the period, so use a smaller alpha.
    float period_samples = (float)SAMPLE_RATE / msynth[oscillator].freq;
    synth[oscillator].lpf_alpha = 1.0 - 1.0 / period_samples;
    float skip = msynth[oscillator].freq / (float)SAMPLE_RATE * IMPULSE32_SIZE;
    // Scale the impulse proportional to the skip so its integral remains ~constant.
    // 0.5 is to compensate for typical skip of ~2.
    float amp = msynth[oscillator].amp * skip * 0.1;
    clear_buf(scratchbuf);
    synth[oscillator].step = render_lut(
          scratchbuf, synth[oscillator].step, skip, amp, impulse32, IMPULSE32_SIZE);
    lpf_buf(scratchbuf, synth[oscillator].lpf_alpha, &synth[oscillator].lpf_state[0]);
    cumulate_buf(scratchbuf, buf);
}



void saw_lfo_trigger(uint8_t oscillator) {
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float period = 1. / (synth[oscillator].freq/lfo_sr);
    synth[oscillator].step = period * synth[oscillator].phase;
}

float compute_lfo_saw(uint8_t oscillator) {
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float period = 1. / (msynth[oscillator].freq/lfo_sr);
    if(synth[oscillator].step >= period || synth[oscillator].step == 0) {
        synth[oscillator].sample = DOWN;
        synth[oscillator].step = 0; // reset the period counter
    } else {
        synth[oscillator].sample = DOWN + (synth[oscillator].step * ((UP-DOWN) / period));
    }
    synth[oscillator].step++;
    return (synth[oscillator].sample * msynth[oscillator].amp)/16384.0; // -1 .. 1    
}


/* triangle wave */

void triangle_note_on(uint8_t oscillator) {
    synth[oscillator].step = (float)IMPULSE32_SIZE * synth[oscillator].phase;
    synth[oscillator].lpf_state[0] = 0;
    synth[oscillator].lpf_state[1] = 0;
}

void render_triangle(float * buf, uint8_t oscillator) {
    // Triangle has two lpfs, one to build the square, and one to integrate the pulse.
    float period_samples = (float)SAMPLE_RATE / msynth[oscillator].freq;
    synth[oscillator].lpf_alpha = 1.0 - 1.0 / (10 * period_samples);
    synth[oscillator].lpf_alpha_1 = 1.0 - 1.0 / period_samples;
    float duty = msynth[oscillator].duty;
    if (duty < 0.01) duty = 0.01;
    if (duty > 0.99) duty = 0.99;
    float skip = msynth[oscillator].freq / (float)SAMPLE_RATE * IMPULSE32_SIZE;
    // Scale impulses by skip to make integral independent of skip.
    // Scale by skip again so integral of each cycle of square wave is same total amp.
    // Divide by duty(1-duty) so peak integral is const regardless of duty.
    float amp = msynth[oscillator].amp * skip * skip / (duty - duty * duty) * 0.0001;
    float pwm_step = synth[oscillator].step + duty * IMPULSE32_SIZE;
    if (pwm_step >= IMPULSE32_SIZE)  pwm_step -= IMPULSE32_SIZE;
    clear_buf(scratchbuf);
    synth[oscillator].step = render_lut(scratchbuf, synth[oscillator].step, skip, amp, impulse32, IMPULSE32_SIZE);
    render_lut(scratchbuf, pwm_step, skip, -amp, impulse32, IMPULSE32_SIZE);
    // Integrate once to get square wave.
    lpf_buf(scratchbuf, synth[oscillator].lpf_alpha, &synth[oscillator].lpf_state[0]);
    // Integrate again to get triangle wave.
    lpf_buf(scratchbuf, synth[oscillator].lpf_alpha_1, &synth[oscillator].lpf_state[1]);
    cumulate_buf(scratchbuf, buf);
}


void triangle_lfo_trigger(uint8_t oscillator) {
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float period = 1. / (synth[oscillator].freq/lfo_sr);
    synth[oscillator].step = period * synth[oscillator].phase;
}

float compute_lfo_triangle(uint8_t oscillator) {
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;    
    float period = 1. / (msynth[oscillator].freq/lfo_sr);
    if(synth[oscillator].step >= period || synth[oscillator].step == 0) {
        synth[oscillator].sample = DOWN;
        synth[oscillator].step = 0; // reset the period counter
    } else {
        if(synth[oscillator].step < (period/2.0)) {
            synth[oscillator].sample = DOWN + (synth[oscillator].step * ((UP-DOWN) / period * 2));
        } else {
            synth[oscillator].sample = UP - ((synth[oscillator].step-(period/2)) * ((UP-DOWN) / period * 2));
        }
    }
    synth[oscillator].step++;
    return (synth[oscillator].sample * msynth[oscillator].amp) / 16384.0; // -1 .. 1
    
}

/* sine */

void sine_note_on(uint8_t oscillator) {
    synth[oscillator].step = (float)SINLUT_SIZE * synth[oscillator].phase;
}

void render_sine(float * buf, uint8_t oscillator) { 
    float skip = msynth[oscillator].freq / (float)SAMPLE_RATE * SINLUT_SIZE;
    synth[oscillator].step = render_lut(buf, synth[oscillator].step, skip, msynth[oscillator].amp, sinLUT, SINLUT_SIZE);
}

float compute_lfo_sine(uint8_t oscillator) { 
    float lfo_sr = (float)SAMPLE_RATE / (float)BLOCK_SIZE;
    float skip = msynth[oscillator].freq / lfo_sr * SINLUT_SIZE;

    int lut_mask = SINLUT_SIZE - 1;
    uint16_t base_index = (uint16_t)floor(synth[oscillator].step);
    float frac = synth[oscillator].step - (float)base_index;
    float b = (float)sinLUT[(base_index + 0) & lut_mask];
    float c = (float)sinLUT[(base_index + 1) & lut_mask];
    // linear interpolation for the LFO 
    float sample = b + ((c - b) * frac);
    synth[oscillator].step += skip;
    if(synth[oscillator].step >= SINLUT_SIZE) synth[oscillator].step -= SINLUT_SIZE;
    return (sample * msynth[oscillator].amp)/16384.0; // -1 .. 1
}


void sine_lfo_trigger(uint8_t oscillator) {
    sine_note_on(oscillator);
}

/* noise */

void render_noise(float *buf, uint8_t oscillator) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] + ( (int16_t) ((esp_random() >> 16) - 32768) * msynth[oscillator].amp);
    }
}

float compute_lfo_noise(uint8_t oscillator) {
    return ( (int16_t) ((esp_random() >> 16) - 32768) * msynth[oscillator].amp) / 16384.0; // -1..1
}

/* karplus-strong */

void render_ks(float * buf, uint8_t oscillator) {
    if(msynth[oscillator].freq >= 55) { // lowest note we can play
        uint16_t buflen = floor(SAMPLE_RATE / msynth[oscillator].freq);
        for(uint16_t i=0;i<BLOCK_SIZE;i++) {
            uint16_t index = floor(synth[oscillator].step);
            synth[oscillator].sample = ks_buffer[oscillator][index];
            ks_buffer[oscillator][index] = (ks_buffer[oscillator][index] + ks_buffer[oscillator][(index + 1) % buflen]) * 0.5 * synth[oscillator].feedback;
            synth[oscillator].step = (index + 1) % buflen;
            buf[i] = buf[i] + synth[oscillator].sample * msynth[oscillator].amp;
        }
    }
}

void ks_note_on(uint8_t oscillator) {
    if(msynth[oscillator].freq<=0) msynth[oscillator].freq = 1;
    uint16_t buflen = floor(SAMPLE_RATE / msynth[oscillator].freq);
    if(buflen > MAX_KS_BUFFER_LEN) buflen = MAX_KS_BUFFER_LEN;
    // init KS buffer with noise up to max
    for(uint16_t i=0;i<buflen;i++) {
        ks_buffer[oscillator][i] = ( (int16_t) ((esp_random() >> 16) - 32768) );
    }
}

void ks_note_off(uint8_t oscillator) {
    msynth[oscillator].amp = 0;
}


void ks_init(void) {
    // 6ms buffer
    // TODO -- i could save a lot of heap by only mallocing this when needed 
    ks_buffer = (float**) malloc(sizeof(float*)*OSCILLATORS);
    for(int i=0;i<OSCILLATORS;i++) ks_buffer[i] = (float*)malloc(sizeof(float)*MAX_KS_BUFFER_LEN); 
}

void ks_deinit(void) {
    for(int i=0;i<OSCILLATORS;i++) free(ks_buffer[i]);
    free(ks_buffer);
}





