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


extern "C" void render_samples(uint16_t * buf, uint16_t len) {
    int32_t int32_t_buf[N];

    uint16_t rounds = len / N;
    uint16_t count = 0;
    for(int i=0;i<rounds;i++) {
	    // this computes "N" (which is 64) samples
		note.compute(int32_t_buf, 0, 0, &controllers);
		// Now make an int32 a uint16_t, and put it in buf
		for(int j=0;j<N;j++) {
			// TOOD -- look at native datatype
			buf[count++] = (uint16_t) (int32_t_buf[j] >> 2);
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





