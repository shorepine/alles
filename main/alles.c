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
// state per oscillator as multi-channel synthesizer that the scheduler renders into
struct event * synth;
// envelope-modified per-oscillator state
struct mod_event * msynth;

// floatblock -- accumulative for mixing
float * floatblock_c0;
float * floatblock_c1;
// A second floatblock for independently generating e.g. triangle.
// This can be used within a given render_* function as scratch space.
float * scratchbuf;
// block -- what gets sent to the DAC -- -32768...32767 (wave file, int16 LE)
int16_t * block;
uint8_t core0 = 0;
uint8_t core1 = 1;


esp_err_t global_init() {
    global.next_event_write = 0;
    global.board_level = ALLES_BOARD_V1;
    global.status = RUNNING;
    global.volume = 1;
    global.resonance = 0.7;
    global.filter_freq = 0;
    return ESP_OK;
}

static const char TAG[] = "main";

// Button event
extern xQueueHandle gpio_evt_queue;

// Task handles for the renderers, multicast listener and main
TaskHandle_t multicast_handle = NULL;
static TaskHandle_t renderTask0 = NULL;
static TaskHandle_t renderTask1 = NULL;
static TaskHandle_t mainTask = NULL;


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
    e.oscillator = 0;
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
        events[ew].oscillator = e.oscillator;
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

void reset_oscillator(uint8_t i ) {
    // set all the synth state to defaults
    synth[i].oscillator = i; // self-reference to make updating oscillators easier
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
    synth[i].lpf_state[0] = 0;
    synth[i].lpf_state[1] = 0;
    synth[i].lpf_alpha = 0;
    synth[i].lpf_alpha_1 = 0;
}

void reset_oscillators() {
    for(uint8_t i=0;i<OSCILLATORS;i++) reset_oscillator(i);
}



// The synth object keeps held state, whereas events are only deltas/changes
esp_err_t oscillators_init() {
    // FM init happens later for mem reason
    ks_init();
    filters_init();
    events = (struct event*) malloc(sizeof(struct event) * EVENT_FIFO_LEN);
    synth = (struct event*) malloc(sizeof(struct event) * OSCILLATORS);
    msynth = (struct mod_event*) malloc(sizeof(struct mod_event) * OSCILLATORS);
    floatblock_c0 = (float*) malloc(sizeof(float) * BLOCK_SIZE);
    floatblock_c1 = (float*) malloc(sizeof(float) * BLOCK_SIZE);
    scratchbuf = (float*) malloc(sizeof(float) * BLOCK_SIZE);
    block = (int16_t *) malloc(sizeof(int16_t) * BLOCK_SIZE);

    reset_oscillators();
    // Fill the FIFO with default events, as the audio thread reads from it immediately
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        // First clear out the malloc'd events so it doesn't seem like the queue is full
        events[i].status = EMPTY;
    }
    for(int i=0;i<EVENT_FIFO_LEN;i++) {
        add_event(default_event());
    }

    // Create a rendering thread on each core so we can deal with dan ellis float math
    xTaskCreatePinnedToCore(&render_task, "render_task_0", 4096, &core0, 1, &renderTask0, 0);
    xTaskCreatePinnedToCore(&render_task, "render_task_1", 4096, &core1, 1, &renderTask1, 1);
    mainTask = xTaskGetCurrentTaskHandle();

    return ESP_OK;
}

