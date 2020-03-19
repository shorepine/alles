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

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

#include "wifi.h"

#define SAMPLE_RATE 44100

#define MAX_RECEIVE_LEN 96
#define UDP_PORT 3333
#define MULTICAST_TTL 1
#define MULTICAST_IPV4_ADDR "232.10.11.12"

extern void mcast_listen_task(void *pvParameters);
extern void mcast_send(char * message, uint16_t len);
extern void create_multicast_ipv4_socket();
extern esp_ip4_addr_t s_ip_addr;

// FM 
extern void fm_init();
extern void render_fm(float * buf, uint16_t len, uint8_t voice, float amp);
extern void fm_new_note_number(uint8_t midi_note, uint8_t velocity, uint16_t patch, uint8_t voice);
extern void fm_new_note_freq(float freq, uint8_t velocity, uint16_t patch, uint8_t voice);

// bandlimted oscillators
extern void oscillators_init();
extern void render_ks(float * buf, uint16_t len, uint8_t voice, float freq, float duty, float amp);
extern void render_sine(float * buf, uint16_t len, uint8_t voice, float freq, float amp);
extern void render_pulse(float * buf, uint16_t len, uint8_t voice, float freq, float duty, float amp);
extern void render_saw(float * buf, uint16_t len, uint8_t voice, float freq, float amp);
extern void render_triangle(float * buf, uint16_t len, uint8_t voice, float freq, float amp);
extern void render_noise(float *buf, uint16_t len, float amp);
extern void ks_new_note_freq(float freq, uint8_t voice);

// We like a lot of LUT for sines, but maybe don't need to alloc 16384*4 bytes for a square wave
#define SINE_LUT_SIZE 16383

#define BLOCK_SIZE 256
#define VOICES 10 
#define SINE 0
#define PULSE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4
#define FM 5
#define KS 6
#define OFF 7

#define EVENT_FIFO_LEN 400
#define EMPTY 0
#define SCHEDULED 1
#define PLAYED 2
#define LATENCY_MS 500
#define MAX_MS_DRIFT 2500

#ifdef __cplusplus
}
#endif

#endif



