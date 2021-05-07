// sounds.c
// various little "make a sound in firmware" methods
#include "alles.h"


// Play a sonar ping -- searching for wifi
void wifi_tone() {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 440;
    e.status = SCHEDULED;
    e.velocity = 1;
    e.adsr_a = 10;
    e.adsr_d = 500;
    e.adsr_s = 0;
    e.adsr_r = 0;
    e.adsr_target = TARGET_AMP;
    add_event(e);
    e.freq = 840;
    add_event(e);
}

// Play the "i'm going into midi mode" tone
void midi_tone() {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 440;
    e.status = SCHEDULED;
    e.velocity = 1;
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 660;
    add_event(e);
    e.time = sysclock + 300;
    e.velocity = 0;
    e.freq = 0;
    add_event(e);
}
// Schedule a bleep now
void bleep() {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 220;
    e.status = SCHEDULED;
    e.velocity = 1;
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 440;
    add_event(e);
    e.time = sysclock + 300;
    e.velocity = 0;
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
    e.velocity = 1;
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 220;
    add_event(e);
    e.time = sysclock + 300;
    e.velocity = 0;
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
        e.velocity = 1;
        e.status = SCHEDULED;
        add_event(e);
    }
}
