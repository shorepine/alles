// alles_esp32.c
// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am

#include "alles.h"
#define configUSE_TASK_NOTIFICATIONS 1
//#define configTASK_NOTIFICATION_ARRAY_ENTRIES 2
#define MAX_WIFI_WAIT_S 120

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"
#include "wifi_manager.h"
#include "http_app.h"
#include "power.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
i2s_chan_handle_t tx_handle;


// This can be 32 bit, int32_t -- helpful for digital output to a i2s->USB teensy3 board
#define I2S_SAMPLE_TYPE I2S_BITS_PER_SAMPLE_16BIT
typedef int16_t i2s_sample_type;


// Button handlers
void wifi_reconfigure();
extern esp_err_t buttons_init();
void esp_show_debug(uint8_t type);

// wifi and multicast
extern wifi_config_t* wifi_manager_config_sta ;
extern void mcast_listen_task(void *pvParameters);

uint8_t board_level;
uint8_t status;
uint8_t debug_on = 0;

// For CPU usage
unsigned long last_task_counters[MAX_TASKS];

// mutex that locks writes to the delta queue
SemaphoreHandle_t xQueueSemaphore;

void delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

uint8_t board_level = ALLES_BOARD_V2;
uint8_t status = RUNNING;

char githash[8];

// Button event
extern xQueueHandle gpio_evt_queue;

// Task handles for the renderers, multicast listener and main
TaskHandle_t mcastTask = NULL;
TaskHandle_t parseTask = NULL;
TaskHandle_t upgradeTask = NULL;

TaskHandle_t alles_handle;
TaskHandle_t alles_parse_handle;
TaskHandle_t alles_receive_handle;
TaskHandle_t amy_render_handle;
TaskHandle_t alles_fill_buffer_handle;
TaskHandle_t idle_0_handle;
TaskHandle_t idle_1_handle;


#define ALLES_TASK_COREID (1)
#define ALLES_PARSE_TASK_COREID (0)
#define ALLES_RECEIVE_TASK_COREID (1)
#define ALLES_RENDER_TASK_COREID (0)
#define ALLES_FILL_BUFFER_TASK_COREID (1)
#define ALLES_PARSE_TASK_PRIORITY (ESP_TASK_PRIO_MIN +2)
#define ALLES_RECEIVE_TASK_PRIORITY (ESP_TASK_PRIO_MIN + 3)
#define ALLES_RENDER_TASK_PRIORITY (ESP_TASK_PRIO_MAX-1 )
#define ALLES_FILL_BUFFER_TASK_PRIORITY (ESP_TASK_PRIO_MAX-1)
#define ALLES_TASK_NAME             "alles_task"
#define ALLES_PARSE_TASK_NAME       "alles_par_task"
#define ALLES_RECEIVE_TASK_NAME     "alles_rec_task"
#define ALLES_RENDER_TASK_NAME      "alles_r_task"
#define ALLES_FILL_BUFFER_TASK_NAME "alles_fb_task"
#define ALLES_TASK_STACK_SIZE    (4 * 1024) 
#define ALLES_PARSE_TASK_STACK_SIZE (8 * 1024)
#define ALLES_RECEIVE_TASK_STACK_SIZE (4 * 1024)
#define ALLES_RENDER_TASK_STACK_SIZE (8 * 1024)
#define ALLES_FILL_BUFFER_TASK_STACK_SIZE (8 * 1024)


// Battery status for V2 board. If no v2 board, will stay at 0
uint8_t battery_mask = 0;

// AMY synth states
extern struct state amy_global;
extern uint32_t event_counter;
extern uint32_t message_counter;


// Render the second core
void esp_render_task( void * pvParameters) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        amy_render(0, AMY_OSCS/2, 1);
        xTaskNotifyGive(alles_fill_buffer_handle);
    }
}

// Make AMY's FABT run forever , as a FreeRTOS task 
void esp_fill_audio_buffer_task() {
    while(1) {
        AMY_PROFILE_START(AMY_ESP_FILL_BUFFER)

        // Get ready to render
        amy_prepare_buffer();
        // Tell the other core to start rendering
        xTaskNotifyGive(amy_render_handle);
        // Render me
        amy_render(AMY_OSCS/2, AMY_OSCS, 0);
        // Wait for the other core to finish
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Write to i2s
        int16_t *block = amy_fill_buffer();
        AMY_PROFILE_STOP(AMY_ESP_FILL_BUFFER)

        // We turn off writing to i2s on r10 when doing on chip debugging because of pins
        #ifndef TULIP_R10_DEBUG
        size_t written = 0;
        i2s_channel_write(tx_handle, block, AMY_BLOCK_SIZE * BYTES_PER_SAMPLE * AMY_NCHANS, &written, portMAX_DELAY);
        if(written != AMY_BLOCK_SIZE * BYTES_PER_SAMPLE * AMY_NCHANS) {
            fprintf(stderr,"i2s underrun: %d vs %d\n", written, AMY_BLOCK_SIZE * BYTES_PER_SAMPLE * AMY_NCHANS);
        }
        #endif


    }
}

