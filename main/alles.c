#include "alles.h"

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
    // TOOD: Destroy FM and all the ram. low-pri, we never get here so ... 
}

void setup_voices() {
    // This inits all 10 FM voices
    fm_init();
    // This inits the oscillators to 0
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
                render_fm_samples(block, BLOCK_SIZE, voice);

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

// Sync to me is a host sending a bunch of UDP packets to everyone
// I receive N of them, and average them out, then send something back to the host
// The host can figure out my latency, give me an ID, find out how many there are, etc
// And then what? how do i compute latency for sequencing?

// Let's first re-factor to get a sequencer in here and do 1-unit 200ms latency timing
// that's t_time

void handle_sync(uint64_t sync) {
/*
    int64_t last_sync = -1;
    int64_t esp_time_at_sync = -1;
    uint16_t delta_sync = 100;    
    // Do something here with
    int64_t ms_since_boot = esp_timer_get_time() / 1000;
    */
}

/*
void parse_message(char * data_buffer, int recv_data) {
    uint8_t mode = 0;
    uint16_t start = 0;
    data_buffer[recv_data] = 0;
    uint16_t c = 0;
    int16_t t_voice = 0;
    int32_t t_sync = -1;
    int32_t t_time = -1;
    int16_t t_note = -1;
    int16_t t_wave = -1;
    int16_t t_patch = -1;
    float t_freq = -1;
    float t_amp = -1;
    while(c < recv_data+1) {
        uint8_t b = data_buffer[c];
        if(b >= 'a' || b <= 'z' || b == 0) {  // new mode or end
            if(mode=='t') t_time=atoi(data_buffer + start);
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
    if(t_time >= 0) { // do something with time

    }
    if(t_sync >= 0) { handle_sync(t_sync); } 
    if(t_note >= 0) { midi_note[t_voice] = t_note; frequency[t_voice] = freq_for_midi_note(t_note); } 
    if(t_wave >= 0) wave[t_voice] = t_wave;
    if(t_patch >= 0) patch[t_voice] = t_patch;
    if(t_freq >= 0) frequency[t_voice] = t_freq;
    if(t_amp >= 0) amplitude[t_voice] = t_amp;
    // Trigger a new note for FM / env? Obv rethink all of this, an env command?
    // For now, trigger a new note on every param change for FM
    if(wave[t_voice]==FM) {
        if(midi_note[t_voice]>0) {
            fm_new_note_number(midi_note[t_voice], 100, patch[t_voice], t_voice);
        } else {
            fm_new_note_freq(frequency[t_voice], 100, patch[t_voice], t_voice);
        }
    }
    printf("voice %d wave %d amp %f freq %f note %d patch %d\n", t_voice, wave[t_voice], amplitude[t_voice], frequency[t_voice], midi_note[t_voice], patch[t_voice]);
    
}
*/

// My dumb FIFO / circular buffer impl

#define EMPTY 0
#define PLAYED 1
#define SCHEDULED 2
#define LATENCY_MS 200

int64_t computed_delta = 0; // can be negative no prob, but usually host is larger # than client
uint8_t computed_delta_set = 0; // have we set a delta yet?

// Here, some events
struct event {
    uint64_t time;
    uint64_t sync;
    int16_t voice;
    int16_t wave;
    int16_t patch;
    int16_t midi_note;
    float amp;
    float freq;
    uint8_t status;
};

#define EVENT_FIFO_LEN 100
int16_t next_event_write;
struct event events[EVENT_FIFO_LEN];

void setup_events() {
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        events[i].status = EMPTY;
        events[i].time = 0;
        events[i].voice = 0;
        events[i].sync = -1;
        events[i].patch = -1;
        events[i].wave = -1;
        events[i].midi_note = -1;
        events[i].amp = -1;
        events[i].freq = -1;
    }
    next_event_write = 0;
}

// my fifo is like
// new message goes into 0, read ptr goes to 0, write to 1
// next new msg to 1, read at 0
// next 2, 0
// 3, 0
// now a read starts at 0
// reading goes until status is no longer scheduled
// we check time of each entry to see if it's time yet


