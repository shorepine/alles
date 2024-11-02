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
#include "alles.h"


xQueueHandle gpio_evt_queue = NULL;
extern uint8_t status;
extern uint8_t board_level;


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
            case BUTTON_WAKEUP:
                printf("power pushed\n");
                status = 0;
                break;
            case BUTTON_MINUS: 
                printf("minus pushed\n");
                amy_decrease_volume();
                break;
            case BUTTON_WIFI: 
                // WIFI config mode
                printf("wifi pushed\n");
                wifi_reconfigure();
                break;
            case BUTTON_PLUS: 
                printf("plus pushed\n");
                if(!(status & WIFI_MANAGER_OK)) { 
                    status |= UPDATE;
                } else {
                    amy_increase_volume();
                }
                break;
            }

            // Ignore any other button presses that come in for the next 100ms
            delay_ms(100);
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
        .pin_bit_mask = (1ULL << BUTTON_WAKEUP)
                        | (1ULL<<BUTTON_MINUS)
                        | (1ULL<<BUTTON_WIFI)
                        | (1ULL<<BUTTON_PLUS),  //bit mask of the pins that you want to set,e.g.GPIO18/19
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
    if(xTaskCreate(gpio_task, "gpio_task", 4096, NULL, 10, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    //install gpio isr service
    ret = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    if(ret != ESP_OK)
        return ret;

    if(board_level == ALLES_BOARD_V2) {
        //hook isr handler for specific gpio pin

        ret = gpio_isr_handler_add(BUTTON_WAKEUP, gpio_isr_handler, (void*) BUTTON_WAKEUP);
        if(ret != ESP_OK)
            return ret;

        ret = gpio_isr_handler_add(BUTTON_MINUS, gpio_isr_handler, (void*) BUTTON_MINUS);
        if(ret != ESP_OK)
            return ret;

        ret = gpio_isr_handler_add(BUTTON_PLUS, gpio_isr_handler, (void*) BUTTON_PLUS);
        if(ret != ESP_OK)
            return ret;

        ret = gpio_isr_handler_add(BUTTON_WIFI, gpio_isr_handler, (void*) BUTTON_WIFI);
        if(ret != ESP_OK)
            return ret;
    }

    return ESP_OK;
}