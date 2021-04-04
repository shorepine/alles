// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"



// Global state 
struct state global;
// envelope-modified global state
struct mod_state mglobal;

// set of events for the fifo to be played
struct event * events;
// state per voice as multi-channel synthesizer that the scheduler renders into
struct event * synth;
// envelope-modified per-voice state
struct mod_event * msynth;

// floatblock -- accumulative for mixing
float * floatblock;
// block -- what gets sent to the DAC -- -32767...32768 (wave file, int16 LE)
int16_t * block;


esp_err_t global_init() {
    global.next_event_write = 0;
    global.board_level = ALLES_BOARD_V1;
    global.status = RUNNING;
    global.volume = 0.5;
    global.resonance = 0.7;
    global.filter_freq = 0;
    return ESP_OK;
}

static const char TAG[] = "main";

// Button event
extern xQueueHandle gpio_evt_queue;

// Multicast task handle 
TaskHandle_t multicast_handle = NULL;

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
    e.phase = -1;
    e.duty = -1;
    e.feedback = -1;
    e.velocity = -1;
    e.midi_note = -1;
    e.amp = 1;
    e.freq = -1;
    e.volume = -1;
    e.filter_freq = -1;
    e.resonance = -1;

    e.lfo_source = -1;
    e.lfo_target = -1;
    e.adsr_target = -1;
    e.adsr_on_clock = -1;
    e.adsr_off_clock = -1;
    e.adsr_a = -1;
    e.adsr_d = -1;
    e.adsr_s = -1;
    e.adsr_r = -1;

    return e;
}

// deep copy an event to the fifo
void add_event(struct event e) { 
    int16_t ew = global.next_event_write;
    if(events[ew].status == SCHEDULED) {
        // We should drop these messages, the queue is full
        printf("queue (size %d) is full at index %d, skipping\n", EVENT_FIFO_LEN, ew);
    } else {
        events[ew].voice = e.voice;
        events[ew].velocity = e.velocity;
        events[ew].volume = e.volume;
        events[ew].filter_freq = e.filter_freq;
        events[ew].resonance = e.resonance;
        events[ew].duty = e.duty;
        events[ew].feedback = e.feedback;
        events[ew].midi_note = e.midi_note;
        events[ew].wave = e.wave;
        events[ew].patch = e.patch;
        events[ew].freq = e.freq;
        events[ew].amp = e.amp;
        events[ew].phase = e.phase;
        events[ew].time = e.time;
        events[ew].status = e.status;
        events[ew].sample = e.sample;
        events[ew].step = e.step;
        events[ew].substep = e.substep;
      
        events[ew].lfo_source = e.lfo_source;
        events[ew].lfo_target = e.lfo_target;
        events[ew].adsr_target = e.adsr_target;
        events[ew].adsr_on_clock = e.adsr_on_clock;
        events[ew].adsr_off_clock = e.adsr_off_clock;
        events[ew].adsr_a = e.adsr_a;
        events[ew].adsr_d = e.adsr_d;
        events[ew].adsr_s = e.adsr_s;
        events[ew].adsr_r = e.adsr_r;

        global.next_event_write = (ew + 1) % (EVENT_FIFO_LEN);
    }
}

void reset_voice(uint8_t i ) {
    // set all the synth state to defaults
    synth[i].voice = i; // self-reference to make updating oscillators easier
    synth[i].wave = SINE;
    synth[i].duty = 0.5;
    msynth[i].duty = 0.5;
    synth[i].patch = -1;
    synth[i].midi_note = 0;
    synth[i].freq = 0;
    msynth[i].freq = 0;
    synth[i].feedback = 0.996;
    synth[i].amp = 1;
    msynth[i].amp = 1;
    synth[i].phase = 0;
    synth[i].volume = 0;
    synth[i].filter_freq = 0;
    synth[i].resonance = 0.7;
    synth[i].velocity = 0;
    synth[i].step = 0;
    synth[i].sample = DOWN;
    synth[i].substep = 0;
    synth[i].status = OFF;
    synth[i].lfo_source = -1;
    synth[i].lfo_target = -1;
    synth[i].adsr_target = -1;
    synth[i].adsr_on_clock = -1;
    synth[i].adsr_off_clock = -1;
    synth[i].adsr_a = 0;
    synth[i].adsr_d = 0;
    synth[i].adsr_s = 1.0;
    synth[i].adsr_r = 0;

}

