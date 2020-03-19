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
float UP = 32767; 
float DOWN = -32767;
int16_t intbuf[BLOCK_SIZE];
float kp_buffer[VOICES][588]; // 44100/75 -- 75Hz lowest we can go 

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

// needs a reset
extern "C" void render_ks(float * buf, uint16_t len, uint8_t voice, float freq, float feedback, float amp) {
    if(freq < 76) freq = 76; // lowest note we can play
    uint16_t buflen = floor(SAMPLE_RATE / freq);
    for(uint16_t i=0;i<len;i++) {
        uint16_t index = floor(step[voice]);
        sample[voice] = kp_buffer[voice][index];
        kp_buffer[voice][index] = (kp_buffer[voice][index] + kp_buffer[voice][(index + 1) % buflen]) * 0.5 * feedback;
        step[voice] = (index + 1) % buflen;
        buf[i] = buf[i] + sample[voice] * amp;
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
        synth.update(i, sample[voice]*amp);
    	step[voice]++;
	}
	blipbuf.end_frame(len);
	blipbuf.read_samples( intbuf, len, 0);
    for(int16_t i=0;i<len;i++) buf[i] = buf[i] + intbuf[i];
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
        synth.update(i, sample[voice]*amp);
    	step[voice]++;
	}
	blipbuf.end_frame(len);
	blipbuf.read_samples( intbuf, len, 0);
    for(int16_t i=0;i<len;i++) buf[i] = buf[i] + intbuf[i];
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
        synth.update(i, sample[voice] * amp);
        step[voice]++;
    }
    blipbuf.end_frame(len);
    blipbuf.read_samples( intbuf, len, 0);
    for(int16_t i=0;i<len;i++) buf[i] = buf[i] + intbuf[i];
}

extern "C" void ks_new_note_freq(float freq, uint8_t voice) {
    if(freq < 76) freq = 76; // lowest note we can play
    uint16_t buflen = floor(SAMPLE_RATE / freq);
    // init KP buffer with noise
    for(uint16_t i=0;i<buflen;i++) {
        kp_buffer[voice][i] = ( (int16_t) ((esp_random() >> 16) - 32768) );
    }
}

extern "C" void oscillators_init(void) {
	// use one-second buffer
    // TODO, malloc hang if set this 
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




