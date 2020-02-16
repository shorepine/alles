//dx7bridge.cc
#include <stddef.h>
#include "dx7/synth.h"
#include "dx7/module.h"
#include "dx7/aligned_buf.h"
#include "dx7/freqlut.h"
#include "dx7/wavout.h"
#include "dx7/sawtooth.h"
#include "dx7/sin.h"
#include "dx7/exp2.h"
#include "dx7/log2.h"
#include "dx7/resofilter.h"
#include "dx7/fm_core.h"
#include "dx7/fm_op_kernel.h"
#include "dx7/env.h"
#include "dx7/patch.h"
#include "dx7/controllers.h"
#include "dx7/dx7note.h"
#include "patches.h"
#include "dx7bridge.h"

// We want to keep Dx7Note static don't we, and controllers
Dx7Note note;
Controllers controllers;


extern "C" void render_samples(int16_t * buf, uint16_t len) {
    int32_t int32_t_buf[N];
    uint16_t rounds = len / N;
    uint16_t count = 0;
    for(int i=0;i<rounds;i++) {
	    // this computes "N" (which is 64) samples
	    for(int j=0;j<N;j++) {
	    	int32_t_buf[j] = 0;
	    }
		note.compute(int32_t_buf, 0, 0, &controllers);
		// Now make an int32 an int16_t, and put it in buf
		// from the wav writer
	    int32_t delta = 0x100;
		for(int j=0;j<N;j++) {
		    int32_t val = int32_t_buf[j] >> 2;
		    int clip_val = val < -(1 << 24) ? 0x8000 : (val >= (1 << 24) ? 0x7fff : (val + delta) >> 9);
		    delta = (delta + val) & 0x1ff;
		    buf[count++] = (int16_t) clip_val;
		}
	}
}


extern "C" void dx7_init(void) {
	double sample_rate = SAMPLE_RATE;
	Freqlut::init(sample_rate);
	Sawtooth::init(sample_rate);
	Sin::init();
	Exp2::init();
	Log2::init();
	// Unpack patches in h file
	char *unpacked_patches = (char*) malloc(PATCHES*156);
	for(int i=0;i<PATCHES;i++) {
		UnpackPatch(patches[i], unpacked_patches + (i*156));
	}
	// patch 2, note 50, vel 100
	note.init(unpacked_patches+(0*156), 50, 100);
	controllers.values_[kControllerPitch] = 0x2000; // pitch wheel?
	// now we're ready to render
}





