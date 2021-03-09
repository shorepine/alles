// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"

// Keep this on if you are using the blinkinlabs board
#define ALLES_V1_BOARD

#ifdef ALLES_V1_BOARD
#include "blinkinlabs/blinkinlabs.h"
#endif



extern uint8_t battery_status;
extern TimerHandle_t ip5306_monitor_timer;
int i2s_num = 0; // i2s port number
int16_t next_event_write = 0;
// One set of events for the fifo
struct event events[EVENT_FIFO_LEN];
// And another event per voice as multi-channel sequencer that the scheduler renders into
struct event seq[VOICES];


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
    e.step = 0;
    e.substep = 0;
    e.sample = DOWN;
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

// deep copy an event to the fifo
void add_event(struct event e) { 
    events[next_event_write].voice = e.voice;
    events[next_event_write].velocity = e.velocity;
    events[next_event_write].duty = e.duty;
    events[next_event_write].feedback = e.feedback;
    events[next_event_write].midi_note = e.midi_note;
    events[next_event_write].wave = e.wave;
    events[next_event_write].patch = e.patch;
    events[next_event_write].freq = e.freq;
    events[next_event_write].amp = e.amp;
    events[next_event_write].time = e.time;
    events[next_event_write].status = e.status;
    events[next_event_write].sample = e.sample;
    events[next_event_write].step = e.step;
    events[next_event_write].substep = e.substep;
    next_event_write = (next_event_write + 1) % (EVENT_FIFO_LEN);
}

// The sequencer object keeps state betweeen voices, whereas events are only deltas/changes
esp_err_t setup_voices() {
    fm_init();
    oscillators_init();
    for(int i=0;i<VOICES;i++) {
        seq[i].voice = i; // self-reference to make updating oscillators easier
        seq[i].wave = OFF;
        seq[i].duty = 0.5;
        seq[i].patch = 0;
        seq[i].midi_note = 0;
        seq[i].freq = 0;
        seq[i].feedback = 0.996;
        seq[i].amp = 0;
        seq[i].velocity = 100;
        seq[i].step = 0;
        seq[i].sample = DOWN;
        seq[i].substep = 0;
    }

    // Fill the FIFO with default events, as the audio thread reads from it immediately
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        add_event(default_event());
    }
    return ESP_OK;
}


// Play an event, now -- tell the audio loop to start making noise
void play_event(struct event e) {
    if(e.midi_note >= 0) { seq[e.voice].midi_note = e.midi_note; seq[e.voice].freq = freq_for_midi_note(e.midi_note); } 
    if(e.wave >= 0) seq[e.voice].wave = e.wave;
    if(e.patch >= 0) seq[e.voice].patch = e.patch;
    if(e.duty >= 0) seq[e.voice].duty = e.duty;
    if(e.feedback >= 0) seq[e.voice].feedback = e.feedback;
    if(e.velocity >= 0) seq[e.voice].velocity = e.velocity;
    if(e.freq >= 0) seq[e.voice].freq = e.freq;
    if(e.amp >= 0) seq[e.voice].amp = e.amp;

    // Triggers / envelopes -- this needs some more thinking
    if(seq[e.voice].wave==FM) {
        if(seq[e.voice].midi_note>0) {
            fm_new_note_number(e.voice);
        } else {
            fm_new_note_freq(e.voice); 
        }
    }
    if(seq[e.voice].wave==KS) {
        ks_new_note_freq(e.voice);
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
        switch(seq[voice].wave) {
            case FM:
                render_fm(floatblock, voice); 
                break;
            case NOISE:
                render_noise(floatblock, voice);
                break;
            case SAW:
                render_saw(floatblock, voice);
                break;
            case PULSE:
                render_pulse(floatblock, voice); 
                break;
            case TRIANGLE:
                render_triangle(floatblock, voice);
                break;                
            case SINE:
                render_sine(floatblock, voice);
                break;
            case KS:
                render_ks(floatblock, voice); 
                break;

        }
    }
    // Bandlimit the buffer all at once
    blip_the_buffer(floatblock, block, BLOCK_SIZE);

    // And write
    size_t written = 0;
    i2s_write((i2s_port_t)i2s_num, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
    if(written != BLOCK_SIZE*2) {
        printf("i2s underrun: %d vs %d\n", written, BLOCK_SIZE*2);
    }
}


//i2s configuration
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
esp_err_t setup_i2s(void) {
  //initialize i2s with configurations above
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  i2s_set_sample_rates((i2s_port_t)i2s_num, SAMPLE_RATE);
  return ESP_OK;
}


void serialize_event(struct event e, uint16_t client) {
    // take an event and make it a string and send it to everyone!
    char message[MAX_RECEIVE_LEN];
    // Maybe only send the ones that are non-default? think
    sprintf(message, "a%fb%fc%dd%fe%df%fn%dp%dv%dw%dt%lld", 
        e.amp, e.feedback, client, e.duty, e.velocity, e.freq, e.midi_note, e.patch, e.voice, e.wave, e.time );
    mcast_send(message, strlen(message));
}

// parse a received event string and add event to queue
void deserialize_event(char * message, uint16_t length) {
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
    message[length] = 0;

    // Cut the OSC cruft Max etc add, they put a 0 and then more things after the 0
    int new_length = length; 
    for(int d=0;d<length;d++) { if(message[d] == 0) { new_length = d; d = length + 1;  } }
    length = new_length;

    //printf("message ###%s### len %d\n", data_buffer, recv_data);

    while(c < length+1) {
        uint8_t b = message[c];
        if(b == '_' && c==0) sync_response = 1;
        if(b >= 'a' || b <= 'z' || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atoi(message + start);
                // if we haven't yet synced our times, do it now
                if(!computed_delta_set) {
                    computed_delta = e.time - sysclock;
                    computed_delta_set = 1;
                }
            }
            if(mode=='a') e.amp=atof(message + start);
            if(mode=='b') e.feedback=atof(message+start);
            if(mode=='c') client = atoi(message + start); 
            if(mode=='d') e.duty=atof(message + start);
            if(mode=='e') e.velocity=atoi(message + start);
            if(mode=='f') e.freq=atof(message + start);
            if(mode=='i') sync_index = atoi(message + start);
            if(mode=='n') e.midi_note=atoi(message + start);
            if(mode=='p') e.patch=atoi(message + start);
            if(mode=='r') ipv4=atoi(message + start);
            if(mode=='s') sync = atoi(message + start); 
            if(mode=='v') e.voice=atoi(message + start);
            if(mode=='w') e.wave=atoi(message + start);
            mode=b;
            start=c+1;
        }
        c++;
    }
    if(sync_response) {
        // If this is a sync response, let's update our local map of who is booted
        update_map(client, ipv4, sync);
        length = 0; // don't need to do the rest
    }
    // Only do this if we got some data
    if(length >0) {
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
            if(for_me) add_event(e);
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
    add_event(e);
    e.time = sysclock + 150;
    e.freq = 440;
    add_event(e);
    e.time = sysclock + 300;
    e.amp = 0;
    e.freq = 0;
    add_event(e);
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
        add_event(e);
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
            type++;
            if(type==10) type = 0;
        }
    }
}


