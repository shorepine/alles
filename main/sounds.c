// sounds.c
// various little "make a sound in firmware" methods
#include "alles.h"

// Schedule a bleep now
void bleep() {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 220;
    e.status = SCHEDULED;
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 440;
    add_event(e);
    e.time = sysclock + 300;
    e.wave = OFF;
    e.freq = 0;
    add_event(e);
}

void debleep() {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 440;
    e.status = SCHEDULED;
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 220;
    add_event(e);
    e.time = sysclock + 300;
    e.wave = OFF;
    e.freq = 0;
    add_event(e);
}


// Plays a short scale 
void scale(uint8_t wave) {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    for(uint8_t i=0;i<12;i++) {
        e.time = sysclock + (i*250);
        e.wave = wave;
        e.midi_note = 48+i;
        e.status = SCHEDULED;
        add_event(e);
    }
}
