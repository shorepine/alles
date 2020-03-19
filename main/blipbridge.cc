#include <stddef.h>
#include "blip/Blip_Buffer.h"
extern "C" { 
	#include "alles.h"
}

static Blip_Buffer blipbuf;
static Blip_Synth<blip_good_quality,65535> synth;

int32_t count[VOICES];
int32_t count2[VOICES];
int16_t amplitude[VOICES];
int16_t UP = 30000; 
int16_t DOWN = -30000;



extern "C" void render_blip_wave(int16_t * ibuf, int16_t * obuf, uint16_t len) {
    for(uint16_t i=0;i<len;i++) {
  		synth.update(i, ibuf[i]);
	}
	blipbuf.end_frame(len);
	blipbuf.read_samples( obuf, len, 0);
}

extern "C" void render_blip_saw(int16_t * buf, uint16_t len, uint8_t voice, float freq) {
    float fperiod = 1. / (freq/(float)SAMPLE_RATE);
    uint16_t period = floor(fperiod);
    for(uint16_t i=0;i<len;i++) {
    	if(count[voice]%  period == 0)  {
    		amplitude[voice] = DOWN;
    		count[voice] = 0; // reset the period counter
    	} else {
    		amplitude[voice] = DOWN + (count[voice] * ((UP-DOWN) / period));
    	}
  		synth.update(i, amplitude[voice]);
    	count[voice]++;
	}
	blipbuf.end_frame(len);
	blipbuf.read_samples( buf, len, 0);
}

extern "C" void render_blip_triangle(int16_t * buf, uint16_t len, uint8_t voice, float freq) {
    float fperiod = 1. / (freq/(float)SAMPLE_RATE);
    uint16_t period = floor(fperiod);
    for(uint16_t i=0;i<len;i++) {
    	if(count[voice] % period == 0)  {
    		amplitude[voice] = DOWN;
    		count[voice] = 0; // reset the period counter
    	} else {
    		if(count[voice] < (period/2)) {
	    		amplitude[voice] = DOWN + (count[voice] * ((UP-DOWN) / period * 2));
	    	} else {
	    		amplitude[voice] = UP - ((count[voice]-(period/2)) * ((UP-DOWN) / period * 2));
	    	}
    	}
  		synth.update(i, amplitude[voice]);
    	count[voice]++;
	}
	blipbuf.end_frame(len);
	blipbuf.read_samples( buf, len, 0);
}


extern "C" void render_blip_pulse(int16_t * buf, uint16_t len, uint8_t voice, float freq, float duty) {
    if(duty < 0.001 || duty > 0.999) duty = 0.5;
    float fperiod = 1. / (freq/(float)SAMPLE_RATE);
    float fperiod2 = fperiod/(1.0/duty); // if duty is 0.5, square wave
    uint16_t period = floor(fperiod);
    uint16_t period2 = floor(fperiod2);
    for(uint16_t i=0;i<len;i++) {
    	if(count[voice] % period == 0)  {
    		amplitude[voice] = UP;
    		count2[voice] = 0; // start the duty cycle counter
    		count[voice] = 0; // reset the period counter
    	}
    	if(amplitude[voice] == UP) {
    		if(count2[voice]++ > period2) {
    			amplitude[voice] = DOWN;
    		}
    	}

  		synth.update(i, amplitude[voice]);
    	count[voice]++;
	}
	blipbuf.end_frame(len);
	blipbuf.read_samples( buf, len, 0);
}


extern "C" void blip_init(void) {
	// use one-second buffer
	if ( blipbuf.set_sample_rate( SAMPLE_RATE ) )
		exit( EXIT_FAILURE );
	blipbuf.clock_rate( blipbuf.sample_rate() );
	blipbuf.bass_freq( 0 ); // makes waveforms perfectly flat
	synth.volume(1);
	synth.output(&blipbuf);
	for(uint8_t voice=0;voice<VOICES;voice++) { amplitude[voice] = DOWN; count[voice] = 0; count2[voice] =0;}
}




