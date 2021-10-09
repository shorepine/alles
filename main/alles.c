// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"

uint8_t board_level;
uint8_t status;


// For CPU usage
unsigned long last_task_counters[MAX_TASKS];

// mutex that locks writes to the delta queue
SemaphoreHandle_t xQueueSemaphore;

void delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

uint8_t board_level = ALLES_BOARD_V2;
uint8_t status = RUNNING;

// Button event
extern xQueueHandle gpio_evt_queue;

// Task handles for the renderers, multicast listener and main
TaskHandle_t mcastTask = NULL;
TaskHandle_t parseTask = NULL;
TaskHandle_t renderTask[2]; // one per core
static TaskHandle_t fillbufferTask = NULL;
static TaskHandle_t idleTask0 = NULL;
static TaskHandle_t idleTask1 = NULL;

// Battery status for V2 board. If no v2 board, will stay at 0
uint8_t battery_mask = 0;

// AMY synth states
extern struct state global;
extern uint32_t event_counter;
extern uint32_t message_counter;


// Wrap AMY's renderer into 2 FreeRTOS tasks, one per core
void esp_render_task( void * pvParameters) {
    uint8_t which = *((uint8_t *)pvParameters);
    uint8_t start = (OSCS/2); 
    uint8_t end = OSCS;
    if(which == 0) { start = 0; end = (OSCS/2); } 
    printf("I'm renderer #%d on core #%d and i'm handling oscs %d up until %d\n", which, xPortGetCoreID(), start, end);
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        render_task(start, end, which);
        xTaskNotifyGive(fillbufferTask);
    }
}


// Make AMY's FABT run forever , as a FreeRTOS task 
void esp_fill_audio_buffer_task() {
    while(1) {
        int16_t *block = fill_audio_buffer_task();
        size_t written = 0;
        i2s_write((i2s_port_t)CONFIG_I2S_NUM, block, BLOCK_SIZE * BYTES_PER_SAMPLE, &written, portMAX_DELAY); 
        if(written != BLOCK_SIZE*BYTES_PER_SAMPLE) {
            printf("i2s underrun: %d vs %d\n", written, BLOCK_SIZE*BYTES_PER_SAMPLE);
        }
    }
}

// Make AMY's parse task run forever, as a FreeRTOS task (with notifications)
void esp_parse_task() {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        parse_task();
        xTaskNotifyGive(mcastTask);
    }
}


// init AMY from the esp. wraps some amy funcs in a task to do multicore rendering on the ESP32 
amy_err_t esp_amy_init() {
    start_amy();
    // We create a mutex for changing the event queue and pointers as two tasks do it at once
    xQueueSemaphore = xSemaphoreCreateMutex();

    // Create rendering threads, one per core so we can deal with dan ellis float math
    static uint8_t zero = 0;
    static uint8_t one = 1;
    xTaskCreatePinnedToCore(&esp_render_task, "render_task0", 4096, &zero, 4, &renderTask[0], 0);
    xTaskCreatePinnedToCore(&esp_render_task, "render_task1", 4096, &one, 4, &renderTask[1], 1);

    // Wait for the render tasks to get going before starting the i2s task
    delay_ms(100);

    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&esp_fill_audio_buffer_task, "fill_audio_buff", 4096, NULL, 22, &fillbufferTask, 0);

    // Grab the idle handles while we're here, we use them for CPU usage reporting
    idleTask0 = xTaskGetIdleTaskHandleForCPU(0);
    idleTask1 = xTaskGetIdleTaskHandleForCPU(1);
    return AMY_OK;
}


// Show a CPU usage counter. This shows the delta in use since the last time you called it
void esp_show_debug(uint8_t type) { 
    TaskStatus_t *pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x, i;
    const char* const tasks[] = { "render_task0", "render_task1", "mcast_task", "parse_task", "main", "fill_audio_buff", "wifi", "idle0", "idle1", 0 }; 
    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, NULL );
    unsigned long counter_since_last[MAX_TASKS];
    unsigned long ulTotalRunTime = 0;
    TaskStatus_t xTaskDetails;
    // We have to check for the names we want to track
    for(i=0;i<MAX_TASKS;i++) { // for each name
        counter_since_last[i] = 0;
        for(x=0; x<uxArraySize; x++) { // for each task
            if(strcmp(pxTaskStatusArray[x].pcTaskName, tasks[i])==0) {
                counter_since_last[i] = pxTaskStatusArray[x].ulRunTimeCounter - last_task_counters[i];
                last_task_counters[i] = pxTaskStatusArray[x].ulRunTimeCounter;
                ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
            }
        }
        // Have to get these specially as the task manager calls them both "IDLE" and swaps their orderings around
        if(strcmp("idle0", tasks[i])==0) { 
            vTaskGetInfo(idleTask0, &xTaskDetails, pdFALSE, eRunning);
            counter_since_last[i] = xTaskDetails.ulRunTimeCounter - last_task_counters[i];
            last_task_counters[i] = xTaskDetails.ulRunTimeCounter;
            ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
        }
        if(strcmp("idle1", tasks[i])==0) { 
            vTaskGetInfo(idleTask1, &xTaskDetails, pdFALSE, eRunning);
            counter_since_last[i] = xTaskDetails.ulRunTimeCounter - last_task_counters[i];
            last_task_counters[i] = xTaskDetails.ulRunTimeCounter;
            ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
        }

    }
    printf("------ CPU usage since last call to debug()\n");
    for(i=0;i<MAX_TASKS;i++) {
        printf("%-15s\t%-15ld\t\t%2.2f%%\n", tasks[i], counter_since_last[i], (float)counter_since_last[i]/ulTotalRunTime * 100.0);
    }   
    printf("------\nEvent queue size %d / %d. Received %d events and %d messages\n", global.event_qsize, EVENT_FIFO_LEN, event_counter, message_counter);
    event_counter = 0;
    message_counter = 0;
    vPortFree(pxTaskStatusArray);

}

   