// A replacement for "parse messages" -- instead of parsing into audio_buffer changes,
// parse into a FIFO of messages that the sequencer will trigger, neat
void parse_message_into_events(char * data_buffer, int recv_data) {
    uint8_t mode = 0;
    uint16_t start = 0;
    data_buffer[recv_data] = 0;
    uint16_t c = 0;
    struct event e;
    int64_t sysclock = esp_timer_get_time() / 1000;

    while(c < recv_data+1) {
        uint8_t b = data_buffer[c];
        if(b >= 'a' || b <= 'z' || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atoi(data_buffer + start);
                // if we haven't yet synced our times, do it now
                if(!computed_delta_set) {
                    computed_delta = e.time - sysclock;
                    computed_delta_set = 1;
                }
            }
            if(mode=='s') e.sync=atoi(data_buffer + start);
            if(mode=='v') e.voice=atoi(data_buffer + start);
            if(mode=='n') e.midi_note=atoi(data_buffer + start);
            if(mode=='w') e.wave=atoi(data_buffer + start);
            if(mode=='p') e.patch=atoi(data_buffer + start);
            if(mode=='f') e.freq=atof(data_buffer + start);
            if(mode=='a') e.amp=atof(data_buffer + start);
            mode=b;
            start=c+1;
        }
        c++;
    }

    // so 1st message is host 1000, client 100
    // computed delta is 900.
    // first event goes out at 1000-900 + 200 = 300ms (200ms away)
    // next message comes in for host 1100, client is now at 250 (took some time to get it)
    // second event goes out at 1100-900 + 200 = 400ms (150ms away)
    // third event comes in at client 450, no host time
    // just gets played at 650 
    

    // Now adjust time in some useful way:
    // if we have a delta & got a time in this message, use it schedule it properly
    if(computed_delta_set && e.time > 0) {
        e.time = (e.time - computed_delta) + LATENCY_MS;
    } else { // else play it asap 
        e.time = sysclock + LATENCY_MS;
    }

    e.status = SCHEDULED;
    events[next_event_write] = e;
    next_event_write = (next_event_write + 1) % EVENT_FIFO_LEN;
}


// Play an event, now -- tell the audio thread to start making noise
void play_event(struct event e) {
    if(e.midi_note >= 0) { midi_note[e.voice] = e.midi_note; frequency[e.voice] = freq_for_midi_note(e.midi_note); } 
    if(e.wave >= 0) wave[e.voice] = e.wave;
    if(e.patch >= 0) patch[e.voice] = e.patch;
    if(e.freq >= 0) frequency[e.voice] = e.freq;
    if(e.amp >= 0) amplitude[e.voice] = e.amp;
    if(wave[e.voice]==FM) {
        if(midi_note[e.voice]>0) {
            fm_new_note_number(midi_note[e.voice], 100, patch[e.voice], e.voice);
        } else {
            fm_new_note_freq(frequency[e.voice], 100, patch[e.voice], e.voice);
        }
    }
}


void sequencer_task(void *pvParameters) {
    // i spin forever, processing sequencer events parsed above 
    // i turn them into audio_buffer commands at the right times
    while(1) {
        int64_t sysclock = esp_timer_get_time() / 1000;
        // We could save some CPU by starting at a read pointer, depends on how big this gets
        for(uint16_t i=0;i<EVENT_FIFO_LEN;i++) {
            if(events[i].status == SCHEDULED) {
                // By now event.time is corrected to our sysclock (from the host)
                if(events[i].time >= sysclock) {
                    // time to play
                    play_event(events[i]);
                    events[i].status = EMPTY;
                }
            }
        }
        // Maybe a 1ms sleep here
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


    // reset the voices & events
    setup_voices();
    setup_events();

    // Create the sequencer thread
    xTaskCreate(&sequencer_task, "sequencer_task", 4096, NULL, 5, NULL);

    // Fill the audio buffer based on what the sequencer says.
    while(1) fill_audio_buffer();

    // We will never get here but just in case
    destroy_luts();


}