void reset_voices() {
    for(uint8_t i=0;i<VOICES;i++) reset_voice(i);
}

// The synth object keeps held state, whereas events are only deltas/changes
esp_err_t voices_init() {
    // FM init happens later for mem reason
    oscillators_init();
    filters_init();
    events = (struct event*) malloc(sizeof(struct event) * EVENT_FIFO_LEN);
    synth = (struct event*) malloc(sizeof(struct event) * VOICES);
    msynth = (struct mod_event*) malloc(sizeof(struct mod_event) * VOICES);
    floatblock = (float*) malloc(sizeof(float) * BLOCK_SIZE);
    block = (int16_t *) malloc(sizeof(int16_t) * BLOCK_SIZE);

    reset_voices();
    // Fill the FIFO with default events, as the audio thread reads from it immediately
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        // First clear out the malloc'd events so it doesn't seem like the queue is full
        events[i].status = EMPTY;
    }
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        add_event(default_event());
    }
    return ESP_OK;
}

void debug_voices() {
    // print out all the voice data

    printf("global: filter %f resonance %f volume %f status %d\n", global.filter_freq, global.resonance, global.volume, global.status);
    printf("mod global: filter %f resonance %f\n", mglobal.filter_freq, mglobal.resonance);
    for(uint8_t i=0;i<VOICES;i++) {
        printf("voice %d: status %d amp %f wave %d freq %f duty %f adsr_target %d lfo_target %d lfo source %d velocity %f E: %d,%d,%2.2f,%d step %f \n",
            i, synth[i].status, synth[i].amp, synth[i].wave, synth[i].freq, synth[i].duty, synth[i].adsr_target, synth[i].lfo_target, synth[i].lfo_source, 
            synth[i].velocity, synth[i].adsr_a, synth[i].adsr_d, synth[i].adsr_s, synth[i].adsr_r, synth[i].step);
        printf("mod voice %d: amp: %f, freq %f duty %f\n",
            i, msynth[i].amp, msynth[i].freq, msynth[i].duty);
    }
}
void voices_deinit() {
    free(block);
    free(floatblock);
    free(synth);
    free(msynth);
    free(events);
    oscillators_deinit();
    filters_deinit();
}




#if (DANDAC)
uint8_t dac_counter = 0;
uint8_t dac_render = 0;
#include "driver/timer.h"
#include "driver/dac.h"
#include "hal/dac_types.h"
// Setup 8-bit dac for Dan Ellis 
// pin D25
static void IRAM_ATTR timer0_ISR(void *ptr) {
    timer_group_clr_intr_status_in_isr(TIMER_GROUP_0, TIMER_0);
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, TIMER_0);
    uint8_t sample = (block[dac_counter++] + 32767) >> 8;
    dac_output_voltage(DAC_CHANNEL_1, sample); 
}

static void timerInit() {
    timer_config_t config = {
        .divider = 8, 
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE, 
        .alarm_en = TIMER_ALARM_EN, 
        .intr_type = TIMER_INTR_LEVEL,
        .auto_reload = 1, 
    };

    ESP_ERROR_CHECK(timer_init(TIMER_GROUP_0, TIMER_0, &config));
    ESP_ERROR_CHECK(timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL));
    ESP_ERROR_CHECK(timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_BASE_CLK / config.divider / SAMPLE_RATE));
    ESP_ERROR_CHECK(timer_enable_intr(TIMER_GROUP_0, TIMER_0));
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer0_ISR, (void *)NULL, ESP_INTR_FLAG_IRAM, NULL);
    timer_start(TIMER_GROUP_0, TIMER_0);
}

