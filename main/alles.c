#include "alles.h"
#include "sineLUT.h"

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
    .data_out_num = 27,//4, // this is DATA output pin, to "A5" on the feather
    .data_in_num = -1   //Not used
};

int16_t block[BLOCK_SIZE];
float step[VOICES];
uint8_t wave[VOICES];
int16_t patch[VOICES];
uint8_t midi_note[VOICES];
float frequency[VOICES];
float amplitude[VOICES];
uint16_t ** LUT;

int64_t computed_delta = 0; // can be negative no prob, but usually host is larger # than client
uint8_t computed_delta_set = 0; // have we set a delta yet?


struct event {
    uint64_t time;
    int16_t voice;
    int16_t wave;
    int16_t patch;
    int16_t midi_note;
    float amp;
    float freq;
    uint8_t status;
};

int16_t next_event_write = 0;
struct event events[EVENT_FIFO_LEN];
uint8_t client_id = 0;

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

void destroy() {
    free(LUT[SQUARE]);
    free(LUT[SAW]);
    free(LUT[TRIANGLE]);
    free(LUT);
    // TOOD: Destroy FM and all the ram. low-pri, we never get here so ... 
}

void setup_voices() {
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



void fill_audio_buffer() {
    // floatblock -- accumulative for mixing, -32767.0 -- 32768.0
    float floatblock[BLOCK_SIZE];
    // block -- used in interim for FM, but also what gets sent to the DAC -- -32767...32768 (wave file, int16 LE)
    int16_t block[BLOCK_SIZE];  


    // Go forever
    while(1) {
        // Check to see which sounds to play 
        int64_t sysclock = esp_timer_get_time() / 1000;
        // We could save some CPU by starting at a read pointer, depends on how big this gets
        for(uint16_t i=0;i<EVENT_FIFO_LEN;i++) {
            if(events[i].status == SCHEDULED) {
                // By now event.time is corrected to our sysclock (from the host)
                if(sysclock >= events[i].time) {
                    play_event(events[i]);
                    events[i].status = PLAYED;
                }
            }
        }

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
            // Clip 
            if(floatblock[i] > 32767) floatblock[i] = 32767;
            if(floatblock[i] < -32768) floatblock[i] = -32768;
            block[i] = (int16_t)floatblock[i];
        }
        // And write
        size_t written = 0;
        i2s_write((i2s_port_t)i2s_num, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
    }
}


void setup_i2s(void) {
  //initialize i2s with configurations above
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  i2s_set_sample_rates((i2s_port_t)i2s_num, SAMPLE_RATE);
}


// Create a new default event
struct event default_event() {
    struct event e;
    e.status = EMPTY;
    e.time = 0;
    e.voice = 0;
    e.patch = -1;
    e.wave = -1;
    e.midi_note = -1;
    e.amp = -1;
    e.freq = -1;
    return e;
}

// deep copy an event to the fifo at index
// if index < 0, use the write pointer (and incremement it)
void add_event(struct event e, int16_t index) { 
    if(index < 0) { 
        index = next_event_write;
        next_event_write = (next_event_write + 1) % (EVENT_FIFO_LEN);
    }
    events[index].voice = e.voice;
    events[index].midi_note = e.midi_note;
    events[index].wave = e.wave;
    events[index].patch = e.patch;
    events[index].freq = e.freq;
    events[index].amp = e.amp;
    events[index].time = e.time;
    events[index].status = e.status;
}

// Fill the FIFO with default events, as the audio thread reads from it immediately
void setup_events() {
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        add_event(default_event(), i);
    }
}

void handle_sync(int64_t time, uint8_t index) {
    // I am called when I get an s message, which comes along with host time
    // I am normally called N times in a row, probably like 10? with 100ms divisons inbetween
    // of course, i may miss one, so i'm also called with an index
    int64_t sysclock = esp_timer_get_time() / 1000;
    char message[100];
    // Send back sync message with my time and received sync index and my client id
    sprintf(message, "_s%lldi%dc%d", sysclock, index, client_id);
    mcast_send(message, strlen(message));
    // Update computed delta (i could average these out, but I don't think that'll help too much)
    computed_delta = time - sysclock;
    computed_delta_set = 1;
}


// A replacement for "parse messages" -- instead of parsing into audio_buffer changes,
// parse into a FIFO of messages that the sequencer will trigger, neat
void parse_message_into_events(char * data_buffer, int recv_data) {
    uint8_t mode = 0;
    int64_t sync = -1;
    int8_t sync_index = -1;
    int16_t client = -1;
    uint16_t start = 0;
    uint16_t c = 0;
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;

    // Put a null at the end for atoi
    data_buffer[recv_data] = 0;
    // Skip this if the message starts with _ (an ack message for sync)
    if(recv_data>0) if(data_buffer[0]=='_') recv_data = -1;

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
            if(mode=='c') client = atoi(data_buffer + start); 
            if(mode=='s') sync = atoi(data_buffer + start); 
            if(mode=='i') sync_index = atoi(data_buffer + start);
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
    // Only do this if we got some data
    if(recv_data >0) {
        // Now adjust time in some useful way:
        // if we have a delta & got a time in this message, use it schedule it properly
        if(computed_delta_set && e.time > 0) {
            e.time = (e.time - computed_delta) + LATENCY_MS;
        } else { // else play it asap 
            e.time = sysclock + LATENCY_MS;
        }
        e.status = SCHEDULED;

        // Don't add sync messages to the event queue
        if(sync >= 0 && sync_index >= 0) {
            handle_sync(sync, sync_index);
        } else {
            // Assume it's for me
            uint8_t for_me = 1;
            // But wait, they specified, so don't assume
            if(client >= 0) {
                for_me = 0;
                // It's actually precisely for me
                if(client == client_id) for_me = 1;
                if(client > 255) {
                    // It's a group message, see if i'm in the group
                    if(client_id % (client-255) == 0) for_me = 1;
                }
            }
            if(for_me) add_event(e, -1);
        }
    }
}

// Schedule a bleep now
void bleep() {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    e.time = sysclock;
    e.wave = SINE;
    e.freq = 220;
    e.amp = 0.75;
    e.status = SCHEDULED;
    add_event(e, -1);
    e.time = sysclock + 150;
    e.freq = 440;
    add_event(e, -1);
    e.time = sysclock + 300;
    e.amp = 0;
    e.freq = 0;
    add_event(e, -1);
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
    create_multicast_ipv4_socket();
    xTaskCreate(&mcast_listen_task, "mcast_task", 4096, NULL, 5, NULL);
    printf("wifi ready\n");
    client_id =esp_ip4_addr4(&s_ip_addr);
    setup_luts();
    setup_voices();
    setup_events();
    printf("oscillators ready\n");
    bleep();

    // TODO -- udp packets will starve this -- figure out priority 
    while(1) fill_audio_buffer();
    
    // We will never get here but just in case
    destroy();


}

