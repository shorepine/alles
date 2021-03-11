// blinkinlabs.h
#include "master_i2c.h"
#include "ip5306.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "driver/i2c.h"
#include "master_i2c.h"
#include "driver/i2s.h"
#include "driver/ledc.h"

#define CONFIG_I2C_SDA_GPIO 18
#define CONFIG_I2C_SCL_GPIO 5
#define CONFIG_I2C_FREQ_KHZ 100
#define LED_STATUS 4
#define BUTTON_EXTRA 16
#define BUTTON_WIFI 17
#define BUTTON_MIDI 0

#define BATTERY_STATE_CHARGING 0x01
#define BATTERY_STATE_CHARGED 0x02
#define BATTERY_STATE_DISCHARGING 0x04
#define BATTERY_STATE_LOW 0x08
#define BATTERY_VOLTAGE_4 0x10
#define BATTERY_VOLTAGE_3 0x20
#define BATTERY_VOLTAGE_2 0x40
#define BATTERY_VOLTAGE_1 0x80



typedef enum {
    STATUS_LED_CHARGED,     // Constant on
    STATUS_LED_CHARGING,    // 50% duty cycle, 1hz frequency
    STATUS_LED_DISCHARGING, // 10% duty cycle, 1hz frequency
    STATUS_LED_LOW_BATTERY, // 10% duty cycle, 2hz frequency
} status_led_state_t;

esp_err_t buttons_init();
esp_err_t status_led_init();
void status_led_set_state(status_led_state_t state);
void ip5306_monitor();
extern void wifi_reconfigure();
extern void start_immediate_mode();
