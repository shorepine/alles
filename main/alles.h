// alles.h

#ifndef __ALLES_H
#define __ALLES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stddef.h>
#include <math.h>
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 2

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
#include "power.h"

// Constants you can change if you want
#define OSCS 64              // # of simultaneous oscs to keep track of 
#define BLOCK_SIZE 128        // i2s buffer block size in samples
#define EVENT_FIFO_LEN 3000   // number of events the queue can store
#define LATENCY_MS 1000      // fixed latency in milliseconds
#define SAMPLE_RATE 44100    // playback sample rate
#define SAMPLE_MAX 32767


// buffering for i2s
#define BYTES_PER_SAMPLE 2
#define I2S_BUFFERS 4

// This can be 32 bit, int32_t
#define I2S_SAMPLE_TYPE I2S_BITS_PER_SAMPLE_16BIT
typedef int16_t i2s_sample_type;

// D is how close the sample gets to the clip limit before the nonlinearity engages.  
// So D=0.1 means output is linear for -0.9..0.9, then starts clipping.
#define CLIP_D 0.1
#define MAX_RECEIVE_LEN 512  // max length of each message
#define UDP_PORT 3333        // port to listen on
#define MULTICAST_TTL 1      // hops multicast packets can take
#define MULTICAST_IPV4_ADDR "232.10.11.12"
#define PING_TIME_MS 10000   // ms between boards pinging each other
#define MAX_DRIFT_MS 20000   // ms of time you can schedule ahead before synth recomputes time base
#define LINEAR_INTERP        // use linear interp for oscs
// "The cubic stuff is just showing off.  One would only ever use linear in prod." -- dpwe, May 10 2021 
//#define CUBIC_INTERP         // use cubic interpolation for oscs
// Sample values for modulation sources
#define UP    32767
#define DOWN -32768
// center frequencies for the EQ
#define EQ_CENTER_LOW 800.0
#define EQ_CENTER_MED 2500.0
#define EQ_CENTER_HIGH 7000.0



// enums
#define DEVBOARD 0
#define ALLES_BOARD_V1 1
#define ALLES_BOARD_V2 2
#define BATTERY_STATE_CHARGING 0x01
#define BATTERY_STATE_CHARGED 0x02
#define BATTERY_STATE_DISCHARGING 0x04
#define BATTERY_VOLTAGE_4 0x10
#define BATTERY_VOLTAGE_3 0x20
#define BATTERY_VOLTAGE_2 0x40
#define BATTERY_VOLTAGE_1 0x80
// modulation/ADSR target mask
#define TARGET_AMP 1
#define TARGET_DUTY 2
#define TARGET_FREQ 4
#define TARGET_FILTER_FREQ 8
#define TARGET_RESONANCE 16
#define FILTER_LPF 1
#define FILTER_BPF 2
#define FILTER_HPF 3
#define FILTER_NONE 0
#define SINE 0
#define PULSE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4
#define FM 5
#define KS 6
#define PCM 7
#define OFF 8

#define EMPTY 0
#define SCHEDULED 1
#define PLAYED 2
#define AUDIBLE 3
#define IS_MOD_SOURCE 4

#define MAX_TASKS 9


// Pins & buttons
#define BUTTON_WAKEUP 34
#define BUTTON_WIFI 17
#define BUTTON_EXTRA 16
#define BUTTON_MIDI 0
#define ESP_INTR_FLAG_DEFAULT 0
#define CONFIG_I2S_LRCLK 25
#define CONFIG_I2S_BCLK 26
#define CONFIG_I2S_DIN 27
#define CONFIG_I2S_NUM 0 
#define MIDI_IN 19
#define BAT_SENSE_EN 32
#define CHARGE_STAT 33
#define POWER_5V_EN 21
#define BATT_SENSE_CHANNEL ADC_CHANNEL_7 // GPIO35 / ADC1_7
#define WALL_SENSE_CHANNEL ADC_CHANNEL_3 // GPIO39 / ADC1_3
#define CPU_MONITOR_0 13
#define CPU_MONITOR_1 12
#define CPU_MONITOR_2 15

    
enum params{
    WAVE, PATCH, MIDI_NOTE, AMP, DUTY, FEEDBACK, FREQ, VELOCITY, PHASE, VOLUME, FILTER_FREQ, RESONANCE, 
    MOD_SOURCE, MOD_TARGET, FILTER_TYPE, EQ_L, EQ_M, EQ_H, ADSR_TARGET, ADSR_A, ADSR_D, ADSR_S, ADSR_R, NO_PARAM
};

