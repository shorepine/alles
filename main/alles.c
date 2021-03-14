// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"




// Global state for events 
// Pointer to next event
int16_t next_event_write = 0;
// One set of events for the fifo
struct event events[EVENT_FIFO_LEN];
// And another event per voice as multi-channel sequencer that the scheduler renders into
struct event seq[VOICES];

// Global state for mode
// assume blinkinlabs for now
uint8_t board_level = ALLES_BOARD_V1;
uint8_t midi_mode = 0;
uint8_t running = 1;
uint8_t wifi_manager_started_ok = 0;


static const char TAG[] = "main";

// Button event
extern xQueueHandle gpio_evt_queue;

// Battery status for V1 board. If no v1 board, will stay at 0
uint8_t battery_mask = 0;


float freq_for_midi_note(uint8_t midi_note) {
    return 440.0*pow(2,(midi_note-69.0)/12.0);
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
    if(events[next_event_write].status == SCHEDULED) {
        // We should drop these messages, the queue is full
        printf("queue (size %d) is full at index %d, skipping\n", EVENT_FIFO_LEN, next_event_write);
    } else {
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
}

// The sequencer object keeps state betweeen voices, whereas events are only deltas/changes
esp_err_t setup_voices() {
    // FM init happens later for mem reason
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
void fill_audio_buffer(float seconds) {
    // if seconds < 0, just do this once, but otherwise, compute iterations
    uint16_t iterations = 1;
    if(seconds > 0) {
        iterations = ((seconds * SAMPLE_RATE) / BLOCK_SIZE)+1;
    }
    for(uint16_t iter=0;iter<iterations;iter++) {
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
        i2s_write((i2s_port_t)CONFIG_I2S_NUM, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
        if(written != BLOCK_SIZE*2) {
            printf("i2s underrun: %d vs %d\n", written, BLOCK_SIZE*2);
        }
    }
}


//i2s configuration
i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
     .sample_rate = SAMPLE_RATE,
     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
     .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
     .dma_buf_count = 8,
     .dma_buf_len = 64   //Interrupt level 1
    };
    
i2s_pin_config_t pin_config = {
    .bck_io_num = CONFIG_I2S_BCLK,   // this is BCK pin 
    .ws_io_num = CONFIG_I2S_LRCLK,   // this is LRCK pin
    .data_out_num = CONFIG_I2S_DIN,  // this is DIN 
    .data_in_num = -1   //Not used
};

// Setup I2S
esp_err_t setup_i2s(void) {
    i2s_driver_install((i2s_port_t)CONFIG_I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin((i2s_port_t)CONFIG_I2S_NUM, &pin_config);
    i2s_set_sample_rates((i2s_port_t)CONFIG_I2S_NUM, SAMPLE_RATE);
    return ESP_OK;
}

// take an event and make it a string and send it to everyone!
void serialize_event(struct event e, uint16_t client) {
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
    uint8_t ipv4 = 0; // lol i had this as an int8 jeez 
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

    // Debug
    //printf("message ###%s### len %d\n", message, length);

    while(c < length+1) {
        uint8_t b = message[c];
        if(b == '_' && c==0) sync_response = 1;
        if(b >= 'a' || b <= 'z' || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atol(message + start);
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
            if(mode=='s') sync = atol(message + start); 
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

int8_t check_init(esp_err_t (*fn)(), char *name) {
    printf("Starting %s: ", name);
    const esp_err_t ret = (*fn)();
    if(ret != ESP_OK) {
        printf("[ERROR:%i (%s)]\n", ret, esp_err_to_name(ret));
        return -1;
    }
    printf("[OK]\n");
    return 0;
}


// callback to let us know when we have wifi set up ok.
void cb_connection_ok(void *pvParameter){
    ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
    wifi_manager_started_ok = 1;
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
    vTaskDelay(100 / portTICK_PERIOD_MS);

    esp_restart();
}


// Called when the MIDI button is hit. Toggle between MIDI on and off mode
void toggle_midi() {
    if(midi_mode) { 
        // turn off midi
        midi_mode = 0;
        // just restart, easier that way
        esp_restart();
    } else {
        // turn on midi
        midi_mode = 1;
        fm_deinit(); // have to free RAM to start the BLE stack
        midi_init();
    }
}


// Periodic task to poll the ip5306 for the battery charge and button states.
// Intented to be run from a low-priority timer at 1-2 Hz
void ip5306_monitor() {
    esp_err_t ret;

    // Check if the power button was pressed
    int buttons;
    ret = ip5306_button_press_get(&buttons);
    if(ret!= ESP_OK) {
        printf("Error reading button press\n");
        return;
    }

    if(buttons & BUTTON_LONG_PRESS) {
        printf("button long\n");
        const uint32_t button = BUTTON_POWER_LONG;
        xQueueSend(gpio_evt_queue, &button, 0);
    }
    if(buttons & BUTTON_SHORT_PRESS) {
        printf("button short\n");
        const uint32_t button = BUTTON_POWER_SHORT;
        xQueueSend(gpio_evt_queue, &button, 0);
    }

    // Update the battery charge state
    ip5306_charge_state_t charge_state;

    ret = ip5306_charge_state_get(&charge_state);
    if(ret != ESP_OK) {
        printf("Error reading battery charge state\n");
        return;
    }
    
    battery_mask = 0;

    switch(charge_state) {
    case CHARGE_STATE_CHARGED:
        battery_mask = battery_mask | BATTERY_STATE_CHARGED;
        break;
    case CHARGE_STATE_CHARGING:
        battery_mask = battery_mask | BATTERY_STATE_CHARGING;
        break;
    case CHARGE_STATE_DISCHARGING:
        battery_mask = battery_mask | BATTERY_STATE_DISCHARGING;
        break;
    case CHARGE_STATE_DISCHARGING_LOW_BAT:
        battery_mask = battery_mask | BATTERY_STATE_LOW;
        break;
    }

    ip5306_battery_voltage_t battery_voltage;

    ret = ip5306_battery_voltage_get(&battery_voltage);
    if(ret != ESP_OK) {
        printf("Error getting battery voltage\n");
        return;
    } else {
        if(battery_voltage == BATTERY_OVER_395) battery_mask = battery_mask | BATTERY_VOLTAGE_4;
        if(battery_voltage == BATTERY_38_395) battery_mask = battery_mask | BATTERY_VOLTAGE_3;
        if(battery_voltage == BATTERY_36_38) battery_mask = battery_mask | BATTERY_VOLTAGE_2;
        if(battery_voltage == BATTERY_33_36) battery_mask = battery_mask | BATTERY_VOLTAGE_1;
    }

}


void app_main() {
    check_init(&esp_event_loop_create_default, "Event");
    check_init(&setup_i2s, "i2s");
    check_init(&setup_voices, "voices");
    check_init(&buttons_init, "buttons"); // only one button for the protoboard, 4 for the blinkinlabs

    wifi_manager_start();

    /* register a callback as an example to how you can integrate your code with the wifi manager */
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);

    while(!wifi_manager_started_ok) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    };

    uint8_t captive_on = 1;
    int64_t tic = esp_timer_get_time() / 1000;


    // Do the blinkinlabs battery setup
    check_init(&master_i2c_init, "master_i2c");
    
    // if ip5306 init fails, we don't have blinkinlabs board, set board level to 0
    if(check_init(&ip5306_init, "ip5306")) board_level = DEVBOARD; 
    if(board_level == ALLES_BOARD_V1) {
        TimerHandle_t ip5306_monitor_timer =xTimerCreate(
            "ip5306_monitor",
            pdMS_TO_TICKS(500),
            pdTRUE,
            NULL,
            ip5306_monitor);
        xTimerStart(ip5306_monitor_timer, 0);
    }

    create_multicast_ipv4_socket();

    // Pin the UDP task to the 2nd core so the audio / main core runs on its own without getting starved
    TaskHandle_t multicast_handle = NULL;
    xTaskCreatePinnedToCore(&mcast_listen_task, "mcast_task", 4096, NULL, 2, &multicast_handle, 1);
    
    // Allocate the FM RAM after the captive portal HTTP server is passed, as you can't have both at once
    fm_init();
    printf("Synth running on core %d\n", xPortGetCoreID());

    // Schedule a "turning on" sound
    bleep();

    // Spin this core until the power off button is pressed, parsing events and making sounds
    while(running) {
        // Only emit sounds if MIDI is not on
        if(!midi_mode) {
            fill_audio_buffer(-1); 
        } else {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        // Kill the http server after 10s
        // we can't kill it immediately because the wifi config checks it to see when it's done adding a new ssid/pass
        int64_t sysclock = esp_timer_get_time() / 1000;
        if((sysclock-tic) > 10000 && captive_on) {
            // Stop the captive portal after 10s from boot, we don't need it anymore
            printf("Stopping captive portal\n");
            http_app_stop();
            wifi_manager_destroy();
            captive_on = 0;
        }
    }

    // If we're here, the power off button was pressed (long hold on power.) 
    // The idea here is we go into a low power deep sleep mode waiting for a GPIO pin to turn us back on
    // The battery can still charge during this, but let's turn off audio, wifi, multicast, midi 

    // Play a "turning off" sound
    debleep();
    fill_audio_buffer(1.0);

#if(ALLES_V1_BOARD)
    // Enable the low-current shutdown mode of the battery IC.
    // Apparently after 8s it will stop providing power from the battery
    ip5306_auto_poweroff_enable();
#endif
    // Stop mulitcast listening, wifi, midi
    vTaskDelete(multicast_handle);
    esp_wifi_stop();
    if(midi_mode) midi_deinit();

    // Go into deep_sleep
    esp_deep_sleep_start();
}

