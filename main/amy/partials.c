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


// who defines which partials go to which speakers? 


// choose a patch from the .h file
void partials_note_on(uint8_t osc) {
	// we can set patches as groups, and store octave-spaced midi note versions of them?
	// this will choose the precise patch index from the whole set
	// let's see how big they are
	
}

// render a full partial set at offset osc (with patch)
// freq controls pitch_ratio -- what controls time ratio? 
// do all patches have sustain point?
void render_partials(uint8_t osc) {
	// we need to keep track of ms_offset here, yes


}