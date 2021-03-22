// envelope.c
// VCA

#include "alles.h"

/*
Here's the spec, sort of
Can assign ADSR to a voice, specified in ms (but maybe locked to BLOCK_SIZE / 1.5ms)?
We may / may not have preset ADSR curves, pulled from DX7 patches for fun
Otherwise it's user supplied and attached to a voice
In normal use, it does nothing
But if you send a note ON, you need to either give it an A,D,S,R (or uses a default preset for missing #s)
and that voice will play along that envelope pattern
note OFF / velocity 0 will trigger release but voice keeps going until end of release
FM patches are weird here, but oh! we can just use the note on/off already in there and ignore ADSR params 

A, time, D time, S -- %/fraction, R, time
A phase is a linear ramp from 0 to max (amp) over A ms
D phase is a linear ramp from max to S over D ms
S phase is straight until note off is signaled
R phase is linear ramp from S to 0 over R ms


this is why phase is going to be important -- if i want a "bass drum" sound, a sine wave with a adsr and a decreasing freq over short time,
i do both a LFO from a sine wave at a phase that goes down, and an ADSR 

so really there are two things you can add to voices: envelopes and LFOs

LFOs eat up a voice, but that's fine, params are:
	source voice [with its own rate, wavetype, depth (amp), etc]

ADSR params do not eat up a voice. params are:
	A - ms
	D - ms
	S - %
	R - ms
	(or presets, later)

both have:
	target voice
	target param (amp, freq, pw, filter, reso)
	only active between note on and off
	retrigger on note on -- source osc step starts at 0, etc


ok, so protocol is

follow (either oscillator or envelope, if set) v[target_voice]T[target_param]L[source_voice]
envelope -- v[target_voice]T[target_param]E[A,D,S,R]  

how do I un "L" a source_voice (or target voice!)
L-1 -- resets the following -- if LFO, source_voice stops being an LFO. either way, target stops being updated
E-1 tales env off voice 

how do I have filter following a note on ADSR then? (envelope follower)

v0T0E10,250,.5,50 -- sets envelope for voice 0 to target amp for that voice
L0T3 -- will have filter cutoff follow the ADSR set on v0 (if no ADSR set, this will use the oscillator itself as an "LFO")
v0w0f440e100 -- plays a note; this should be under the ADSR envelope, and the filter cutoff should also track the envelope
v0e0 -- note off
L-1T3 -- turn off filter following
if it was duty, more like
v1L0T2 -- follow ADSR of voice 0, but use it to modify duty of voice 1
v1L-1T2 -- turn off duty following
I guess you can have more than one L per voice
yeah it's -- do envelopes lead or not is the deign q, it's the same thing i think


ok, need to think about multiples here
a voice always has an ADSR attached. by default it's 0,0,1,0.
you can change ADSR with a envelope command: v0L0,0,0.5,0 etc
ADSR can control up to 5 things with a bitmask - 1 + 2 + 4 + 8 + 16, filter+amp is 9
T0 means none
but do you ever want to remove an amp envelope? Oh yes, you would.. sure - if it's a constant tone but with env for filters
OK, that should work for T



*/

extern struct event* seq;
extern struct mod_event* mseq;

extern struct mod_state mglobal;

// Yeah, ok. ADSRs & LFOs happpen at BLOCK_SIZE resolution for now. not per sample. 
// can update to ADSRs per sample later after i hear it

float compute_lfo_scale(uint8_t voice) {
	return 1;
}

float compute_adsr_scale(uint8_t voice) {
	// get the scale out of a voice
	int64_t sysclock = esp_timer_get_time() / 1000;
	float scale = 1.0; // the overall ratio to modify the thing

	if(seq[voice].adsr_on_clock >= 0) { 
		// figure out where we are in the curve
		int64_t elapsed = sysclock - seq[voice].adsr_on_clock;
		if(elapsed > (seq[voice].adsr_d+seq[voice].adsr_a)) { // we're in sustain
			scale = seq[voice].adsr_s;
		} else if(elapsed > seq[voice].adsr_a) { // decay
			// compute the decay scale
			elapsed = elapsed - seq[voice].adsr_a; // time since d started 
			float elapsed_ratio = ((float) elapsed / (float)seq[voice].adsr_d);
			scale = 1.0 - ((1.0-seq[voice].adsr_s) * elapsed_ratio);
		} else { // attack
			// compute the attack scale
			scale = (float)elapsed / (float)seq[voice].adsr_a; 
		}
	} else if(seq[voice].adsr_off_clock >= 0) {
		int64_t elapsed = sysclock - seq[voice].adsr_off_clock;
		if(elapsed > seq[voice].adsr_r) {
			scale = 0; // note is done
			seq[voice].wave=OFF; // or else it'll just start playing as an oscillator again
			// Turn off release clock
			seq[voice].adsr_off_clock = -1;
		} else {
			// compute the release scale
			scale = seq[voice].adsr_s - (((float) elapsed / (float) seq[voice].adsr_r) * seq[voice].adsr_s); 
		}
	} else {
		// Nothing. scale = 1
	}
	if(scale < 0) scale = 0;
	return scale;
}