void debug_oscillators() {
    // print out all the osc data
    char usage[40*16]; // 16 tasks running last i looked
/*
mcast_task      254348      <1
render_task_1   20852909        49
IDLE            19139465        45
IDLE            21115722        49
tiT             45785       <1
ipc1            37825       <1
httpd           1206        <1
ipc0            6505        <1
sys_evt         5842        <1
wifi_manager    76034       <1
esp_timer       22512       <1
wifi            1193764     2
render_task_0   13010973        30
main            8950820     21
Tmr Svc         15      <1
gpio_task       36      <1
*/
    vTaskGetRunTimeStats(usage);
    printf(usage);
    // print out all the osc data
    printf("global: filter %f resonance %f volume %f status %d\n", global.filter_freq, global.resonance, global.volume, global.status);
    printf("mod global: filter %f resonance %f\n", mglobal.filter_freq, mglobal.resonance);
    for(uint8_t i=0;i<OSCILLATORS;i++) {
        printf("osc %d: status %d amp %f wave %d freq %f duty %f adsr_target %d lfo_target %d lfo source %d velocity %f E: %d,%d,%2.2f,%d step %f \n",
            i, synth[i].status, synth[i].amp, synth[i].wave, synth[i].freq, synth[i].duty, synth[i].adsr_target, synth[i].lfo_target, synth[i].lfo_source, 
            synth[i].velocity, synth[i].adsr_a, synth[i].adsr_d, synth[i].adsr_s, synth[i].adsr_r, synth[i].step);
        printf("mod osc %d: amp: %f, freq %f duty %f\n",
            i, msynth[i].amp, msynth[i].freq, msynth[i].duty);
    }
}
void oscillators_deinit() {
    free(block);
    free(floatblock_c0);
    free(floatblock_c1);
    free(synth);
    free(msynth);
    free(events);
    ks_deinit();
    filters_deinit();
}





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


// Play an event, now -- tell the audio loop to start making noise
void play_event(struct event e) {
    if(e.midi_note >= 0) { synth[e.oscillator].midi_note = e.midi_note; synth[e.oscillator].freq = freq_for_midi_note(e.midi_note); } 
    if(e.wave >= 0) synth[e.oscillator].wave = e.wave;
    if(e.phase >= 0) synth[e.oscillator].phase = e.phase;
    if(e.patch >= 0) synth[e.oscillator].patch = e.patch;
    if(e.duty >= 0) synth[e.oscillator].duty = e.duty;
    if(e.feedback >= 0) synth[e.oscillator].feedback = e.feedback;
    if(e.freq >= 0) synth[e.oscillator].freq = e.freq;
    
    if(e.adsr_target >= 0) synth[e.oscillator].adsr_target = e.adsr_target;
    if(e.adsr_a >= 0) synth[e.oscillator].adsr_a = e.adsr_a;
    if(e.adsr_d >= 0) synth[e.oscillator].adsr_d = e.adsr_d;
    if(e.adsr_s >= 0) synth[e.oscillator].adsr_s = e.adsr_s;
    if(e.adsr_r >= 0) synth[e.oscillator].adsr_r = e.adsr_r;

    if(e.lfo_source >= 0) { synth[e.oscillator].lfo_source = e.lfo_source; synth[e.lfo_source].status = LFO_SOURCE; }
    if(e.lfo_target >= 0) synth[e.oscillator].lfo_target = e.lfo_target;

    // For global changes, just make the change, no need to update the per-oscillator synth
    if(e.volume >= 0) global.volume = e.volume; 
    if(e.filter_freq >= 0) global.filter_freq = e.filter_freq; 
    if(e.resonance >= 0) global.resonance = e.resonance; 

    // Triggers / envelopes 
    // The only way a sound is made is if velocity (note on) is >0.
    if(e.velocity>0 ) { // New note on (even if something is already playing on this oscillator)
        synth[e.oscillator].amp = e.velocity; 
        synth[e.oscillator].velocity = e.velocity;
        synth[e.oscillator].status = AUDIBLE;
        // Take care of FM & KS first -- no special treatment for ADSR/LFO
        if(synth[e.oscillator].wave==FM) { fm_note_on(e.oscillator); } 
        else if(synth[e.oscillator].wave==KS) { ks_note_on(e.oscillator); } 
        else {
            // an oscillator came in with a note on.
            // Start the ADSR clock
            synth[e.oscillator].adsr_on_clock = esp_timer_get_time() / 1000;

            // Restart the waveforms, adjusting for phase if given
            if(synth[e.oscillator].wave==SINE) sine_note_on(e.oscillator);
            if(synth[e.oscillator].wave==SAW) saw_note_on(e.oscillator);
            if(synth[e.oscillator].wave==TRIANGLE) triangle_note_on(e.oscillator);
            if(synth[e.oscillator].wave==PULSE) pulse_note_on(e.oscillator);
            if(synth[e.oscillator].wave==PCM) pcm_note_on(e.oscillator);

            // Also trigger "note ons" for the LFO source, if we have one
            if(synth[e.oscillator].lfo_source >= 0) {
                if(synth[synth[e.oscillator].lfo_source].wave==SINE) sine_note_on(synth[e.oscillator].lfo_source);
                if(synth[synth[e.oscillator].lfo_source].wave==SAW) saw_note_on(synth[e.oscillator].lfo_source);
                if(synth[synth[e.oscillator].lfo_source].wave==TRIANGLE) triangle_note_on(synth[e.oscillator].lfo_source);
                if(synth[synth[e.oscillator].lfo_source].wave==PULSE) pulse_note_on(synth[e.oscillator].lfo_source);
                if(synth[synth[e.oscillator].lfo_source].wave==PCM) pcm_note_on(synth[e.oscillator].lfo_source);
            }

        }
    } else if(synth[e.oscillator].velocity > 0 && e.velocity == 0) { // new note off
        synth[e.oscillator].velocity = e.velocity;
        if(synth[e.oscillator].wave==FM) { fm_note_off(e.oscillator); }
        else if(synth[e.oscillator].wave==KS) { ks_note_off(e.oscillator); }
        else {
            // osc note off, start release
            synth[e.oscillator].adsr_on_clock = -1;
            synth[e.oscillator].adsr_off_clock = esp_timer_get_time() / 1000;
        }
    }

}

