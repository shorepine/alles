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
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

#include "sineLUT.h"
#include "auth.h"
// has 
// #define WIFI_SSID "wifissid"
// #define WIFI_PASS "password"

static const char *TAG = "UDP";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int STARTED_BIT = BIT1;
#define RECEIVER_PORT_NUM 6001
char my_ip[32];

#define SAMPLE_RATE 44100

//i2s configuration
int i2s_num = 0; // i2s port number
i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
     .sample_rate = SAMPLE_RATE,
     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
     .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
     .dma_buf_count = 8,
     .dma_buf_len = 64   //Interrupt level 1
    };
    
i2s_pin_config_t pin_config = {
    .bck_io_num = 26, //this is BCK pin, to "A0" on the adafruit feather 
    .ws_io_num = 25, //this is LRCK pin, to "A1" on the adafruit feather
    .data_out_num = 4, // this is DATA output pin, to "A5" on the feather
    .data_in_num = -1   //Not used
};

#define LUT_SIZE 4095
#define BLOCK_SIZE 256
#define VOICES 6 // we get buffer underruns after 6 voices. could increase cpu clock? 2nd cpu? 
#define SINE 0
#define SQUARE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4

int16_t block[BLOCK_SIZE];
float step[VOICES];
uint8_t wave[VOICES];
float frequency[VOICES];
float amplitude[VOICES];
uint8_t get_going = 0;

uint16_t ** LUT;

void setup_luts() {
    LUT = (uint16_t **)malloc(sizeof(uint16_t*)*4);
    uint16_t * square_LUT = (uint16_t*)malloc(sizeof(uint16_t)*LUT_SIZE);
    uint16_t * saw_LUT = (uint16_t*)malloc(sizeof(uint16_t)*LUT_SIZE);
    uint16_t * triangle_LUT = (uint16_t*)malloc(sizeof(uint16_t)*LUT_SIZE);

    for(uint16_t i=0;i<LUT_SIZE;i++) {
        if(i<LUT_SIZE/2) {
            square_LUT[i] = 0;
            triangle_LUT[i] = (uint16_t) (((float)i/(float)LUT_SIZE/2)*65535.0);
        } else {
            square_LUT[i] = 0xffff;
            triangle_LUT[i] = 65535 - ((uint16_t) (((float)(i-(LUT_SIZE/2))/(float)LUT_SIZE/2)*65535.0));
        }
        saw_LUT[i] = (uint16_t) (((float)i/(float)LUT_SIZE)*65535.0);
    }
    LUT[SINE] = sine_LUT;
    LUT[SQUARE] = square_LUT;
    LUT[SAW] = saw_LUT;
    LUT[TRIANGLE] = triangle_LUT;
}

void destroy_luts() {
    free(LUT[SQUARE]);
    free(LUT[SAW]);
    free(LUT[TRIANGLE]);
    free(LUT);
}

void setup_voices() {
    for(int i=0;i<VOICES;i++) {
        wave[i] = SINE;
        step[i] = 0;
        frequency[i] = 0;
        amplitude[i] = 0;
    }
}
void fill_audio_buffer() {
    float floatblock[BLOCK_SIZE];
    int16_t block[BLOCK_SIZE];

    // Clear out the accumulator buffer
    for(uint16_t i=0;i<BLOCK_SIZE;i++) floatblock[i] = 0;

    for(uint8_t voice=0;voice<VOICES;voice++) {
        if(wave[voice]==NOISE) { // noise is special 
           for(uint16_t i=0;i<BLOCK_SIZE;i++) {
                float sample = (uint16_t) (esp_random() >> 16);
                floatblock[i] = floatblock[i] + (sample * amplitude[voice]);
            }
        } else { // all other voices come from a LUT
            float skip = frequency[voice] / 44100.0 * LUT_SIZE;
            for(uint16_t i=0;i<BLOCK_SIZE;i++) {
                if(skip >= 1) { // skip compute if frequency is < 10hz
                    float x0 = (float)LUT[wave[voice]][(uint16_t)floor(step[voice])];
                    float x1 = (float)LUT[wave[voice]][(uint16_t)(floor(step[voice])+1) % LUT_SIZE];
                    float frac = step[voice] - floor(step[voice]);
                    float sample = x0 + ((x1 - x0) * frac);
                    floatblock[i] = floatblock[i] + (sample * amplitude[voice]);
                    step[voice] = step[voice] + skip;
                    if(step[voice] >= LUT_SIZE) step[voice] = step[voice] - LUT_SIZE;
                }
            }
        }
    }
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        floatblock[i] = floatblock[i] - 32767.0;
        block[i] = (int16_t)floatblock[i];
    }
    size_t written = 0;
    i2s_write((i2s_port_t)i2s_num, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
}


