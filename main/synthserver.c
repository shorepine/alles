#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <stddef.h>
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "driver/timer.h"
#include "driver/i2s.h"
#include "freertos/queue.h"
#include <lwip/sockets.h>
#include <string.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <math.h>
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>
#include "sineLUT.h"

#define EXAMPLE_WIFI_SSID "wifissid"
#define EXAMPLE_WIFI_PASS "password"

float frequency = 440.0;
float amplitude = 0.2;
static const char *TAG = "UDP";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int STARTED_BIT = BIT1;
#define RECEIVER_PORT_NUM 6001
char my_ip[32];

#define LUT_SIZE 4095
#define SAMPLE_RATE 44100
#define BLOCK_SIZE 256


uint16_t block[BLOCK_SIZE];
float step = 0;

void fill_audio_buffer() {
    float skip = frequency / 44100.0 * LUT_SIZE;
    if(skip < 1) skip = 1; // lowest is 10hz. 
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        float x0 = (float)sine_LUT[(uint16_t)floor(step)];
        float x1 = (float)sine_LUT[(uint16_t)(floor(step)+1) % LUT_SIZE];
        float frac = step - floor(step);
        float sample = x0 + ((x1 - x0) * frac);
        block[i] = floor(sample * amplitude);
        step = step + skip;
        if(step >= LUT_SIZE) step = step - LUT_SIZE;
    }
}



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
    .bck_io_num = 26, //this is BCK pin
    .ws_io_num = 25, // this is LRCK pin
    .data_out_num = 22, // this is DATA output pin
    .data_in_num = -1   //Not used
};

void setup_i2s(void) {
  //initialize i2s with configurations above
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  i2s_set_sample_rates((i2s_port_t)i2s_num, SAMPLE_RATE);
}


// Similar to uint32_t system_get_time(void)
uint32_t get_usec() {
  struct timeval tv;
   gettimeofday(&tv,NULL);
   return (tv.tv_sec*1000000 + tv.tv_usec);
}




void receive_thread(void *pvParameters) {
    int socket_fd;
    struct sockaddr_in sa,ra;

    int recv_data; char data_buffer[80];
    printf("1\n");
    socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
    if ( socket_fd < 0 ) {
        printf("socket call failed");
        exit(0);
    }
    printf("2\n");

    memset(&sa, 0, sizeof(struct sockaddr_in));
    ra.sin_family = AF_INET;
    ra.sin_addr.s_addr = inet_addr(my_ip);
    ra.sin_port = htons(RECEIVER_PORT_NUM);

    printf("3\n");

    /* Bind the UDP socket to the port RECEIVER_PORT_NUM and to the current
    * machines IP address (Its defined by RECEIVER_PORT_NUM).
    * Once bind is successful for UDP sockets application can operate
    * on the socket descriptor for sending or receiving data.
    */
    if (bind(socket_fd, (struct sockaddr *)&ra, sizeof(struct sockaddr_in)) == -1)
    {

        printf("Bind to Port Number %d ,IP address %s failed\n",RECEIVER_PORT_NUM,my_ip);
        close(socket_fd);
        exit(1);
    }
        printf("4\n");

    while(1) {
        recv_data = recv(socket_fd,data_buffer,sizeof(data_buffer),0);
        if(recv_data > 0)
        {
            data_buffer[recv_data] = '\0';
            printf("%s\n",data_buffer);
            frequency = atof(data_buffer);
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
            printf("s1\n");
            xEventGroupSetBits(wifi_event_group, STARTED_BIT);            
            printf("s2\n");
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            ESP_LOGD(TAG, "EVENT_SCAN_DONE");
            break;

        case SYSTEM_EVENT_STA_CONNECTED: 
            ESP_LOGD(TAG, "EVENT_STA_CONNECTED");
            printf("c1\n");
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            printf("c2\n");
            break;

        // If we fail to connect to an access point as a station, become an access point.
        case SYSTEM_EVENT_STA_DISCONNECTED:
            printf("d1\n");
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            printf("d2\n");
            ESP_LOGD(TAG, "EVENT_STA_DISCONNECTED");
            // We think we tried to connect as a station and failed! ... become
            // an access point.
            break;

        // If we connected as a station then we are done and we can stop being a
        // web server.
        case SYSTEM_EVENT_STA_GOT_IP: 
            printf("e1\n");
            sprintf(my_ip,IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
            printf("e2\n");
            xTaskCreate(&receive_thread, "receive_thread", 2048, NULL, 5, NULL);
            printf("e3\n");
            break;

        default: // Ignore the other event types
            break;
    } // Switch event

    return ESP_OK;
} // esp32_wifi_eventHandler



static void initialize_wifi(void)
{
    printf("w1\n");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    printf("w2\n");
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    printf("w3\n");
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    printf("w4\n");
    wifi_config_t wifi_config = {
        .sta = {
               .ssid = EXAMPLE_WIFI_SSID,
               .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    printf("w5\n");
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    printf("w6\n");
    ESP_ERROR_CHECK( esp_wifi_start() );
    printf("w7\n");
    ESP_ERROR_CHECK( esp_wifi_connect() );
    printf("w8\n");

}


void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    printf("Hello world!\n");

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

    printf("i2s\n");
    setup_i2s();
    printf("i2s done\n");

    printf("Sleep\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    printf("Wifi\n");

    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(esp32_wifi_eventHandler, NULL) );
    tcpip_adapter_init();

    initialize_wifi();
    printf("wait\n");
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    printf("go\n");
    size_t written = 0;
    while(1) {
        fill_audio_buffer();
        i2s_write((i2s_port_t)i2s_num, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
    }


}