// Setup I2S
amy_err_t setup_i2s(void) {
    //i2s configuration
    i2s_config_t i2s_config = {
         .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
         .sample_rate = SAMPLE_RATE,
         .bits_per_sample = I2S_SAMPLE_TYPE,
         .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
         .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
         .intr_alloc_flags = 0, //ESP_INTR_FLAG_LEVEL1, // high interrupt priority
         .dma_buf_count = 2, //I2S_BUFFERS,
         .dma_buf_len = 1024, //BLOCK_SIZE * BYTES_PER_SAMPLE,
         .tx_desc_auto_clear = true,
        };
        
    i2s_pin_config_t pin_config = {
        .bck_io_num = CONFIG_I2S_BCLK, 
        .ws_io_num = CONFIG_I2S_LRCLK,  
        .data_out_num = CONFIG_I2S_DIN, 
        .data_in_num = -1   //Not used
    };
    SET_PERI_REG_BITS(I2S_TIMING_REG(0), 0x1, 1, I2S_TX_DSYNC_SW_S);

    i2s_driver_install((i2s_port_t)CONFIG_I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin((i2s_port_t)CONFIG_I2S_NUM, &pin_config);
    i2s_set_sample_rates((i2s_port_t)CONFIG_I2S_NUM, SAMPLE_RATE);
    return AMY_OK;
}




// callback to let us know when we have wifi set up ok.
void wifi_connected(void *pvParameter){
    ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI("main", "I have a connection and my IP is %s!", str_ip);
    status |= WIFI_MANAGER_OK;
}


// Called when the WIFI button is hit. Deletes the saved SSID/pass and restarts into the captive portal
void wifi_reconfigure() {
     printf("reconfigure wifi\n");

    if(wifi_manager_config_sta){
        memset(wifi_manager_config_sta, 0x00, sizeof(wifi_config_t));
    }

    if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
        wifi_manager_generate_ip_info_json( UPDATE_USER_DISCONNECT );
        wifi_manager_unlock_json_buffer();
    }

    wifi_manager_save_sta_config();
    delay_ms(100);
    esp_restart();
}



// Called when the MIDI button is hit. Toggle between MIDI on and off mode
void toggle_midi() {
    if(!(status & WIFI_MANAGER_OK)) {
        // we haven't connected yet, ignore
        printf("MIDI mode pressed but not yet connected\n");
    } else if(status & MIDI_MODE) { 
        // just restart, easier that way
        esp_restart();
    } else {
        // turn on midi
        status = MIDI_MODE | RUNNING;
        // Play a MIDI sound before shutting down oscs
        midi_tone();
        delay_ms(500);

        // stop rendering
        vTaskDelete(fillbufferTask);
        // stop parsing
        vTaskDelete(parseTask);
        // stop receiving
        vTaskDelete(mcastTask);

        // have to free RAM to start the BLE stack
        oscs_deinit();

        // start midi
        midi_init();
    }
}

void power_monitor() {
    power_status_t power_status;

    const amy_err_t ret = power_read_status(&power_status);
    if(ret != AMY_OK)
        return;

    // print a debugging power status every few seconds to the monitor 
    /*
    char buf[100];
    snprintf(buf, sizeof(buf),
        "powerStatus: power_source=\"%s\",charge_status=\"%s\",wall_v=%0.3f,battery_v=%0.3f\n",
        (power_status.power_source == POWER_SOURCE_WALL ? "wall" : "battery"),
        (power_status.charge_status == POWER_CHARGE_STATUS_CHARGING ? "charging" :
            (power_status.charge_status == POWER_CHARGE_STATUS_CHARGED ? "charged" : " discharging")),
        power_status.wall_voltage/1000.0,
        power_status.battery_voltage/1000.0
        );

    printf(buf);
    */

    battery_mask = 0;

    switch(power_status.charge_status) {
        case POWER_CHARGE_STATUS_CHARGED:
            battery_mask = battery_mask | BATTERY_STATE_CHARGED;
            break;
        case POWER_CHARGE_STATUS_CHARGING:
            battery_mask = battery_mask | BATTERY_STATE_CHARGING;
            break;
        case POWER_CHARGE_STATUS_DISCHARGING:
            battery_mask = battery_mask | BATTERY_STATE_DISCHARGING;
            break;        
    }

    float voltage = power_status.wall_voltage/1000.0;
    if(voltage > 3.95) battery_mask = battery_mask | BATTERY_VOLTAGE_4; else 
    if(voltage > 3.80) battery_mask = battery_mask | BATTERY_VOLTAGE_3; else 
    if(voltage > 3.60) battery_mask = battery_mask | BATTERY_VOLTAGE_2; else 
    if(voltage > 3.30) battery_mask = battery_mask | BATTERY_VOLTAGE_1;
}

