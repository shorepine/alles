// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"

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
    .bck_io_num = 26,   //this is BCK pin 
    .ws_io_num = 25,    //this is LRCK pin
    .data_out_num = 27, // this is DIN 
    .data_in_num = -1   //Not used
};


// Synth globals for timing and client


struct event {
    uint64_t time;
    int16_t voice;
    int16_t wave;
    int16_t patch;
    int16_t midi_note;
    float amp;
    float duty;
    float feedback;
    float freq;
    uint8_t status;
    int8_t velocity;
};

int16_t next_event_write = 0;
// One set of events for the fifo
struct event events[EVENT_FIFO_LEN];
// And another event per voice as multi-channel sequencer that the scheduler renders into
struct event sequencer[VOICES];


float freq_for_midi_note(uint8_t midi_note) {
    return 440.0*pow(2,(midi_note-69.0)/12.0);
}


void destroy() {
    // TODO: Destroy FM and all the ram. low-pri, we never get here so ... 
}

// Create a new default event -- mostly -1 or no change
struct event default_event() {
    struct event e;
    e.status = EMPTY;
    e.time = 0;
    e.voice = 0;
    e.patch = -1;
    e.wave = -1;
    e.duty = -1;
    e.feedback = -1;
    e.velocity = -1;
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
    events[index].velocity = e.velocity;
    events[index].duty = e.duty;
    events[index].feedback = e.feedback;
    events[index].midi_note = e.midi_note;
    events[index].wave = e.wave;
    events[index].patch = e.patch;
    events[index].freq = e.freq;
    events[index].amp = e.amp;
    events[index].time = e.time;
    events[index].status = e.status;
}

// The sequencer object keeps state betweeen voices, whereas events are only deltas/changes
void setup_voices() {
    fm_init();
    oscillators_init();
    for(int i=0;i<VOICES;i++) {
        sequencer[i].wave = OFF;
        sequencer[i].duty = 0.5;
        sequencer[i].patch = 0;
        sequencer[i].midi_note = 0;
        sequencer[i].freq = 0;
        sequencer[i].feedback = 0.996;
        sequencer[i].amp = 0;
        sequencer[i].velocity = 100;
    }

    // Fill the FIFO with default events, as the audio thread reads from it immediately
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        add_event(default_event(), i);
    }
}


// Play an event, now -- tell the audio loop to start making noise
void play_event(struct event e) {
    if(e.midi_note >= 0) { sequencer[e.voice].midi_note = e.midi_note; sequencer[e.voice].freq = freq_for_midi_note(e.midi_note); } 
    if(e.wave >= 0) sequencer[e.voice].wave = e.wave;
    if(e.patch >= 0) sequencer[e.voice].patch = e.patch;
    if(e.duty >= 0) sequencer[e.voice].duty = e.duty;
    if(e.feedback >= 0) sequencer[e.voice].feedback = e.feedback;
    if(e.velocity >= 0) sequencer[e.voice].velocity = e.velocity;
    if(e.freq >= 0) sequencer[e.voice].freq = e.freq;
    if(e.amp >= 0) sequencer[e.voice].amp = e.amp;

    // Triggers / envelopes -- this needs some more thinking
    if(sequencer[e.voice].wave==FM) {
        if(sequencer[e.voice].midi_note>0) {
            fm_new_note_number(sequencer[e.voice].midi_note, 
                                sequencer[e.voice].velocity, 
                                sequencer[e.voice].patch, 
                                e.voice);
        } else {
            fm_new_note_freq(sequencer[e.voice].freq, 
                                sequencer[e.voice].velocity, 
                                sequencer[e.voice].patch, 
                                e.voice);
        }
    }
    if(sequencer[e.voice].wave==KS) {
        ks_new_note_freq(sequencer[e.voice].freq, e.voice);
    }
}


// floatblock -- accumulative for mixing, -32767.0 -- 32768.0
float floatblock[BLOCK_SIZE];
// block -- what gets sent to the DAC -- -32767...32768 (wave file, int16 LE)
int16_t block[BLOCK_SIZE];  


// This takes scheduled events and plays them at the right time
void fill_audio_buffer() {
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
        switch(sequencer[voice].wave) {
            case FM:
                render_fm(floatblock, BLOCK_SIZE, voice, sequencer[voice].amp);
                break;
            case NOISE:
                render_noise(floatblock, BLOCK_SIZE, sequencer[voice].amp);
                break;
            case SAW:
                render_saw(floatblock, BLOCK_SIZE, voice, sequencer[voice].freq, sequencer[voice].amp);
                break;
            case PULSE:
                render_pulse(floatblock, BLOCK_SIZE, voice, sequencer[voice].freq, sequencer[voice].duty, sequencer[voice].amp);
                break;
            case TRIANGLE:
                render_triangle(floatblock, BLOCK_SIZE, voice, sequencer[voice].freq, sequencer[voice].amp);
                break;                
            case SINE:
                render_sine(floatblock, BLOCK_SIZE, voice, sequencer[voice].freq, sequencer[voice].amp);
                break;
            case KS:
                render_ks(floatblock, BLOCK_SIZE, voice, sequencer[voice].freq, sequencer[voice].feedback, sequencer[voice].amp);
                break;

        }
    }
    // Bandlimt the buffer all at once
    blip_the_buffer(floatblock, block, BLOCK_SIZE);

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







