#include "alles.h"
#include "sinLUT_1024.h"
#include "impulse32_1024.h"
#include "pcm.h"

// TODO -- i could save a lot of heap by only mallocing this when needed 
#define MAX_KS_BUFFER_LEN 802 // 44100/55  -- 55Hz (A1) lowest we can go for KS
float ** ks_buffer; 


extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by LFOs & envelopes
extern struct state global; 

extern float *scratchbuf;

void pcm_note_on(uint8_t voice) {
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
void render_pcm(float * buf, uint8_t voice) {
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


//#define LINEAR_INTERP
#define CUBIC_INTERP

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

void lpf_buf(float *buf, float decay, float &state) {
    // Implement first-order low-pass (leaky integrator).
    for (uint16_t i = 0; i < BLOCK_SIZE; ++i) {
      buf[i] = decay * state + buf[i];
      state = buf[i];
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

void pulse_note_on(uint8_t voice) {
    // So i reset step to some phase math, right? yeah
    synth[voice].step = (float)IMPULSE32_SIZE * synth[voice].phase;
    synth[voice].lpf_state[0] = 0;
}

void render_pulse(float * buf, uint8_t voice) {
    // LPF time constant should be ~ 10x oscillator period, so droop is minimal.
    float period_samples = 44100.0 / msynth[voice].freq;
    synth[voice].lpf_alpha = 1.0 - 1.0 / (10.0 * period_samples);
    float duty = msynth[voice].duty;
    if (duty < 0.01) duty = 0.01;
    if (duty > 0.99) duty = 0.99;
    float skip = msynth[voice].freq / 44100.0 * IMPULSE32_SIZE;
    // Scale the impulse proportional to the skip so its integral remains ~constant.
    // 0.5 is to compensate for typical skip of ~2.
    float amp = msynth[voice].amp * skip * 0.1;  // was 0.5
    float pwm_step = synth[voice].step + duty * IMPULSE32_SIZE;
    if (pwm_step >= IMPULSE32_SIZE)  pwm_step -= IMPULSE32_SIZE;
    clear_buf(scratchbuf);
    synth[voice].step = render_lut(scratchbuf, synth[voice].step, skip, amp, impulse32, IMPULSE32_SIZE);
    render_lut(scratchbuf, pwm_step, skip, -amp, impulse32, IMPULSE32_SIZE);
    lpf_buf(scratchbuf, synth[voice].lpf_alpha, synth[voice].lpf_state[0]);
    // Accumulate into actual output buffer.
    cumulate_buf(scratchbuf, buf);
}

void saw_note_on(uint8_t voice) {
    synth[voice].step = (float)IMPULSE32_SIZE * synth[voice].phase;
    synth[voice].lpf_state[0] = 0;
}

void render_saw(float * buf, uint8_t voice) {
    // For saw, we *want* the lpf to droop across the period, so use a smaller alpha.
    float period_samples = 44100.0 / msynth[voice].freq;
    synth[voice].lpf_alpha = 1.0 - 1.0 / period_samples;
    float skip = msynth[voice].freq / 44100.0 * IMPULSE32_SIZE;
    // Scale the impulse proportional to the skip so its integral remains ~constant.
    // 0.5 is to compensate for typical skip of ~2.
    float amp = msynth[voice].amp * skip * 0.1;
    clear_buf(scratchbuf);
    synth[voice].step = render_lut(
          scratchbuf, synth[voice].step, skip, amp, impulse32, IMPULSE32_SIZE);
    lpf_buf(scratchbuf, synth[voice].lpf_alpha, synth[voice].lpf_state[0]);
    cumulate_buf(scratchbuf, buf);
}

void triangle_note_on(uint8_t voice) {
    synth[voice].step = (float)IMPULSE32_SIZE * synth[voice].phase;
    synth[voice].lpf_state[0] = 0;
    synth[voice].lpf_state[1] = 0;
}

void render_triangle(float * buf, uint8_t voice) {
    // Triangle has two lpfs, one to build the square, and one to integrate the pulse.
    float period_samples = 44100.0 / msynth[voice].freq;
    synth[voice].lpf_alpha = 1.0 - 1.0 / (10 * period_samples);
    synth[voice].lpf_alpha_1 = 1.0 - 1.0 / period_samples;
    float duty = msynth[voice].duty;
    if (duty < 0.01) duty = 0.01;
    if (duty > 0.99) duty = 0.99;
    float skip = msynth[voice].freq / 44100.0 * IMPULSE32_SIZE;
    // Scale impulses by skip to make integral independent of skip.
    // Scale by skip again so integral of each cycle of square wave is same total amp.
    // Divide by duty(1-duty) so peak integral is const regardless of duty.
    float amp = msynth[voice].amp * skip * skip / (duty - duty * duty) * 0.0001;
    float pwm_step = synth[voice].step + duty * IMPULSE32_SIZE;
    if (pwm_step >= IMPULSE32_SIZE)  pwm_step -= IMPULSE32_SIZE;
    clear_buf(scratchbuf);
    synth[voice].step = render_lut(scratchbuf, synth[voice].step, skip, amp, impulse32, IMPULSE32_SIZE);
    render_lut(scratchbuf, pwm_step, skip, -amp, impulse32, IMPULSE32_SIZE);
    // Integrate once to get square wave.
    lpf_buf(scratchbuf, synth[voice].lpf_alpha, synth[voice].lpf_state[0]);
    // Integrate again to get trianlge wave.
    lpf_buf(scratchbuf, synth[voice].lpf_alpha_1, synth[voice].lpf_state[1]);
    cumulate_buf(scratchbuf, buf);
}

void sine_note_on(uint8_t voice) {
    // So i reset step to some phase math, right? yeah
    synth[voice].step = (float)SINLUT_SIZE * synth[voice].phase;
}

void render_sine(float * buf, uint8_t voice) { 
    float skip = msynth[voice].freq / 44100.0 * SINLUT_SIZE;
    synth[voice].step = render_lut(buf, synth[voice].step, skip, msynth[voice].amp, sinLUT, SINLUT_SIZE);
}

void render_noise(float *buf, uint8_t voice) {
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        buf[i] = buf[i] + ( (int16_t) ((esp_random() >> 16) - 32768) * msynth[voice].amp);
    }
}

void render_ks(float * buf, uint8_t voice) {
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

void ks_note_on(uint8_t voice) {
    if(msynth[voice].freq<=0) msynth[voice].freq = 1;
    uint16_t buflen = floor(SAMPLE_RATE / msynth[voice].freq);
    if(buflen > MAX_KS_BUFFER_LEN) buflen = MAX_KS_BUFFER_LEN;
    // init KS buffer with noise up to max
    for(uint16_t i=0;i<buflen;i++) {
        ks_buffer[voice][i] = ( (int16_t) ((esp_random() >> 16) - 32768) );
    }
}

void ks_note_off(uint8_t voice) {
    msynth[voice].amp = 0;
}


void oscillators_init(void) {
    // 6ms buffer
    // TODO -- i could save a lot of heap by only mallocing this when needed 
    ks_buffer = (float**) malloc(sizeof(float*)*VOICES);
    for(int i=0;i<VOICES;i++) ks_buffer[i] = (float*)malloc(sizeof(float)*MAX_KS_BUFFER_LEN); 
}

void oscillators_deinit(void) {
    for(int i=0;i<VOICES;i++) free(ks_buffer[i]);
    free(ks_buffer);
}