void check_init(esp_err_t (*fn)(), char *name) {
    printf("Starting %s: ", name);

    const esp_err_t ret = (*fn)();
    if(ret != ESP_OK) {
        printf("[ERROR:%i (%s)]\n", ret, esp_err_to_name(ret));
        return;
    }

    printf("[OK]\n");
}

void app_main() {
    // Init flash, network, event loop, GPIO
    check_init(&nvs_flash_init, "Flash");
    check_init(&esp_netif_init, "Netif");
    check_init(&esp_event_loop_create_default, "Event");


    check_init(&setup_i2s, "i2s");
    check_init(&setup_voices, "voices");
    check_init(&setup_midi, "midi");

#ifdef ALLES_V1_BOARD
    // Do the blinkinlabs board setup
    check_init(&master_i2c_init, "master_i2c"); // Used by ip5306
    check_init(&ip5306_init, "ip5306");         // Battery monitor
    check_init(&buttons_init, "buttons");       // For hardware buttons
    //check_init(&status_led_init, "status_led"); // LEDC driver for status LED

    ip5306_monitor_timer = xTimerCreate(
        "ip5306_monitor",
        pdMS_TO_TICKS(500),
        pdTRUE,
        NULL,
        ip5306_monitor);
    xTimerStart(ip5306_monitor_timer, 0);

#else
    // Do the protoboard / devboard setup
    // This is the "BOOT" pin on the devboards -- GPIO0
    gpio_set_direction(GPIO_NUM_0,  GPIO_MODE_INPUT);
    gpio_pullup_en(GPIO_NUM_0);
    vTaskDelay(2000 / portTICK_PERIOD_MS); // wait 2 seconds to see if button is pressed
    // Play a test sound and enter immediate mode.
    if(!gpio_get_level(GPIO_NUM_0)) { 
        immediate_mode = 1;
        xTaskCreatePinnedToCore(&read_midi, "read_midi_task", 4096, NULL, 1, NULL, 1);
        scale(KS, 0.5);
        while(1) { fill_audio_buffer(); } 
    }
#endif

    
    // start the main loop 
    ESP_ERROR_CHECK(wifi_connect());
    create_multicast_ipv4_socket();

    // Pin the UDP task to the 2nd core so the audio / main core runs on its own without getting starved
    xTaskCreatePinnedToCore(&mcast_listen_task, "mcast_task", 4096, NULL, 2, NULL, 1);
    
    // And the MIDI task to another process on the 2nd core
    xTaskCreatePinnedToCore(&read_midi, "read_midi_task", 4096, NULL, 1, NULL, 1);

    printf("Synth running on core %d\n", xPortGetCoreID());
    bleep();

    // Spin this core forever parsing events and making sounds
    while(1) {  
        fill_audio_buffer(); 
    }
    
    // We will never get here but just in case
    destroy();


}