// parse a received UDP message into a FIFO of messages that the sequencer will trigger
void parse_message_into_events(char * data_buffer, int recv_data) {
    uint8_t mode = 0;
    int64_t sync = -1;
    int8_t sync_index = -1;
    int8_t ipv4 = 0;
    int16_t client = -1;
    uint16_t start = 0;
    uint16_t c = 0;
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    uint8_t sync_response = 0;
    // Put a null at the end for atoi
    data_buffer[recv_data] = 0;

    // Cut the OSC cruft Max etc add, they put a 0 and then more things after the 0
    int new_recv_data = recv_data; 
    for(int d=0;d<recv_data;d++) { if(data_buffer[d] == 0) { new_recv_data = d; d = recv_data + 1;  } }
    recv_data = new_recv_data;

    //printf("message ###%s### len %d\n", data_buffer, recv_data);

    while(c < recv_data+1) {
        uint8_t b = data_buffer[c];
        if(b == '_' && c==0) sync_response = 1;
        if(b >= 'a' || b <= 'z' || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atoi(data_buffer + start);
                // if we haven't yet synced our times, do it now
                if(!computed_delta_set) {
                    computed_delta = e.time - sysclock;
                    computed_delta_set = 1;
                }
            }
            if(mode=='a') e.amp=atof(data_buffer + start);
            if(mode=='b') e.feedback=atof(data_buffer+start);
            if(mode=='c') client = atoi(data_buffer + start); 
            if(mode=='d') e.duty=atof(data_buffer + start);
            if(mode=='e') e.velocity=atoi(data_buffer + start);
            if(mode=='f') e.freq=atof(data_buffer + start);
            if(mode=='i') sync_index = atoi(data_buffer + start);
            if(mode=='n') e.midi_note=atoi(data_buffer + start);
            if(mode=='p') e.patch=atoi(data_buffer + start);
            if(mode=='r') ipv4=atoi(data_buffer + start);
            if(mode=='s') sync = atoi(data_buffer + start); 
            if(mode=='v') e.voice=atoi(data_buffer + start);
            if(mode=='w') e.wave=atoi(data_buffer + start);
            mode=b;
            start=c+1;
        }
        c++;
    }
    if(sync_response) {
        // If this is a sync response, let's update our local map of who is booted
        update_map(client, ipv4, sync);
        recv_data = 0; // don't need to do the rest
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
                if(client <= 255) {
                    // If they gave an individual client ID check that it exists
                    if(alive>0) { // alive may get to 0 in a bad situation, and will reboot the box here div0
                        if(client >= alive) {
                            client = client % alive;
                        } 
                    }
                }
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

// Plays a scale in the test program
void scale(uint8_t wave, float vol) {
    struct event e = default_event();
    int64_t sysclock = esp_timer_get_time() / 1000;
    for(uint8_t i=0;i<12;i++) {
        e.time = sysclock + (i*250);
        e.wave = wave;
        e.midi_note = 48+i;
        e.amp = vol;
        e.status = SCHEDULED;
        add_event(e, -1);
    }
}

// Run forever playing scales with different oscillators
void test_sounds() {
    scale(SINE, 0.5);
    int64_t sysclock = esp_timer_get_time() / 1000;
    uint8_t type = 0;
    while(1) {
        fill_audio_buffer();
        if(esp_timer_get_time() / 1000 - sysclock > 3000) { // 3 seconds
            sysclock = esp_timer_get_time() / 1000;
            if(type==0) scale(PULSE, 0.1);
            if(type==1) scale(TRIANGLE, 0.1);
            if(type==2) scale(SAW, 0.5);
            if(type==3) scale(FM, 0.5);
            if(type==4) scale(KS, 0.5);
            if(type==5) scale(SINE, 0.9);
            if(type==6) scale(PULSE, 0.5);
            if(type==7) scale(FM, 0.9);
            if(type==8) scale(NOISE, 0.2);
            if(type==9) scale(KS, 1);
            if(type==10) type = 0;
            type++;
        }
    }
}

void app_main() {
    // Init flash, network, event loop, GPIO
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // This is the "BOOT" pin on the devboards -- GPIO0
    gpio_set_direction(GPIO_NUM_0,  GPIO_MODE_INPUT);
    gpio_pullup_en(GPIO_NUM_0);

    setup_i2s();
    setup_voices();

    vTaskDelay(100*2); // wait 2 seconds to see if button is pressed
    if(!gpio_get_level(GPIO_NUM_0)) {
        // play a test thing forever if the button was pressed
        test_sounds();
    }
    
    // else start the main loop 
    ESP_ERROR_CHECK(wifi_connect());
    create_multicast_ipv4_socket();

    // Pin the UDP task to the 2nd core so the audio / main core runs on its own without getting starved
    xTaskCreatePinnedToCore(&mcast_listen_task, "mcast_task", 4096, NULL, 5, NULL, 1);

    printf("Synth running on core %d\n", xPortGetCoreID());
    bleep();

    // Spin this core forever parsing events and making sounds
    while(1) fill_audio_buffer();
    
    // We will never get here but just in case
    destroy();


}