esp_err_t setup_dac(void) {
    ESP_ERROR_CHECK(dac_output_enable(DAC_CHANNEL_1));
    timerInit();
    return ESP_OK;
}

#else
// Setup I2S
esp_err_t setup_i2s(void) {
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
        .bck_io_num = CONFIG_I2S_BCLK, 
        .ws_io_num = CONFIG_I2S_LRCLK,  
        .data_out_num = CONFIG_I2S_DIN, 
        .data_in_num = -1   //Not used
    };
    i2s_driver_install((i2s_port_t)CONFIG_I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin((i2s_port_t)CONFIG_I2S_NUM, &pin_config);
    i2s_set_sample_rates((i2s_port_t)CONFIG_I2S_NUM, SAMPLE_RATE);
    return ESP_OK;
}

#endif

// Play an event, now -- tell the audio loop to start making noise
void play_event(struct event e) {
    if(e.midi_note >= 0) { synth[e.voice].midi_note = e.midi_note; synth[e.voice].freq = freq_for_midi_note(e.midi_note); } 
    if(e.wave >= 0) synth[e.voice].wave = e.wave;
    if(e.phase >= 0) synth[e.voice].phase = e.phase;
    if(e.patch >= 0) synth[e.voice].patch = e.patch;
    if(e.duty >= 0) synth[e.voice].duty = e.duty;
    if(e.feedback >= 0) synth[e.voice].feedback = e.feedback;
    if(e.freq >= 0) synth[e.voice].freq = e.freq;
    
    if(e.adsr_target >= 0) synth[e.voice].adsr_target = e.adsr_target;
    if(e.adsr_a >= 0) synth[e.voice].adsr_a = e.adsr_a;
    if(e.adsr_d >= 0) synth[e.voice].adsr_d = e.adsr_d;
    if(e.adsr_s >= 0) synth[e.voice].adsr_s = e.adsr_s;
    if(e.adsr_r >= 0) synth[e.voice].adsr_r = e.adsr_r;

    if(e.lfo_source >= 0) { synth[e.voice].lfo_source = e.lfo_source; synth[e.lfo_source].status = LFO_SOURCE; }
    if(e.lfo_target >= 0) synth[e.voice].lfo_target = e.lfo_target;

    // For global changes, just make the change, no need to update the per-voice synth
    if(e.volume >= 0) global.volume = e.volume; 
    if(e.filter_freq >= 0) global.filter_freq = e.filter_freq; 
    if(e.resonance >= 0) global.resonance = e.resonance; 

    // Triggers / envelopes 
    // The only way a sound is made is if velocity (note on) is >0.
    if(e.velocity>0 ) { // New note on (even if something is already playing on this voice)
        synth[e.voice].amp = e.velocity; 
        synth[e.voice].velocity = e.velocity;
        synth[e.voice].status = AUDIBLE;
        // Take care of FM & KS first -- no special treatment for ADSR/LFO
        if(synth[e.voice].wave==FM) { fm_note_on(e.voice); } 
        else if(synth[e.voice].wave==KS) { ks_note_on(e.voice); } 
        else {
            // an oscillator voice came in with a note on.
            // Start the ADSR clock
            synth[e.voice].adsr_on_clock = esp_timer_get_time() / 1000;

            // Restart the waveforms, adjusting for phase if given
            if(synth[e.voice].wave==SINE) sine_note_on(e.voice);
            if(synth[e.voice].wave==SAW) saw_note_on(e.voice);
            if(synth[e.voice].wave==TRIANGLE) triangle_note_on(e.voice);
            if(synth[e.voice].wave==PULSE) pulse_note_on(e.voice);
            if(synth[e.voice].wave==PCM) pcm_note_on(e.voice);

            // Also trigger "note ons" for the LFO source, if we have one
            if(synth[e.voice].lfo_source >= 0) {
                if(synth[synth[e.voice].lfo_source].wave==SINE) sine_note_on(synth[e.voice].lfo_source);
                if(synth[synth[e.voice].lfo_source].wave==SAW) saw_note_on(synth[e.voice].lfo_source);
                if(synth[synth[e.voice].lfo_source].wave==TRIANGLE) triangle_note_on(synth[e.voice].lfo_source);
                if(synth[synth[e.voice].lfo_source].wave==PULSE) pulse_note_on(synth[e.voice].lfo_source);
                if(synth[synth[e.voice].lfo_source].wave==PCM) pcm_note_on(synth[e.voice].lfo_source);
            }

        }
    } else if(synth[e.voice].velocity > 0 && e.velocity == 0) { // new note off
        synth[e.voice].velocity = e.velocity;
        if(synth[e.voice].wave==FM) { fm_note_off(e.voice); }
        else if(synth[e.voice].wave==KS) { ks_note_off(e.voice); }
        else {
            // osc voice note off, start release
            synth[e.voice].adsr_on_clock = -1;
            synth[e.voice].adsr_off_clock = esp_timer_get_time() / 1000;
        }
    }

}

