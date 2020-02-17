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
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"

#include "sineLUT.h"
#include "wifi.h"

#define SAMPLE_RATE 44100

extern void mcast_listen_task(void *pvParameters);


// FM synth stuff
#include "dx7bridge.h"
extern void dx7_init();
extern void render_samples(int16_t * buf, uint16_t len);
extern void dx7_new_note(uint8_t midi_note, uint8_t velocity, uint16_t patch);
extern void dx7_new_freq(float freq, uint8_t velocity, uint16_t patch);


//i2s configuration
int i2s_num = 0; // i2s port number
i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
     .sample_rate = SAMPLE_RATE,
     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, //I2S_CHANNEL_FMT_RIGHT_LEFT,
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

// We like a lot of LUT for sines, but maybe don't need to alloc 16384*4 bytes for a square wave
#define SINE_LUT_SIZE 16383
#define OTHER_LUT_SIZE 2047

#define BLOCK_SIZE 256
#define VOICES 10 
#define SINE 0
#define SQUARE 1
#define SAW 2
#define TRIANGLE 3
#define NOISE 4
#define FM 5
#define OFF 6


int16_t block[BLOCK_SIZE];
float step[VOICES];
uint8_t wave[VOICES];
int16_t patch[VOICES];
uint8_t midi_note[VOICES];
float frequency[VOICES];
float amplitude[VOICES];
uint8_t get_going = 0;

uint16_t ** LUT;

float freq_for_midi_note(uint8_t midi_note) {
    return 440.0*pow(2,(midi_note-57.0)/12.0);
}

