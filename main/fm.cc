#include <stddef.h>
#include "dx7/synth.h"
#include "dx7/module.h"
#include "dx7/aligned_buf.h"
#include "dx7/freqlut.h"
#include "dx7/sawtooth.h"
#include "dx7/sin.h"
#include "dx7/exp2.h"
#include "dx7/log2.h"
#include "dx7/resofilter.h"
#include "dx7/fm_core.h"
#include "dx7/fm_op_kernel.h"
#include "dx7/env.h"
#include "dx7/controllers.h"
#include "dx7/dx7note.h"
#include "patches.h"
extern "C" { 
    #include "alles.h"
}

// One note/controller object per voice
Dx7Note note[VOICES];
Controllers controllers[VOICES];
extern struct event sequencer[VOICES];

extern "C" void render_fm(float * buf, uint8_t voice) {
    int32_t int32_t_buf[N];
    uint16_t rounds = BLOCK_SIZE / N;
    uint16_t count = 0;
    // I2S requests usually 256 samples, FM synth renders 64 at a time
    for(int i=0;i<rounds;i++) {
        // this computes "N" (which is 64) samples

        // Important -- clear out this first -- note.compute is accumulative
        for(int j=0;j<N;j++) int32_t_buf[j] = 0;

        note[voice].compute(int32_t_buf, 0, 0, &controllers[voice]);

        // Now make an int32 an int16_t, and put it in buf, this is from their wav writer
        int32_t delta = 0x100;
        for(int j=0;j<N;j++) {
            // TODO -- this clips/crackles at >> 2, the original in msfa. 
            int32_t val = int32_t_buf[j] >> 3;
            int clip_val = val < -(1 << 24) ? 0x8000 : (val >= (1 << 24) ? 0x7fff : (val + delta) >> 9);
            delta = (delta + val) & 0x1ff;
            buf[count] = buf[count] + clip_val * sequencer[voice].amp;
            count++;
        }
    }
}

extern "C" void fm_new_note_freq(uint8_t voice) {
    note[voice].init_with_freq(patches+(sequencer[voice].patch*156), sequencer[voice].freq, sequencer[voice].velocity);
    controllers[voice].values_[kControllerPitch] = 0x2000; // pitch wheel

}
extern "C" void fm_new_note_number(uint8_t voice) {
    note[voice].init(patches+(sequencer[voice].patch*156), sequencer[voice].midi_note, sequencer[voice].velocity);
    controllers[voice].values_[kControllerPitch] = 0x2000; // pitch wheel
}

extern "C" void fm_init(void) {
    double sample_rate = SAMPLE_RATE;
    Freqlut::init(sample_rate);
    Sawtooth::init(sample_rate);
    Sin::init();
    Exp2::init();
    Log2::init();
}

extern "C" void fm_destroy(void) {
    // Not sure i need to worry about this too much, but for good hygiene  
    // Probably do a lot here
}