// Make AMY's parse task run forever, as a FreeRTOS task (with notifications)
void esp_parse_task() {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        alles_parse_message(message_start_pointer, message_length);
        xTaskNotifyGive(mcastTask);
    }
}


// init AMY from the esp. wraps some amy funcs in a task to do multicore rendering on the ESP32 
amy_err_t esp_amy_init() {
    amy_start(2, 0, 1);
    amy_global.latency_ms = ALLES_LATENCY_MS;
    // We create a mutex for changing the event queue and pointers as two tasks do it at once
    xQueueSemaphore = xSemaphoreCreateMutex();

    // Create the second core rendering task
    xTaskCreatePinnedToCore(&esp_render_task, ALLES_RENDER_TASK_NAME, ALLES_RENDER_TASK_STACK_SIZE, NULL, ALLES_RENDER_TASK_PRIORITY, &amy_render_handle, ALLES_RENDER_TASK_COREID);

    // Wait for the render tasks to get going before starting the i2s task
    delay_ms(100);

    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&esp_fill_audio_buffer_task, ALLES_FILL_BUFFER_TASK_NAME, ALLES_FILL_BUFFER_TASK_STACK_SIZE, NULL, ALLES_FILL_BUFFER_TASK_PRIORITY, &alles_fill_buffer_handle, ALLES_FILL_BUFFER_TASK_COREID);

    // Grab the idle handles while we're here, we use them for CPU usage reporting
    idle_0_handle = xTaskGetIdleTaskHandleForCPU(0);
    idle_1_handle = xTaskGetIdleTaskHandleForCPU(1);
    return AMY_OK;
}


// Show a CPU usage counter. This shows the delta in use since the last time you called it
void esp_show_debug(uint8_t type) { 
    TaskStatus_t *pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x, i;
    const char* const tasks[] = { ALLES_PARSE_TASK_NAME, ALLES_RECEIVE_TASK_NAME, ALLES_RENDER_TASK_NAME, ALLES_FILL_BUFFER_TASK_NAME, "main", "wifi", "IDLE0", "IDLE1", 0 }; 
    const uint8_t cores[] = {ALLES_PARSE_TASK_COREID, ALLES_RECEIVE_TASK_COREID, ALLES_RENDER_TASK_COREID, ALLES_FILL_BUFFER_TASK_COREID, 0, 0, 0, 1, 0};

    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, NULL );
    unsigned long counter_since_last[MAX_TASKS];
    //unsigned long ulTotalRunTime = 0;
    unsigned long ulTotalRunTime_per_core[2];
    ulTotalRunTime_per_core[0] = 0;
    ulTotalRunTime_per_core[1] = 0;

    //TaskStatus_t xTaskDetails;

    // We have to check for the names we want to track
    for(i=0;i<MAX_TASKS;i++) { // for each name
        counter_since_last[i] = 0;
        for(x=0; x<uxArraySize; x++) { // for each task
            if(strcmp(pxTaskStatusArray[x].pcTaskName, tasks[i])==0) {
                counter_since_last[i] = pxTaskStatusArray[x].ulRunTimeCounter - last_task_counters[i];
                last_task_counters[i] = pxTaskStatusArray[x].ulRunTimeCounter;
                //ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
                ulTotalRunTime_per_core[cores[i]] += counter_since_last[i];
            }
        }
    }
    printf("------ CPU usage since last call to debug()\n");
    for(i=0;i<MAX_TASKS;i++) {
        printf("%d %-15s\t%-15ld\t\t%2.2f%%\n", cores[i], tasks[i], counter_since_last[i], (float)counter_since_last[i]/ulTotalRunTime_per_core[cores[i]] * 100.0);
    }   
    printf("------\nEvent queue size %d / %d. Received %" PRIu32 " events and %" PRIu32 " messages\n", amy_global.event_qsize, AMY_EVENT_FIFO_LEN, event_counter, message_counter);
    event_counter = 0;
    message_counter = 0;
    vPortFree(pxTaskStatusArray);

}

   

// Setup I2S
amy_err_t setup_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AMY_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_I2S_BCLK,
            .ws = CONFIG_I2S_LRCLK,
            .dout = CONFIG_I2S_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);

    /* Before writing data, start the TX channel first */
    i2s_channel_enable(tx_handle);
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