// Apply an LFO & ADSR, if any, to the voice
void hold_and_modify(uint8_t voice) {
    // Copy all the modifier variables
    msynth[voice].amp = synth[voice].amp;
    msynth[voice].duty = synth[voice].duty;
    msynth[voice].freq = synth[voice].freq;

    // Modify the synth params by scale -- ADSR scale is (original * scale)
    float scale = compute_adsr_scale(voice);
    if(synth[voice].adsr_target & TARGET_AMP) msynth[voice].amp = msynth[voice].amp * scale;
    if(synth[voice].adsr_target & TARGET_DUTY) msynth[voice].duty = msynth[voice].duty * scale;
    if(synth[voice].adsr_target & TARGET_FREQ) msynth[voice].freq = msynth[voice].freq * scale;
    if(synth[voice].adsr_target & TARGET_FILTER_FREQ) mglobal.filter_freq = (mglobal.filter_freq * scale);
    if(synth[voice].adsr_target & TARGET_RESONANCE) mglobal.resonance = mglobal.resonance * scale;

    // And the LFO -- LFO scale is (original + (original * scale))
    scale = compute_lfo_scale(voice);
    if(synth[voice].lfo_target & TARGET_AMP) msynth[voice].amp = msynth[voice].amp + (msynth[voice].amp * scale);
    if(synth[voice].lfo_target & TARGET_DUTY) msynth[voice].duty = msynth[voice].duty + (msynth[voice].duty * scale);
    if(synth[voice].lfo_target & TARGET_FREQ) msynth[voice].freq = msynth[voice].freq + (msynth[voice].freq * scale);
    if(synth[voice].lfo_target & TARGET_FILTER_FREQ) mglobal.filter_freq = mglobal.filter_freq + (mglobal.filter_freq * scale);
    if(synth[voice].lfo_target & TARGET_RESONANCE) mglobal.resonance = mglobal.resonance + (mglobal.resonance * scale);
}



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

        // Save the current global synth state to the modifiers         
        mglobal.resonance = global.resonance;
        mglobal.filter_freq = global.filter_freq;

        for(uint8_t voice=0;voice<VOICES;voice++) {
            if(synth[voice].status==AUDIBLE) { // skip voices that are silent or LFO sources from playback
                hold_and_modify(voice); // apply ADSR / LFO
                if(synth[voice].wave == FM) render_fm(floatblock, voice);
                if(synth[voice].wave == NOISE) render_noise(floatblock, voice);
                if(synth[voice].wave == SAW) render_saw(floatblock, voice);
                if(synth[voice].wave == PULSE) render_pulse(floatblock, voice);
                if(synth[voice].wave == TRIANGLE) render_triangle(floatblock, voice);
                if(synth[voice].wave == SINE) render_sine(floatblock, voice);
                if(synth[voice].wave == KS) render_ks(floatblock, voice);
                if(synth[voice].wave == PCM) render_pcm(floatblock, voice);
            }
        }

        // Bandlimit the buffer all at once
        blip_the_buffer(floatblock, block, BLOCK_SIZE);

        // If filtering is on, filter the mixed signal
        if(mglobal.filter_freq > 0) {
            filter_update();
            filter_process_ints(block);
        }