void setup_i2s(void) {
  //initialize i2s with configurations above
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  i2s_set_sample_rates((i2s_port_t)i2s_num, SAMPLE_RATE);
}


void receive_thread(void *pvParameters) {
    int socket_fd;
    struct sockaddr_in sa,ra;

    int recv_data; char data_buffer[80];
    socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if ( socket_fd < 0 ) {
        printf("socket call failed");
        exit(0);
    }

    memset(&sa, 0, sizeof(struct sockaddr_in));
    ra.sin_family = AF_INET;
    ra.sin_addr.s_addr = inet_addr(my_ip);
    ra.sin_port = htons(RECEIVER_PORT_NUM);
    if (bind(socket_fd, (struct sockaddr *)&ra, sizeof(struct sockaddr_in)) == -1) {
        printf("Bind to Port Number %d ,IP address %s failed\n",RECEIVER_PORT_NUM,my_ip);
        close(socket_fd);
        exit(1);
    }

    // Spin forever in this thread waiting for commands
    while(1) {
        recv_data = recv(socket_fd,data_buffer,sizeof(data_buffer),0);
        if(recv_data > 0) {
            data_buffer[recv_data] = '\0';
            uint8_t voice = data_buffer[0]-48;
            wave[voice] = data_buffer[2]-48;
            amplitude[voice] = atof(data_buffer+4);
            // Look for frequency, not required
            for(uint8_t k=5;k<recv_data;k++) {
                if(data_buffer[k] == 0) k = recv_data; // skip to the end, Max's udpsend sends footer data after a NULL
                if(data_buffer[k] == ',') {
                    frequency[voice] = atof(data_buffer+k+1);
                }
            }
            //printf("voice %d wave %d amp %f freq %f\n", voice, wave[voice], amplitude[voice], frequency[voice]);
        }
    }
    close(socket_fd); 

}

static esp_err_t esp32_wifi_eventHandler(void *ctx, system_event_t *event) {

    switch(event->event_id) {
        case SYSTEM_EVENT_WIFI_READY:
            ESP_LOGD(TAG, "EVENT_WIFI_READY");

            break;

        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGD(TAG, "EVENT_AP_START");
            break;

        // When we have started being an access point
        case SYSTEM_EVENT_AP_START: 
            ESP_LOGD(TAG, "EVENT_START");
            xEventGroupSetBits(wifi_event_group, STARTED_BIT);            
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            ESP_LOGD(TAG, "EVENT_SCAN_DONE");
            break;

        case SYSTEM_EVENT_STA_CONNECTED: 
            ESP_LOGD(TAG, "EVENT_STA_CONNECTED");
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;

        // If we fail to connect to an access point as a station, become an access point.
        case SYSTEM_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            ESP_LOGD(TAG, "EVENT_STA_DISCONNECTED");
            // We think we tried to connect as a station and failed! ... become
            // an access point.
            break;

        // If we connected as a station then we are done and we can stop being a
        // web server.
        case SYSTEM_EVENT_STA_GOT_IP: 
            sprintf(my_ip,IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
            xTaskCreate(&receive_thread, "receive_thread", 2048, NULL, 5, NULL);
            get_going = 1;
            break;

        default: // Ignore the other event types
            break;
    } // Switch event

    return ESP_OK;
} // esp32_wifi_eventHandler



static void initialize_wifi(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
               .ssid = WIFI_SSID,
               .password = WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_connect() );
}


void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Setting up I2S\n");
    setup_i2s();

    printf("Setting up wifi\n");

    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(esp32_wifi_eventHandler, NULL) );
    tcpip_adapter_init();

    initialize_wifi();
    printf("Waiting for wifi to connect\n");
    while(!get_going) vTaskDelay(200 / portTICK_PERIOD_MS);

    printf("Ready\n");
    // Setup the oscillators
    setup_luts();
    setup_voices();

    // Bleep to confirm we're online
    uint16_t cycles = 0.25 / ((float)BLOCK_SIZE/SAMPLE_RATE);
    amplitude[0] = 0.05;
    for(uint8_t i=0;i<cycles;i++) {
        if(i<cycles/2) {
            frequency[0] = 220;
        } else {
            frequency[0] = 440;
        }
        fill_audio_buffer();
    } 
    // Reset the voices and go forever, waiting for commands on the UDP thread
    setup_voices();
    while(1) fill_audio_buffer();

    // We will never get here but just in case
    destroy_luts();


}
