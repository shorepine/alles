// managers for hardware on the blinkinlabs PCB -- leds, buttons, battery IC
#include "blinkinlabs.h"




uint8_t battery_mask = 0;
static xQueueHandle gpio_evt_queue = NULL;


//============ Status LED =====================================================
static const ledc_channel_config_t ledc_channel = {
    .channel    = LEDC_CHANNEL_0,
    .duty       = 0,
    .gpio_num   = LED_STATUS,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .hpoint     = 0,
    .timer_sel  = LEDC_TIMER_0,
};



void status_led_set_state(status_led_state_t state) {
    uint32_t duty = 0;
    uint32_t freq = 0;

    switch(state) {
    case STATUS_LED_CHARGED:
        duty = 0xFFFF;
        freq = 1;
        break;
    case STATUS_LED_CHARGING:
        duty = 0x7FFFF;
        freq = 1;
        break;
    case STATUS_LED_DISCHARGING:
        duty = 0x1999;
        freq = 1;
        break;
    case STATUS_LED_LOW_BATTERY:
        duty = 0x1999;
        freq = 10;
        break;
    }

    if(ledc_get_duty(ledc_channel.speed_mode, ledc_channel.channel) != duty) {
//        printf("changing duty old:%i new:%i\n",
//                ledc_get_duty(ledc_channel.speed_mode, ledc_channel.channel),
//                duty);
        ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty);
        ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
    }

    if(ledc_get_freq(ledc_channel.speed_mode, ledc_channel.channel) != freq) {
//        printf("changing freq old:%i new:%i\n",
//                ledc_get_freq(ledc_channel.speed_mode, ledc_channel.channel),
//                freq);
        ledc_set_freq(ledc_channel.speed_mode, ledc_channel.channel, freq);
    }
}

esp_err_t status_led_init() {
    esp_err_t ret;

    const ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_16_BIT, // resolution of PWM duty
        .freq_hz = 1,                         // frequency of PWM signal
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // timer mode
        .timer_num = LEDC_TIMER_0,            // timer index
    };

    ret = ledc_timer_config(&ledc_timer);
    if(ret != ESP_OK)
        return ret;

    ret = ledc_channel_config(&ledc_channel);
    if(ret != ESP_OK)
        return ret;

    ret = ledc_fade_func_install(0);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}

//============ IP5306 monitor =================================================

TimerHandle_t ip5306_monitor_timer = NULL;

// Periodic task to poll the ip5306 for the battery charge and button states.
// Intented to be run from a low-priority timer at 1-2 Hz
void ip5306_monitor() {
    esp_err_t ret;

    // Check if the power button was pressed
    int buttons;
    ret = ip5306_button_press_get(&buttons);
    if(ret!= ESP_OK) {
        printf("Error reading button press\n");
        return;
    }

    if(buttons & BUTTON_LONG_PRESS) {
        const uint32_t button = BUTTON_POWER_LONG;
        xQueueSend(gpio_evt_queue, &button, 0);
    }
    if(buttons & BUTTON_SHORT_PRESS) {
        const uint32_t button = BUTTON_POWER_SHORT;
        xQueueSend(gpio_evt_queue, &button, 0);
    }

    // Update the battery charge state
    ip5306_charge_state_t charge_state;

    ret = ip5306_charge_state_get(&charge_state);
    if(ret != ESP_OK) {
        printf("Error reading battery charge state\n");
        return;
    }
    
    battery_mask = 0;

    switch(charge_state) {
    case CHARGE_STATE_CHARGED:
        battery_mask = battery_mask | BATTERY_STATE_CHARGED;
        //status_led_set_state(STATUS_LED_CHARGED);
        break;
    case CHARGE_STATE_CHARGING:
        battery_mask = battery_mask | BATTERY_STATE_CHARGING;
        //status_led_set_state(STATUS_LED_CHARGING);
        break;
    case CHARGE_STATE_DISCHARGING:
        battery_mask = battery_mask | BATTERY_STATE_DISCHARGING;
        //status_led_set_state(STATUS_LED_DISCHARGING);
        break;
    case CHARGE_STATE_DISCHARGING_LOW_BAT:
        battery_mask = battery_mask | BATTERY_STATE_LOW;
        //status_led_set_state(STATUS_LED_LOW_BATTERY);
        break;
    }

    ip5306_battery_voltage_t battery_voltage;

    ret = ip5306_battery_voltage_get(&battery_voltage);
    if(ret != ESP_OK) {
        printf("Error getting battery voltage\n");
        return;
    } else {
        if(battery_voltage == BATTERY_OVER_395) battery_mask = battery_mask | BATTERY_VOLTAGE_4;
        if(battery_voltage == BATTERY_38_395) battery_mask = battery_mask | BATTERY_VOLTAGE_3;
        if(battery_voltage == BATTERY_36_38) battery_mask = battery_mask | BATTERY_VOLTAGE_2;
        if(battery_voltage == BATTERY_33_36) battery_mask = battery_mask | BATTERY_VOLTAGE_1;
    }

}