void setup_luts() {
    LUT = (uint16_t **)malloc(sizeof(uint16_t*)*4);
    uint16_t * square_LUT = (uint16_t*)malloc(sizeof(uint16_t)*OTHER_LUT_SIZE);
    uint16_t * saw_LUT = (uint16_t*)malloc(sizeof(uint16_t)*OTHER_LUT_SIZE);
    uint16_t * triangle_LUT = (uint16_t*)malloc(sizeof(uint16_t)*OTHER_LUT_SIZE);

    for(uint16_t i=0;i<OTHER_LUT_SIZE;i++) {
        if(i<OTHER_LUT_SIZE/2) {
            square_LUT[i] = 0;
            triangle_LUT[i] = (uint16_t) (((float)i/(float)(OTHER_LUT_SIZE/2.0))*65535.0);
        } else {
            square_LUT[i] = 0xffff;
            triangle_LUT[i] = 65535 - ((uint16_t) (((float)(i-(OTHER_LUT_SIZE/2.0))/(float)(OTHER_LUT_SIZE/2.0))*65535.0));
        }
        saw_LUT[i] = (uint16_t) (((float)i/(float)OTHER_LUT_SIZE)*65535.0);
    }
    LUT[SINE] = (uint16_t*)sine_LUT;
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
        wave[i] = OFF;
        step[i] = 0;
        patch[i] = 0;
        midi_note[i] = 0;
        frequency[i] = 0;
        amplitude[i] = 0;
    }
}
void fill_audio_buffer() {
    // floatblock -- accumulative for mixing, -32767.0 -- 32768.0
    float floatblock[BLOCK_SIZE];
    // block -- used in interim for FM, but also what gets sent to the DAC -- -32767...32768 (wave file, int16 LE)
    int16_t block[BLOCK_SIZE];  

    // Clear out the accumulator buffer
    for(uint16_t i=0;i<BLOCK_SIZE;i++) floatblock[i] = 0;

    for(uint8_t voice=0;voice<VOICES;voice++) {
        if(wave[voice]!=OFF) { // don't waste CPU
            if(wave[voice]==FM) { // FM is special
                // we can render into int16 block just fine 
                render_samples(block, BLOCK_SIZE);

                // but then add it into floatblock
                for(uint16_t i=0;i<BLOCK_SIZE;i++) {
                    floatblock[i] = floatblock[i] + (block[i] * amplitude[voice]);
                }
            } else if(wave[voice]==NOISE) { // noise is special, just use esp_random
               for(uint16_t i=0;i<BLOCK_SIZE;i++) {
                    float sample = (int16_t) ((esp_random() >> 16) - 32768);
                    floatblock[i] = floatblock[i] + (sample * amplitude[voice]);
                }
            } else { // all other voices come from a LUT
                // Choose which LUT we're using, they are different sizes
                uint32_t lut_size = OTHER_LUT_SIZE;
                if(wave[voice]==SINE) lut_size = SINE_LUT_SIZE;

                float skip = frequency[voice] / 44100.0 * lut_size;
                for(uint16_t i=0;i<BLOCK_SIZE;i++) {
                    if(skip >= 1) { // skip compute if frequency is < 3Hz
                        uint16_t u0 = LUT[wave[voice]][(uint16_t)floor(step[voice])];
                        uint16_t u1 = LUT[wave[voice]][(uint16_t)(floor(step[voice])+1 % lut_size)];
                        float x0 = (float)u0 - 32768.0;
                        float x1 = (float)u1 - 32768.0;
                        float frac = step[voice] - floor(step[voice]);
                        float sample = x0 + ((x1 - x0) * frac);
                        floatblock[i] = floatblock[i] + (sample * amplitude[voice]);
                        step[voice] = step[voice] + skip;
                        if(step[voice] >= lut_size) step[voice] = step[voice] - lut_size;
                    }
                }
            }
        }
    }
    // Now make it a signed int16 for the i2s
    for(uint16_t i=0;i<BLOCK_SIZE;i++) {
        block[i] = (int16_t)floatblock[i];
    }
    // And write
    size_t written = 0;
    i2s_write((i2s_port_t)i2s_num, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
}


void setup_i2s(void) {
  //initialize i2s with configurations above
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  i2s_set_sample_rates((i2s_port_t)i2s_num, SAMPLE_RATE);
}

void handle_sync(uint64_t sync) {
    int64_t last_sync = -1;
    int64_t esp_time_at_sync = -1;
    uint16_t delta_sync = 100;    
    // Do something here with
    int64_t ms_since_boot = esp_timer_get_time() / 1000;
}

void parse_message(char * data_buffer, int recv_data) {
    uint8_t mode = 0;
    uint16_t start = 0;
    data_buffer[recv_data] = 0;
    uint16_t c = 0;
    int16_t t_voice = 0;
    int32_t t_sync = -1;
    int16_t t_note = -1;
    int16_t t_wave = -1;
    int16_t t_patch = -1;
    float t_freq = -1;
    float t_amp = -1;
    while(c < recv_data+1) {
        uint8_t b = data_buffer[c];
        if(b >= 'a' || b <= 'z' || b == 0) {  // new mode or end
            if(mode=='s') t_sync=atoi(data_buffer + start);
            if(mode=='v') t_voice=atoi(data_buffer + start);
            if(mode=='n') t_note=atoi(data_buffer + start);
            if(mode=='w') t_wave=atoi(data_buffer + start);
            if(mode=='p') t_patch=atoi(data_buffer + start);
            if(mode=='f') t_freq=atof(data_buffer + start);
            if(mode=='a') t_amp=atof(data_buffer + start);
            mode=b;
            start=c+1;
        }
        c++;
    }
    // Now we have the whole message parsed and figured out what voice we are, make changes
    // Note change triggers a freq change, but not the other way around (i think that's good)
    // Sync is separate, and only ever comes in on its own 
    if(t_sync >= 0) { handle_sync(t_sync); } else {
        if(t_note >= 0) { midi_note[t_voice] = t_note; frequency[t_voice] = freq_for_midi_note(t_note); } 
        if(t_wave >= 0) wave[t_voice] = t_wave;
        if(t_patch >= 0) patch[t_voice] = t_patch;
        if(t_freq >= 0) frequency[t_voice] = t_freq;
        if(t_amp >= 0) amplitude[t_voice] = t_amp;
        // Trigger a new note for FM / env? Obv rethink all of this, an env command?
        // For now, trigger a new note on every param change for FM
        if(wave[t_voice]==FM) {
            if(midi_note[t_voice]>0) {
                dx7_new_note(midi_note[t_voice], 100, patch[t_voice]);
            } else {
                dx7_new_freq(frequency[t_voice], 100, patch[t_voice]);
            }
        }
        printf("voice %d wave %d amp %f freq %f note %d patch %d\n", t_voice, wave[t_voice], amplitude[t_voice], frequency[t_voice], midi_note[t_voice], patch[t_voice]);
    }
}


void app_main() {
    // The flash has get init'd even though we're not using it as some wifi stuff is stored in there
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

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

    printf("Setting up wifi & multicast listening\n");
    ESP_ERROR_CHECK(wifi_connect());
    xTaskCreate(&mcast_listen_task, "mcast_task", 4096, NULL, 5, NULL);
    printf("wifi ready\n");


    setup_luts();
    setup_voices();
    printf("oscillators ready\n");
    dx7_init();
    printf("FM ready\n");

    // Bleep to confirm we're online
    uint16_t cycles = 0.25 / ((float)BLOCK_SIZE/SAMPLE_RATE);
    amplitude[0] = 0.8;
    wave[0] = SINE;
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