#if (DANDAC)
        // "block" until the dac is done writing the current buffer
        while(dac_counter < BLOCK_SIZE) {
            ets_delay_us(200); 
        }
        dac_counter = 0;
#else
        // And write to I2S
        size_t written = 0;
        i2s_write((i2s_port_t)CONFIG_I2S_NUM, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
        if(written != BLOCK_SIZE*2) {
            printf("i2s underrun: %d vs %d\n", written, BLOCK_SIZE*2);
        }
#endif
    }
}

// Helper to parse the special ADSR string
void parse_adsr(struct event * e, char* message) {
    uint8_t idx = 0;
    uint8_t c = 0;
    // Change only the ones i received
    while(message[c] != 0 && c < MAX_RECEIVE_LEN) {
        if(message[c]!=',') {
            if(idx==0) {
                e->adsr_a = atoi(message+c);
            } else if(idx == 1) {
                e->adsr_d = atoi(message+c);
            } else if(idx == 2) {
                e->adsr_s = atof(message+c);
            } else if(idx == 3) {
                e->adsr_r = atoi(message+c);
            }
        }
        while(message[c]!=',' && message[c]!=0 && c < MAX_RECEIVE_LEN) c++;
        c++; idx++;
    }
}

// parse a received event string and add event to queue
uint8_t deserialize_event(char * message, uint16_t length) {
    // Don't process new messages if we're in MIDI mode
    if(global.status & MIDI_MODE) return 0;

    uint8_t mode = 0;
    int64_t sync = -1;
    int8_t sync_index = -1;
    uint8_t ipv4 = 0; 
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
    for(int d=0;d<length;d++) {
        if(message[d] == 0) { new_length = d; d = length + 1;  } 
    }
    length = new_length;

    //printf("received message ###%s### len %d\n", message, length);
    while(c < length+1) {
        uint8_t b = message[c];
        if(b == '_' && c==0) sync_response = 1;
        if( ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) || b == 0) {  // new mode or end
            if(mode=='t') {
                e.time=atol(message + start);
                // if we haven't yet synced our times, do it now
                if(!computed_delta_set) {
                    computed_delta = e.time - sysclock;
                    computed_delta_set = 1;
                }
            }
            if(mode=='A') parse_adsr(&e, message+start);
            if(mode=='b') e.feedback=atof(message+start);
            if(mode=='c') client = atoi(message + start); 
            if(mode=='d') e.duty=atof(message + start);
            // reminder: don't use "E" or "e", lol 
            if(mode=='f') e.freq=atof(message + start); 
            if(mode=='F') e.filter_freq=atof(message + start);
            if(mode=='g') e.lfo_target = atoi(message + start); 
            if(mode=='i') sync_index = atoi(message + start);
            if(mode=='l') e.velocity=atof(message + start);
            if(mode=='L') e.lfo_source=atoi(message + start);
            if(mode=='n') e.midi_note=atoi(message + start);
            if(mode=='p') e.patch=atoi(message + start);
            if(mode=='P') e.phase=atof(message + start);
            if(mode=='r') ipv4=atoi(message + start);
            if(mode=='R') e.resonance=atof(message + start);
            if(mode=='s') sync = atol(message + start); 
            if(mode=='S') { 
                uint8_t voice = atoi(message + start); 
                if(voice > VOICES-1) { reset_voices(); } else { reset_voice(voice); }
            }
            if(mode=='T') e.adsr_target = atoi(message + start); 
            if(mode=='v') e.voice=(atoi(message + start) % VOICES); // allow voice wraparound
            if(mode=='V') { e.volume = atof(message + start); debug_voices(); }
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
            // OK, so check for potentially negative numbers here (or really big numbers-sysclock) 
            int64_t potential_time = (e.time - computed_delta) + LATENCY_MS;
            if(potential_time < 0 || (potential_time > sysclock + LATENCY_MS + MAX_DRIFT_MS)) {
                printf("recomputing time base: message came in with %lld, mine is %lld, computed delta was %lld\n", e.time, sysclock, computed_delta);
                computed_delta = e.time - sysclock;
                printf("computed delta now %lld\n", computed_delta);
            }
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
                    if(alive>0) { // alive may get to 0 in a bad situation
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
    return 0;
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
void wifi_connected(void *pvParameter){
    ip_event_got_ip_t* param = (ip_event_got_ip_t*)pvParameter;

    char str_ip[16];
    esp_ip4addr_ntoa(&param->ip_info.ip, str_ip, IP4ADDR_STRLEN_MAX);

    ESP_LOGI(TAG, "I have a connection and my IP is %s!", str_ip);
    global.status |= WIFI_MANAGER_OK;
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
    if(global.status & MIDI_MODE) { 
        // just restart, easier that way
        esp_restart();
    } else {
        // If button pushed before wifi connects, wait for wifi to connect.
        while(!(global.status & WIFI_MANAGER_OK)) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        // turn on midi
        global.status = MIDI_MODE | RUNNING;
        // Play a MIDI sound before shutting down voices
        midi_tone();
        fill_audio_buffer(1.0);
        fm_deinit(); // have to free RAM to start the BLE stack
        voices_deinit();
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
        //printf("Error getting battery voltage\n");
        return;
    } else {
        if(battery_voltage == BATTERY_OVER_395) battery_mask = battery_mask | BATTERY_VOLTAGE_4;
        if(battery_voltage == BATTERY_38_395) battery_mask = battery_mask | BATTERY_VOLTAGE_3;
        if(battery_voltage == BATTERY_36_38) battery_mask = battery_mask | BATTERY_VOLTAGE_2;
        if(battery_voltage == BATTERY_33_36) battery_mask = battery_mask | BATTERY_VOLTAGE_1;
    }
}


void app_main() {
    check_init(&global_init, "global state");
    check_init(&esp_event_loop_create_default, "Event");
#if(DANDAC)
    check_init(&setup_dac, "dac");
#else
    check_init(&setup_i2s, "i2s");
#endif
    check_init(&voices_init, "voices");
    check_init(&buttons_init, "buttons"); // only one button for the protoboard, 4 for the blinkinlabs

    wifi_manager_start();
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &wifi_connected);

    // Wait for wifi to connect
    while(!(global.status & WIFI_MANAGER_OK)) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    };

    // Do the blinkinlabs battery setup
    check_init(&master_i2c_init, "master_i2c");
    
    // if ip5306 init fails, we don't have blinkinlabs board, set board level to 0
    if(check_init(&ip5306_init, "ip5306")) global.board_level = DEVBOARD; 
    if(global.board_level == ALLES_BOARD_V1) {
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
    xTaskCreatePinnedToCore(&mcast_listen_task, "mcast_task", 4096, NULL, 2, &multicast_handle, 1);
    
    // Allocate the FM RAM after the captive portal HTTP server is passed, as you can't have both at once
    fm_init();
    printf("Synth running on core %d\n", xPortGetCoreID());

    // Schedule a "turning on" sound
    bleep();

    // Spin this core until the power off button is pressed, parsing events and making sounds
    while(global.status & RUNNING) {
        // Only emit sounds if MIDI is not on
        if(!(global.status & MIDI_MODE)) {
            fill_audio_buffer(-1); 
        } else {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    // If we got here, the power off button was pressed (long hold on power.) 
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
    if(global.status & MIDI_MODE) midi_deinit();

    // Go into deep_sleep
    esp_deep_sleep_start();
}

