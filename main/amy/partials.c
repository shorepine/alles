// partials.c
// deal with partials

#include "amy.h"

typedef struct {
	uint32_t bp_offset;
	uint32_t bp_length;
	uint8_t midi_note;
	uint32_t sustain_ms;
	uint8_t oscs_alloc;
} partial_breakpoint_map_t;

typedef struct {
	uint32_t ms_offset;
	uint8_t osc;
	float freq;
	float amp;
	float bw;
	float phase;
	uint16_t ms_delta;
	float amp_delta;
	float freq_delta;
	float bw_delta;
} partial_breakpoint_t;

// Automatically generated from partials.generate_partials_header()
#include "partials.h"




// who defines which partials go to which speakers? 


// choose a patch from the .h file
void partials_note_on(uint8_t osc) {
	// we can set patches as groups, and store octave-spaced midi note versions of them?
	// this will choose the precise patch index from the whole set
	// let's see how big they are
	// just like PCM, start & end breakpoint are stored here
	synth[osc].step = partial_breakpoints_offset_map[synth[osc].patch*5 + 0];
	synth[osc].substep = synth[osc].step + partial_breakpoints_offset_map[synth[osc].patch*5 + 1];
	// Now let's start the oscillators (silently)
	uint8_t oscs = partial_breakpoints_offset_map[synth[osc].patch*5 + 4];
	if(osc + 1 + oscs > OSCS) {
		printf("Asking for more oscs than you have -- starting %d, + 1 + %d more\n", osc, oscs);
	}
	for(uint8_t i=osc+1;i<osc+1+oscs;i++) {
	    synth[i % OSCS].note_on_clock = total_samples;
    	synth[i % OSCS].status = IS_ALGO_SOURCE; 
	    sine_note_on(i % OSCS);
	}
}

void partials_note_off(uint8_t osc) {
	// todo; finish the sustain
	synth[osc].step = -1;
}

// render a full partial set at offset osc (with patch)
// freq controls pitch_ratio, amp amp_ratio, ratio controls time ratio
// do all patches have sustain point?
void render_partials(float *buf, uint8_t osc) {
	// If ratio is set (not 0 or -1), use it for a time stretch
	float time_ratio = 1;
	if(synth[osc].ratio > 0) time_ratio = synth[osc].ratio;
	uint32_t ms_since_started = (((total_samples - synth[osc].note_on_clock) / (float)SAMPLE_RATE)*1000.0)*time_ratio;
	if(synth[osc].step >= 0) {
		// do we either have no sustain, or are we past sustain? 
		// TODO: sustain is likely more complicated --we want to bounce between the closest bps for loopstart & loopend
		uint32_t sustain_ms = partial_breakpoints_offset_map[synth[osc].patch*5 + 3];
		if((sustain_ms > 0 && (ms_since_started < sustain_ms)) || sustain_ms == 0) {
			partial_breakpoint_t pb = partial_breakpoints[(uint32_t)synth[osc].step];
			if(ms_since_started >= pb.ms_offset ) {
				// set up this oscillator
			    uint8_t o = (pb.osc + 1 + osc) % OSCS; // just in case
			    synth[o].wave = PARTIAL;
		        synth[o].amp = pb.amp;

		        if(pb.phase>=-1) { // start or continuation 
		        	// Find our ratio using the midi note of the analyzed partial
		        	float freq_ratio = msynth[osc].freq / freq_for_midi_note(partial_breakpoints_offset_map[synth[osc].patch*5 + 2]); 

			        synth[o].freq = pb.freq * freq_ratio;
			        synth[o].feedback = pb.bw * msynth[osc].feedback;

			        synth[o].breakpoint_times[0][0] = ms_to_samples((int)((float)pb.ms_delta/time_ratio));
			        synth[o].breakpoint_values[0][0] = pb.amp_delta; 
			        synth[o].breakpoint_times[0][1] = 0; 
			        synth[o].breakpoint_values[0][1] = 0; 
			        synth[o].breakpoint_target[0] = TARGET_AMP + TARGET_LINEAR;

			        synth[o].breakpoint_times[1][0] = ms_to_samples((int)((float)pb.ms_delta/time_ratio));
			        synth[o].breakpoint_values[1][0] = pb.freq_delta; 
			        synth[o].breakpoint_times[1][1] = 0; 
			        synth[o].breakpoint_values[1][1] = 0; 
			        synth[o].breakpoint_target[1] = TARGET_FREQ + TARGET_LINEAR;

			        if(synth[o].feedback > 0) {
	      			    synth[o].breakpoint_times[2][0] = ms_to_samples((int)((float)pb.ms_delta/time_ratio));
				        synth[o].breakpoint_values[2][0] = pb.bw_delta; 
				        synth[o].breakpoint_times[2][1] = 0; 
				        synth[o].breakpoint_values[2][1] = 0; 
			    	    synth[o].breakpoint_target[2] = TARGET_FEEDBACK + TARGET_LINEAR;
				    }
			    } else { // end partial
    	            synth[o].note_on_clock = -1;
			    	synth[o].note_off_clock = total_samples;
			    }
				if(pb.phase >= 0) { // start of a partial, use SINE type to capture phase
					synth[o].wave = SINE;
					synth[o].phase = pb.phase;
					synth[o].note_on_clock = total_samples;
				}
				synth[osc].step++;
				printf("ms is %d step is now %f sub %f\n", ms_since_started, synth[osc].step, synth[osc].substep);
				if(synth[osc].step == synth[osc].substep) {
					partials_note_off(osc);
				}
			}
		}
	}
	// now, render everything, add it up
	uint8_t oscs = partial_breakpoints_offset_map[synth[osc].patch*5 + 4];
	for(uint16_t i=0;i<BLOCK_SIZE;i++) buf[i] = 0;
	for(uint8_t i=osc+1;i<osc+1+oscs;i++) {
		uint8_t o = i % OSCS;
	    hold_and_modify(o);
		if(synth[o].wave==SINE) render_sine(buf, o);
		if(synth[o].wave==PARTIAL) render_partial(buf, o);
	}
	for(uint16_t i=0;i<BLOCK_SIZE;i++) buf[i] = buf[i] * msynth[osc].amp;

}

