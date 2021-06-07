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

#include "amy/amy.h"



// This can be 32 bit, int32_t -- helpful for digital output to a i2s->USB teensy3 board
#define I2S_SAMPLE_TYPE I2S_BITS_PER_SAMPLE_16BIT
typedef int16_t i2s_sample_type;

#define UDP_PORT 3333        // port to listen on
#define MULTICAST_TTL 1      // hops multicast packets can take
#define MULTICAST_IPV4_ADDR "232.10.11.12"
#define PING_TIME_MS 10000   // ms between boards pinging each other

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

void delay_ms(uint32_t ms);

// Status mask 
#define RUNNING 1
#define WIFI_MANAGER_OK 2
#define MIDI_MODE 4

uint8_t board_level;
uint8_t status;

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
extern int16_t client_id;

// MIDI
extern void midi_init();
extern void midi_deinit();
extern void read_midi();






#ifdef __cplusplus
}
#endif

#endif



