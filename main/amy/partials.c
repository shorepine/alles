// partials.c
// deal with partials

#include "amy.h"


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

extern struct event *synth;
extern struct mod_event *msynth; // the synth that is being modified by modulations & envelopes
extern struct state global; 


// who defines which partials go to which speakers? 


// choose a patch from the .h file
void partials_note_on(uint8_t osc) {
	// we can set patches as groups, and store octave-spaced midi note versions of them?
	// this will choose the precise patch index from the whole set
	// let's see how big they are
	// just like PCM, start & end breakpoint are stored here
	synth[osc].step = partial_breakpoints_offset_map[synth[osc].patch*4];
	synth[osc].substep = synth[osc].step + partial_breakpoints_offset_map[synth[osc].patch*4 + 1];

}

void partials_note_off(uint8_t osc) {
	// todo; finish the sustain
	synth[osc].step = -1;
}

// render a full partial set at offset osc (with patch)
// freq controls pitch_ratio, amp amp_ratio -- what controls time ratio? 
// do all patches have sustain point?
// this is less "render" and more "set up a bunch of other oscillators" -- keep buf empty 
void render_partials(float *buf, uint8_t osc) {
	// we need to keep track of ms_offset here, yes
	char bp[25];
	uint32_t ms_since_started = ((total_samples - synth[osc].note_on_clock) / (float)SAMPLE_RATE)*1000.0;
	if(synth[osc].step >= 0) {
		// do we either have no sustain, or are we past sustain? 
		uint32_t sustain_ms = partial_breakpoints_offset_map[synth[osc].patch*4 + 3];
		//printf("sustain_ms %d ms_since_started %d\n", sustain_ms, ms_since_started);
		if((sustain_ms > 0 && (ms_since_started < sustain_ms)) || sustain_ms == 0) {
			partial_breakpoint_t pb = partial_breakpoints[(uint32_t)synth[osc].step];
			if(ms_since_started >= pb.ms_offset ) {
				// set up this oscillator
			    struct event e = default_event();
			    e.osc = pb.osc + 1 + osc;
		        e.time = get_sysclock();
		        e.wave = PARTIAL;
		        e.amp = pb.amp * msynth[osc].amp;
		        if(pb.phase>=-1) { // start or continuation 
		        	float source_freq = freq_for_midi_note(partial_breakpoints_offset_map[synth[osc].patch*4 + 2]);
		        	float freq_ratio = msynth[osc].freq / source_freq; 

			        e.freq = pb.freq * freq_ratio; 
			        e.feedback = pb.bw * msynth[osc].feedback;

			        sprintf(bp, "%d,%f,0,0", pb.ms_delta, pb.amp_delta);
			        parse_breakpoint(&e, bp ,0);
			        e.breakpoint_target[0] = TARGET_AMP + TARGET_LINEAR;

			        sprintf(bp, "%d,%f,0,0", pb.ms_delta, pb.freq_delta);
			        parse_breakpoint(&e, bp ,1);
			        e.breakpoint_target[1] = TARGET_FREQ + TARGET_LINEAR;

			        if(e.feedback > 0) {
				        sprintf(bp, "%d,%f,0,0", pb.ms_delta, pb.bw_delta);
				        parse_breakpoint(&e, bp ,2);
				        e.breakpoint_target[2] = TARGET_FEEDBACK + TARGET_LINEAR;
				    }

			        e.velocity = e.amp; 
			    } else { // end
			    	e.velocity = 0;
			    }
				if(pb.phase >= 0) { // start
					e.wave = SINE;
					e.phase = pb.phase;
				}
				//printf("Adding event at time %d osc %d ms %d offset %d\n", e.time, e.osc, ms_since_started, pb.ms_offset);
		        add_event(e);

				synth[osc].step++;

				if(synth[osc].step == synth[osc].substep) {
					partials_note_off(osc);
				}
			}
		}
	}
	for(uint16_t i=0;i<BLOCK_SIZE;i++) buf[i] = 0;
}

