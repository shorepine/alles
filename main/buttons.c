// buttons.c
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
#include "alles.h"


xQueueHandle gpio_evt_queue = NULL;
extern struct state global;

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
static void gpio_task(void* arg) {
    uint32_t io_num;
    while(true) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            switch(io_num) {
            case BUTTON_EXTRA: 
                printf("extra pushed\n");
                esp_restart();
                break;
            case BUTTON_WIFI: 
                // WIFI config mode
                printf("wifi pushed\n");
                wifi_reconfigure();
                break;
            case BUTTON_MIDI: 
                printf("midi pushed\n");
                toggle_midi();
                break;
            case BUTTON_POWER_SHORT:
                printf("power short\n");
                break;
            case BUTTON_POWER_LONG:
                printf("power long\n");
                if(global.board_level == ALLES_BOARD_V1) global.running = 0;
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
    if(xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    //install gpio isr service
    ret = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    if(ret != ESP_OK)
        return ret;

    if(global.board_level == ALLES_BOARD_V1) {
        //hook isr handler for specific gpio pin
        esp_sleep_enable_ext0_wakeup(BUTTON_MIDI, 0);
        ret = gpio_isr_handler_add(BUTTON_EXTRA, gpio_isr_handler, (void*) BUTTON_EXTRA);
        if(ret != ESP_OK)
            return ret;

        ret = gpio_isr_handler_add(BUTTON_WIFI, gpio_isr_handler, (void*) BUTTON_WIFI);
        if(ret != ESP_OK)
            return ret;
    }

    ret = gpio_isr_handler_add(BUTTON_MIDI, gpio_isr_handler, (void*) BUTTON_MIDI);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}