// Apply an LFO & ADSR, if any, to the oscillator
void hold_and_modify(uint8_t oscillator) {
    // Copy all the modifier variables
    msynth[oscillator].amp = synth[oscillator].amp;
    msynth[oscillator].duty = synth[oscillator].duty;
    msynth[oscillator].freq = synth[oscillator].freq;

    // Modify the synth params by scale -- ADSR scale is (original * scale)
    float scale = compute_adsr_scale(oscillator);
    if(synth[oscillator].adsr_target & TARGET_AMP) msynth[oscillator].amp = msynth[oscillator].amp * scale;
    if(synth[oscillator].adsr_target & TARGET_DUTY) msynth[oscillator].duty = msynth[oscillator].duty * scale;
    if(synth[oscillator].adsr_target & TARGET_FREQ) msynth[oscillator].freq = msynth[oscillator].freq * scale;
    if(synth[oscillator].adsr_target & TARGET_FILTER_FREQ) mglobal.filter_freq = (mglobal.filter_freq * scale);
    if(synth[oscillator].adsr_target & TARGET_RESONANCE) mglobal.resonance = mglobal.resonance * scale;

    // And the LFO -- LFO scale is (original + (original * scale))
    scale = compute_lfo_scale(oscillator);
    if(synth[oscillator].lfo_target & TARGET_AMP) msynth[oscillator].amp = msynth[oscillator].amp + (msynth[oscillator].amp * scale);
    if(synth[oscillator].lfo_target & TARGET_DUTY) msynth[oscillator].duty = msynth[oscillator].duty + (msynth[oscillator].duty * scale);
    if(synth[oscillator].lfo_target & TARGET_FREQ) msynth[oscillator].freq = msynth[oscillator].freq + (msynth[oscillator].freq * scale);
    if(synth[oscillator].lfo_target & TARGET_FILTER_FREQ) mglobal.filter_freq = mglobal.filter_freq + (mglobal.filter_freq * scale);
    if(synth[oscillator].lfo_target & TARGET_RESONANCE) mglobal.resonance = mglobal.resonance + (mglobal.resonance * scale);
}


