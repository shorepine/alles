// alles.h

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

#include "sineLUT.h"
#include "wifi.h"

#define SAMPLE_RATE 44100

extern void mcast_listen_task(void *pvParameters);

// C++ FM synth stuff
extern void dx7_init();
extern void render_samples(int16_t * buf, uint16_t len, uint8_t voice);
extern void dx7_new_note(uint8_t midi_note, uint8_t velocity, uint16_t patch, uint8_t voice);
extern void dx7_new_freq(float freq, uint8_t velocity, uint16_t patch, uint8_t voice);


// We like a lot of LUT for sines, but maybe don't need to alloc 16384*4 bytes for a square wave
#define SINE_LUT_SIZE 16383
#define OTHER_LUT_SIZE 2047

#define BLOCK_SIZE 256
#define VOICES 10 
#define SINE 0
#define SQUARE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4
#define FM 5
#define OFF 6

#ifdef __cplusplus
}
#endif


