// managers for hardware on the blinkinlabs PCB -- leds, buttons, battery IC
#include "blinkinlabs.h"


//============ Button handler =================================================

#define BUTTON_POWER_SHORT 100  // Button state from IP5306
#define BUTTON_POWER_LONG 101   // Button state from IP5306

#define ESP_INTR_FLAG_DEFAULT 0

static xQueueHandle gpio_evt_queue = NULL;
uint8_t battery_mask = 0;

// Called whenever a button press triggers a GPIO interrupt
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    // Note: A simple form of deboucing is implemented by setting the size of
    // the queue to one element. Because of the fact that the receiver task
    // usually takes much longer than a debounce interval to handle the key press
    // event, it blocks the ISR from posting multiple press events. This isn't
    // perfect.
    const uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// Low-pritority task to handle button events. Blocks on events received through
// the gpio_evt_queue.
static void gpio_task_example(void* arg) {
    uint32_t io_num;
    while(true) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            switch(io_num) {
            case BUTTON_EXTRA: 
                printf("extra pushed\n");
                break;
            case BUTTON_WIFI: 
                // WIFI config mode
                printf("wifi pushed\n");
                wifi_reconfigure();
                break;
            case BUTTON_MIDI: 
                printf("midi pushed\n");
                start_immediate_mode();
                break;
            case BUTTON_POWER_SHORT:
                printf("power short\n");
                break;
            case BUTTON_POWER_LONG:
                printf("power long\n");
                break;
            }

            // Ignore any other button presses that come in for the next 100ms
            vTaskDelay(100/portTICK_PERIOD_MS);
            xQueueReset(gpio_evt_queue);
        }
    }
}

esp_err_t buttons_init() {
    esp_err_t ret;

    // Configure buttons as interrupt events
    const gpio_config_t in_conf = {
        .intr_type = GPIO_INTR_POSEDGE,     //enable interrupt on positive edge
        .mode = GPIO_MODE_INPUT,            //set as input mode
        .pin_bit_mask = (1ULL<<BUTTON_EXTRA) | (1ULL<<BUTTON_WIFI) | (1ULL<<BUTTON_MIDI), //bit mask of the pins that you want to set,e.g.GPIO18/19
        .pull_down_en = 0,                  //disable pull-down mode
        .pull_up_en = 1,                    //enable pull-up mode
    };
    ret = gpio_config(&in_conf);                  //configure GPIO with the given settings
    if(ret != ESP_OK)
        return ret;

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(1, sizeof(uint32_t));
    if(gpio_evt_queue == NULL)
        return ESP_ERR_NO_MEM;

    //start gpio task
    if(xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    //install gpio isr service
    ret = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    if(ret != ESP_OK)
        return ret;

    //hook isr handler for specific gpio pin
    ret = gpio_isr_handler_add(BUTTON_EXTRA, gpio_isr_handler, (void*) BUTTON_EXTRA);
    if(ret != ESP_OK)
        return ret;

    ret = gpio_isr_handler_add(BUTTON_WIFI, gpio_isr_handler, (void*) BUTTON_WIFI);
    if(ret != ESP_OK)
        return ret;

    ret = gpio_isr_handler_add(BUTTON_MIDI, gpio_isr_handler, (void*) BUTTON_MIDI);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}

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