// Delta holds the individual changes from an event, it's sorted in order of playback time 
// this is more efficient in memory than storing entire events per message 
struct delta {
    uint32_t data; // casted to the right thing later
    enum params param; // which parameter is being changed
    uint32_t time; // what time to play / change this parameter
    int8_t osc; // which oscillator it impacts
    struct delta * next; // the next event, in time 
};


// Events are used to parse from ASCII UDP strings into, and also as each oscillators current internal state 
struct event {
    // todo -- clean up types here - many don't need to be signed anymore, and time doesn't need to be int64
    int64_t time;
    int8_t osc;
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
    int8_t mod_source;
    int8_t mod_target;
    int8_t adsr_target;
    int8_t filter_type;
    int64_t adsr_on_clock;
    int64_t adsr_off_clock;
    int32_t adsr_a;
    int32_t adsr_d;
    float adsr_s;
    int32_t adsr_r;
    // State variable for the impulse-integrating oscs.
    float lpf_state;
    // Constant offset to add to sawtooth before integrating.
    float dc_offset;
    // Decay alpha of LPF filter (e.g. 0.99 or 0.999).
    float lpf_alpha;
    // Selected lookup table and size.
    const float *lut;
    int16_t lut_size;
    float eq_l;
    float eq_m;
    float eq_h;
};

// events, but only the things that mods/env can change. one per osc
struct mod_event {
    float amp;
    float duty;
    float freq;
    float filter_freq;
    float resonance;
};

struct event default_event();
void add_event(struct event e);
void render_task();
void fill_audio_buffer_task();
void delay_ms(uint32_t ms);

// Status mask 
#define RUNNING 1
#define WIFI_MANAGER_OK 2
#define MIDI_MODE 4

// global synth state
struct state {
    float volume;
    float eq[3];
    uint16_t event_qsize;
    int16_t next_event_write;
    struct delta * event_start; // start of the sorted list
    uint8_t board_level;
    uint8_t status;
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
char *message_start_pointer;
int16_t message_length;
void parse_adsr(struct event * e, char* message) ;


// FM 
extern void fm_init();
extern void fm_deinit();
extern void render_fm(float * buf, uint8_t osc); 
extern void fm_note_on(uint8_t osc);
extern void fm_note_off(uint8_t osc);


// bandlimted oscs

extern void lpf_buf(float *buf, float decay, float *state);
extern float render_lut(float * buf, float step, float skip, float amp, const float* lut, int16_t lut_size);
extern void clear_buf(float *buf);
extern void cumulate_buf(const float *from, float *dest);

extern void ks_init();
extern void ks_deinit();

extern void render_ks(float * buf, uint8_t osc); 
extern void render_sine(float * buf, uint8_t osc); 
extern void render_pulse(float * buf, uint8_t osc); 
extern void render_saw(float * buf, uint8_t osc); 
extern void render_triangle(float * buf, uint8_t osc); 
extern void render_noise(float * buf, uint8_t osc); 
extern void render_pcm(float * buf, uint8_t osc);

extern float compute_mod_pulse(uint8_t osc);
extern float compute_mod_noise(uint8_t osc);
extern float compute_mod_sine(uint8_t osc);
extern float compute_mod_saw(uint8_t osc);
extern float compute_mod_triangle(uint8_t osc);
extern float compute_mod_pcm(uint8_t osc);

extern void ks_note_on(uint8_t osc); 
extern void ks_note_off(uint8_t osc);
extern void sine_note_on(uint8_t osc); 
extern void saw_note_on(uint8_t osc); 
extern void triangle_note_on(uint8_t osc); 
extern void pulse_note_on(uint8_t osc); 
extern void pcm_note_on(uint8_t osc);

extern void sine_mod_trigger(uint8_t osc);
extern void saw_mod_trigger(uint8_t osc);
extern void triangle_mod_trigger(uint8_t osc);
extern void pulse_mod_trigger(uint8_t osc);
extern void pcm_mod_trigger(uint8_t osc);



// filters
extern void filters_init();
extern void filters_deinit();
extern void filter_process(float * block, uint8_t osc);
extern void parametric_eq_process(float *block);
extern void update_filter(uint8_t osc);

// envelopes
extern float compute_adsr_scale(uint8_t osc);
extern float compute_mod_scale(uint8_t osc);
extern void retrigger_mod_source(uint8_t osc);

// MIDI
extern void midi_init();
extern void midi_deinit();
extern void read_midi();






#ifdef __cplusplus
}
#endif

#endif



