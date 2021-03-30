// alles.h

#ifndef __ALLES_H
#define __ALLES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/uart.h"

#include "driver/i2s.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"
#include "wifi_manager.h"
#include "http_app.h"
#include "blemidi.h"


// Constants you can change if you want
#define BLOCK_SIZE 64       // i2s buffer block size in samples
#define VOICES 10            // # of simultaneous voices to keep track of 
#define EVENT_FIFO_LEN 400   // number of events the queue can store
#define LATENCY_MS 1000      // fixed latency in milliseconds
#define SAMPLE_RATE 44100    // playback sample rate
#define MAX_RECEIVE_LEN 127  // max length of each message
#define UDP_PORT 3333        // port to listen on
#define MULTICAST_TTL 1      // hops multicast packets can take
#define MULTICAST_IPV4_ADDR "232.10.11.12"
#define PING_TIME_MS 10000   // ms between boards pinging each other
#define MAX_DRIFT_MS 20000   // ms of time you can schedule ahead before synth recomputes time base

#define DEVBOARD 0
#define ALLES_BOARD_V1 1
#define ALLES_BOARD_V2 2

#include "ip5306.h"
#include "master_i2c.h"
#define BATTERY_STATE_CHARGING 0x01
#define BATTERY_STATE_CHARGED 0x02
#define BATTERY_STATE_DISCHARGING 0x04
#define BATTERY_STATE_LOW 0x08
#define BATTERY_VOLTAGE_4 0x10
#define BATTERY_VOLTAGE_3 0x20
#define BATTERY_VOLTAGE_2 0x40
#define BATTERY_VOLTAGE_1 0x80
#define BUTTON_POWER_SHORT 100  // Button state from IP5306
#define BUTTON_POWER_LONG 101   // Button state from IP5306

// Buttons set on the blinkinlabs board, by default only MIDI on most prototype boards
#define BUTTON_EXTRA 16
#define BUTTON_WIFI 17
#define BUTTON_MIDI 0
#define ESP_INTR_FLAG_DEFAULT 0

// pins
#define CONFIG_I2S_LRCLK 25
#define CONFIG_I2S_BCLK 26
#define CONFIG_I2S_DIN 27
#define CONFIG_I2S_NUM 0 
#define MIDI_IN 19

// LFO/ADSR target mask
#define TARGET_AMP 1
#define TARGET_DUTY 2
#define TARGET_FREQ 4
#define TARGET_FILTER_FREQ 8
#define TARGET_RESONANCE 16

// Events
struct event {
    int64_t time;
    int16_t voice;
    int16_t wave;
    int16_t patch;
    int16_t midi_note;
    float amp;
    float duty;
    float feedback;
    float freq;
    uint8_t status;
    float velocity;
    float phase;
    float step;
    float substep;
    float sample;
    float volume;
    float filter_freq;
    float resonance;
    int8_t lfo_source;
    int8_t lfo_target;
    int8_t adsr_target;
    int64_t adsr_on_clock;
    int64_t adsr_off_clock;
    int16_t adsr_a;
    int16_t adsr_d;
    float adsr_s;
    int16_t adsr_r;

};

// only the things that LFOs/env can change per voice
struct mod_event {
    float amp;
    float duty;
    float freq;
};

struct event default_event();
void add_event(struct event e);


// Status mask 
#define RUNNING 1
#define WIFI_MANAGER_OK 2
#define MIDI_MODE 4

// global synth state
struct state {
    float volume;
    float resonance;
    float filter_freq;
    int16_t next_event_write;
    uint8_t board_level;
    uint8_t status;
};

// global synth state, only the things LFO/env can change
struct mod_state {
    float resonance;
    float filter_freq;
};


// Sounds
extern void bleep();
extern void debleep();
extern void midi_tone();
extern void wifi_tone();
extern void scale(uint8_t wave);


// Button handlers
void wifi_reconfigure();
void toggle_midi();
extern esp_err_t buttons_init();

// wifi and multicast
extern wifi_config_t* wifi_manager_config_sta ;
extern void mcast_listen_task(void *pvParameters);
extern void mcast_send(char * message, uint16_t len);
extern void create_multicast_ipv4_socket();
extern void update_map(uint8_t client, uint8_t ipv4, int64_t time);
extern void handle_sync(int64_t time, int8_t index);
extern uint8_t alive;
extern int64_t computed_delta; // can be negative no prob, but usually host is larger # than client
extern uint8_t computed_delta_set; // have we set a delta yet?
extern int16_t client_id;


// FM 
extern void fm_init();
extern void fm_deinit();
extern void render_fm(float * buf, uint8_t voice); 
extern void fm_note_on(uint8_t voice);
extern void fm_note_off(uint8_t voice);


// bandlimted oscillators
extern void oscillators_init();
extern void oscillators_deinit();
extern void blip_the_buffer(float * ibuf, int16_t * obuf,  uint16_t len ) ;
extern void render_ks(float * buf, uint8_t voice); 
extern void render_sine(float * buf, uint8_t voice); 
extern void render_pulse(float * buf, uint8_t voice); 
extern void render_saw(float * buf, uint8_t voice); 
extern void render_triangle(float * buf, uint8_t voice); 
extern void render_noise(float * buf, uint8_t voice); 
extern void ks_note_on(uint8_t voice); 
extern void ks_note_off(uint8_t voice);
extern void sine_note_on(uint8_t voice); 
extern void saw_note_on(uint8_t voice); 
extern void triangle_note_on(uint8_t voice); 
extern void pulse_note_on(uint8_t voice); 


// filters
extern void filters_init();
extern void filters_deinit();
extern void filter_process(float * block);
extern void filter_update();
extern void filter_process_ints(int16_t * block);

// envelopes
extern float compute_adsr_scale(uint8_t voice);
extern float compute_lfo_scale(uint8_t voice);
extern void retrigger_lfo_source(uint8_t voice);

// MIDI
extern void midi_init();
extern void midi_deinit();
extern void read_midi();

#define SINE_LUT_SIZE 16383


#define NUM_WAVES 8
#define SINE 0
#define PULSE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4
#define FM 5
#define KS 6
#define OFF 7

#define EMPTY 0
#define SCHEDULED 1
#define PLAYED 2
#define AUDIBLE 3
#define LFO_SOURCE 4


#define UP    16383
#define DOWN -16384


#ifdef __cplusplus
}
#endif

#endif