void firmware_upgrade( void * pvParameters) {
    esp_http_client_config_t config = {
        .url = "https://github.com/shorepine/alles/raw/main/ota/alles.bin",
        .cert_pem = NULL,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        printf("Problem with upgrade %i %s\n", ret, esp_err_to_name(ret));
    }
    esp_restart();
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

    float voltage = power_status.battery_voltage/1000.0;
    if(voltage > 3.95) battery_mask = battery_mask | BATTERY_VOLTAGE_4; else 
    if(voltage > 3.80) battery_mask = battery_mask | BATTERY_VOLTAGE_3; else 
    if(voltage > 3.60) battery_mask = battery_mask | BATTERY_VOLTAGE_2; else 
    if(voltage > 3.30) battery_mask = battery_mask | BATTERY_VOLTAGE_1;
}

void turn_off() {
    debleep();
    delay_ms(500);
    // TODO: Where did these come from? JTAG?
    gpio_pullup_dis(14);
    gpio_pullup_dis(15);
    esp_sleep_enable_ext1_wakeup((1ULL<<BUTTON_WAKEUP),ESP_EXT1_WAKEUP_ALL_LOW);
    esp_deep_sleep_start();
}
void app_main() {
    const esp_app_desc_t * app_desc = esp_app_get_description(); // esp_ota_get_app_description();
    // version comes back as version "v0.1-alpha-259-g371d500-dirty"
    // the v0.1-alpha seems hardcoded, setting cmake PROJECT_VER replaces the more useful git describe line
    // so we'll have to parse the commit ID out
    // or maybe just get date time as YYYYMMDDHHMMSS? 

    if(strlen(app_desc->version) > 20) {
        if(app_desc->version[strlen(app_desc->version)-1] == 'y') {
            strncpy(githash, app_desc->version + strlen(app_desc->version)-13, 7);
        } else {
            strncpy(githash, app_desc->version + strlen(app_desc->version)-7, 7);            
        }
        githash[7] = 0;
    }
    printf("Welcome to %s -- date %s time %s version %s [%s]\n", app_desc->project_name, app_desc->date, app_desc->time, app_desc->version, githash);

    for(uint8_t i=0;i<MAX_TASKS;i++) last_task_counters[i] = 0;
    check_init(&esp_event_loop_create_default, "Event");
    // TODO -- this does not properly detect DEVBOARD anymore, not a big deal for now, doesn't impact anything
    // if power init fails, we don't have blinkinlabs board, set board level to 0
    if(check_init(&power_init, "power")) {
        printf("No power IC, assuming DIY Alles\n");
        board_level = DEVBOARD; 
    }
    if(board_level == ALLES_BOARD_V2) {
        printf("Detected revB+ Alles\n");
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
    int64_t start_time = amy_sysclock();
    delay_ms(250);
    //heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

    while((!(status & WIFI_MANAGER_OK) && (status & RUNNING) )) {
        wifi_tone();
        for(uint8_t i=0;i<250;i++) { 
            if(!(status & RUNNING)) turn_off();
            delay_ms(10);
        }
        if(amy_sysclock() - start_time > (MAX_WIFI_WAIT_S*1000)) turn_off();
    }

    // We check for RUNNING as someone could have pressed power already
    if(!(status & RUNNING)) turn_off();

    // was + held down right now? if so check for updates
    if(status & UPDATE) {
        xTaskCreatePinnedToCore(&firmware_upgrade, "upgrade", 8192, NULL, 0, &upgradeTask, 0);
        while(1) {
            upgrade_tone();
            delay_ms(2000);
        }
    }


    delay_ms(500);
    amy_reset_oscs();

    // Setup the socket
    create_multicast_ipv4_socket();

    // Create the task that listens fro new incoming UDP messages (core 2)
    xTaskCreatePinnedToCore(&mcast_listen_task, ALLES_RECEIVE_TASK_NAME, ALLES_RECEIVE_TASK_STACK_SIZE, NULL, ALLES_RECEIVE_TASK_PRIORITY, &mcastTask, ALLES_RECEIVE_TASK_COREID);
    delay_ms(100);

    // Create the task that waits for UDP messages, parses them and puts them on the sequencer queue (core 1)
    xTaskCreatePinnedToCore(&esp_parse_task, ALLES_PARSE_TASK_NAME, ALLES_PARSE_TASK_STACK_SIZE, NULL, ALLES_PARSE_TASK_PRIORITY, &parseTask, ALLES_PARSE_TASK_COREID);

    // Schedule a "turning on" sound
    bleep();

    // Print free RAm
    //heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

    // Spin this core until the power off button is pressed, parsing events and making sounds
    while(status & RUNNING) {
        delay_ms(10);
    }

    // If we got here, the power off button was pressed 
    turn_off();
}