void esp_shutdown() {
    // TODO: Where did these come from? JTAG?
    gpio_pullup_dis(14);
    gpio_pullup_dis(15);
    esp_sleep_enable_ext1_wakeup((1ULL<<BUTTON_WAKEUP),ESP_EXT1_WAKEUP_ALL_LOW);
    esp_deep_sleep_start();
}

void app_main() {
    for(uint8_t i=0;i<MAX_TASKS;i++) last_task_counters[i] = 0;
    check_init(&esp_event_loop_create_default, "Event");
    // TODO -- this does not properly detect DEVBOARD anymore, not a big deal for now, doesn't impact anything
    // if power init fails, we don't have blinkinlabs board, set board level to 0
    if(check_init(&power_init, "power")) {
        printf("No power IC, assuming DIY Alles\n");
        board_level = DEVBOARD; 
    }
    if(board_level == ALLES_BOARD_V2) {
        printf("Detected revB Alles\n");
        TimerHandle_t power_monitor_timer = xTimerCreate(
            "power_monitor",
            pdMS_TO_TICKS(5000),
            pdTRUE,
            NULL,
            power_monitor);
        xTimerStart(power_monitor_timer, 0);

    }

    // TODO: one of these interferes with the power monitor, not a big deal, we don't use both at once
    // Setup GPIO outputs for watching CPU usage on an oscilloscope 
    /*
    const gpio_config_t out_conf = {
         .mode = GPIO_MODE_OUTPUT,            
         .pin_bit_mask = (1ULL<<CPU_MONITOR_0) | (1ULL<<CPU_MONITOR_1) | (1ULL<<CPU_MONITOR_2),
    };
    gpio_config(&out_conf); 

    // Set them all to low
    gpio_set_level(CPU_MONITOR_0, 0); // use 0 as ground for the scope 
    gpio_set_level(CPU_MONITOR_1, 0); // use 1 for the rendering loop 
    gpio_set_level(CPU_MONITOR_2, 0); // use 2 for whatever you want 
    */

    check_init(&sync_init, "sync"); 
    check_init(&setup_i2s, "i2s");
    esp_amy_init();
    check_init(&buttons_init, "buttons"); // only one button for the protoboard, 4 for the blinkinlabs

    wifi_manager_start();
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &wifi_connected);


    // A funny story: in the early development of Alles, I had four prototype speakers with me as I flew a little
    // prop plane from YBL to YVR. The bag with the speakers went into the cargo hold of the prop plane and when we
    // landed everyone could hear this weird chiming noise -- it turns out all four speakers got turned on during the 
    // flight and at the time there was a bug where if it couldn't connect to the stored wifi, the power button
    // wouldn't be active either and all four speakers were stuck making the wifi chime. Quite a fun noise to hear in an
    // airplane! I didn't have any programming cables or a screwdriver with me so I had to put them all in a hotel 
    // safe covered with a pillow to get their batteries to die. 

    //So now they shut off after MAX_WIFI_WAIT_S if they can't connect.
    int64_t start_time = get_sysclock();
    delay_ms(250);
    while((!(status & WIFI_MANAGER_OK) && (status & RUNNING) )) {
        wifi_tone();
        for(uint8_t i=0;i<250;i++) { 
            if(!(status & RUNNING)) {
                debleep();
                delay_ms(500);
                esp_shutdown();
            }
            delay_ms(10);
        }
        //delay_ms(2500);
        if(get_sysclock() - start_time > (MAX_WIFI_WAIT_S*1000)) esp_shutdown();
    }

    // We check for RUNNING as someone could have pressed power already
    if(!(status & RUNNING)) {
        // shut down
        debleep();
        delay_ms(500);
        esp_shutdown();
    }

    delay_ms(500);
    reset_oscs();

    // Setup the socket
    create_multicast_ipv4_socket();

    // Create the task that waits for UDP messages, parses them and puts them on the sequencer queue (core 1)
    xTaskCreatePinnedToCore(&esp_parse_task, "parse_task", 4096, NULL, 2, &parseTask, 0);
    // Create the task that listens fro new incoming UDP messages (core 2)
    xTaskCreatePinnedToCore(&mcast_listen_task, "mcast_task", 4096, NULL, 3, &mcastTask, 1);

    // Schedule a "turning on" sound
    bleep();

    // Spin this core until the power off button is pressed, parsing events and making sounds
    while(status & RUNNING) {
        delay_ms(10);
    }

    // If we got here, the power off button was pressed 
    // Play a "turning off" sound
    debleep();
    delay_ms(500);
    esp_shutdown();
}