void render_task(void *c) {
    uint8_t core = *(uint8_t*)c;
    uint8_t start, end;
    float *fbl = NULL;
    if(core==0) { 
        start = 0; end = OSCILLATORS / 2; 
        fbl = floatblock_c0; 
    }
    if(core==1) {
        start = OSCILLATORS /2; end = OSCILLATORS; 
        fbl = floatblock_c1; 
    }
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for(uint16_t i=0;i<BLOCK_SIZE;i++) fbl[i] = 0; 
        for(uint8_t oscillator=start;oscillator<end;oscillator++) {
            if(synth[oscillator].status==AUDIBLE) { // skip oscs that are silent or LFO sources from playback
                hold_and_modify(oscillator); // apply ADSR / LFO
                if(synth[oscillator].wave == FM) render_fm(fbl, oscillator);
                if(synth[oscillator].wave == NOISE) render_noise(fbl, oscillator);
                if(synth[oscillator].wave == SAW) render_saw(fbl, oscillator);
                if(synth[oscillator].wave == PULSE) render_pulse(fbl, oscillator);
                if(synth[oscillator].wave == TRIANGLE) render_triangle(fbl, oscillator);
                if(synth[oscillator].wave == SINE) render_sine(fbl, oscillator);
                if(synth[oscillator].wave == KS) render_ks(fbl, oscillator);
                if(synth[oscillator].wave == PCM) render_pcm(fbl, oscillator);
            }
        }
        // Tell the main task that i'm done rendering (TODO, fill_audio_buffer should be a task, not main)
        xTaskNotifyGive(mainTask);
    }
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
        // Save the current global synth state to the modifiers         
        mglobal.resonance = global.resonance;
        mglobal.filter_freq = global.filter_freq;

        // Tell the two rendering threads to start rendering
        xTaskNotifyGive(renderTask0);
        xTaskNotifyGive(renderTask1);
        // And wait for each of them to come back
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

#define SAMPLE_MAX 32767
        // D is how close the sample gets to the clip limit before the nonlinearity engages.  
        // So D=0.1 means output is linear for -0.9..0.9, then starts clipping.
#define CLIP_D 0.1
        for(int16_t i=0; i < BLOCK_SIZE; ++i) {
            // Soft clipping.
            float sign = 1;
            float fsample = floatblock_c0[i] + floatblock_c1[i];
            if (fsample < 0) sign = -1;  // sign  = sign(floatblock[i]);
            // Global volume is supposed to max out at 10, so scale by 0.1.
            float val = fabs(0.1 * global.volume * fsample / ((float)SAMPLE_MAX));
            float clipped_val = val;
            if (val > (1.0 + 0.5 * CLIP_D)) {
                clipped_val = 1.0;
            } else if (val > (1.0 - CLIP_D)) {
                // Cubic transition from linear to saturated - classic x - (x^3)/3.
                float xdash = (val - (1.0 - CLIP_D)) / (1.5 * CLIP_D);
                clipped_val = (1.0 - CLIP_D) + 1.5 * CLIP_D * (xdash - xdash * xdash * xdash / 3.0);
            }

            int16_t sample = (int16_t)round(SAMPLE_MAX * sign * clipped_val);
            // ^ 0x01 implements word-swapping, needed for ESP32 I2S_CHANNEL_FMT_ONLY_LEFT
            block[i ^ 0x01] = sample;   // for internal DAC:  + 32768.0); 
        }
    
        // If filtering is on, filter the mixed signal
        if(mglobal.filter_freq > 0) {
            filter_update();
            filter_process_ints(block);
        }

        // And write to I2S
        size_t written = 0;
        i2s_write((i2s_port_t)CONFIG_I2S_NUM, block, BLOCK_SIZE * 2, &written, portMAX_DELAY);
        if(written != BLOCK_SIZE*2) {
            printf("i2s underrun: %d vs %d\n", written, BLOCK_SIZE*2);
        }
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
            if(mode=='D') debug_oscillators(); 
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
                uint8_t oscillator = atoi(message + start); 
                if(oscillator > OSCILLATORS-1) { reset_oscillators(); } else { reset_oscillator(oscillator); }
            }
            if(mode=='T') e.adsr_target = atoi(message + start); 
            if(mode=='v') e.oscillator=(atoi(message + start) % OSCILLATORS); // allow osc wraparound
            if(mode=='V') { e.volume = atof(message + start); }
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
        // Play a MIDI sound before shutting down oscs
        midi_tone();
        fill_audio_buffer(1.0);
        fm_deinit(); // have to free RAM to start the BLE stack
        oscillators_deinit();
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
    check_init(&setup_i2s, "i2s");
    check_init(&oscillators_init, "oscillators");
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

