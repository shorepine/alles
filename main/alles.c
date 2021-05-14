// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"



// Global state 
struct state global;
// envelope-modified global state
struct mod_state mglobal;

// set of deltas for the fifo to be played
struct delta * events;
// state per osc as multi-channel synthesizer that the scheduler renders into
struct event * synth;
// envelope-modified per-osc state
struct mod_event * msynth;

// One floatblock per core, added up later
float ** fbl;

// A second floatblock per core for independently generating e.g. triangle.
// This can be used within a given render_* function as scratch space.
float ** scratchbuf;
// block -- what gets sent to the DAC -- -32768...32767 (wave file, int16 LE)
int16_t * block;

// Semaphore that locks writes to the delta queue
SemaphoreHandle_t xQueueSemaphore;

void delay_ms(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

esp_err_t global_init() {
    global.next_event_write = 0;
    global.event_start = NULL;
    global.event_qsize = 0;
    global.board_level = ALLES_BOARD_V2;
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
TaskHandle_t mcastTask = NULL;
static TaskHandle_t renderTask[2]; // one per core
static TaskHandle_t fillbufferTask = NULL;
static TaskHandle_t idleTask0 = NULL;
static TaskHandle_t idleTask1 = NULL;


// Battery status for V2 board. If no v2 board, will stay at 0
uint8_t battery_mask = 0;


float freq_for_midi_note(uint8_t midi_note) {
    return 440.0*pow(2,(midi_note-69.0)/12.0);
}


// Create a new default event -- mostly -1 or no change
struct event default_event() {
    struct event e;
    e.status = EMPTY;
    e.time = 0;
    e.osc = 0;
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

void add_delta_to_queue(struct delta d) {
    //  Take the queue semaphore before starting
    xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);

    if(global.event_qsize < EVENT_FIFO_LEN) {
        // scan through the memory to find a free slot, starting at write pointer
        uint16_t write_location = global.next_event_write;
        int16_t found = -1;
        // guaranteed to find eventually if qsize stays accurate
        while(found<0) {
            if(events[write_location].time == UINT32_MAX) found = write_location;
            write_location = (write_location + 1) % EVENT_FIFO_LEN;
        }
        // Found a mem location. Copy the data in and update the write pointers.
        events[found].time = d.time;
        events[found].osc = d.osc;
        events[found].param = d.param;
        events[found].data = d.data;
        global.next_event_write = write_location;
        global.event_qsize++;

        // Now insert it into the sorted list for fast playback
        // First, see if it's eariler than the first item, special case
        if(d.time < global.event_start->time) {
            events[found].next = global.event_start;
            global.event_start = &events[found];
        } else {
            // or it's got to be found somewhere
            struct delta* ptr = global.event_start; 
            int8_t inserted = -1;
            while(inserted<0) {
                if(d.time < ptr->next->time) { 
                    // next should point to me, and my next should point to old next
                    events[found].next = ptr->next;
                    ptr->next = &events[found];
                    inserted = 1;
                }
                ptr = ptr->next;
            }
        }

    } else {
        // TODO -- report this somehow? 
        // If there's no room in the queue, just skip the message
    }
    xSemaphoreGive( xQueueSemaphore );


}


void add_event(struct event e) {
    // make delta objects out of the UDP event and add them to the queue
    struct delta d;
    d.osc = e.osc;
    d.time = e.time;
    if(e.wave>-1) { d.param=WAVE; d.data = *(uint32_t *)&e.wave; add_delta_to_queue(d); }
    if(e.patch>-1) { d.param=PATCH; d.data = *(uint32_t *)&e.patch; add_delta_to_queue(d); }
    if(e.midi_note>-1) { d.param=MIDI_NOTE; d.data = *(uint32_t *)&e.midi_note; add_delta_to_queue(d); }
    if(e.amp>-1) { d.param=AMP; d.data = *(uint32_t *)&e.amp; add_delta_to_queue(d); }
    if(e.duty>-1) { d.param=DUTY; d.data = *(uint32_t *)&e.duty; add_delta_to_queue(d); }
    if(e.feedback>-1) { d.param=FEEDBACK; d.data = *(uint32_t *)&e.feedback; add_delta_to_queue(d); }
    if(e.freq>-1) { d.param=FREQ; d.data = *(uint32_t *)&e.freq; add_delta_to_queue(d); }
    if(e.phase>-1) { d.param=PHASE; d.data = *(uint32_t *)&e.phase; add_delta_to_queue(d); }
    if(e.volume>-1) { d.param=VOLUME; d.data = *(uint32_t *)&e.volume; add_delta_to_queue(d); }
    if(e.filter_freq>-1) { d.param=FILTER_FREQ; d.data = *(uint32_t *)&e.filter_freq; add_delta_to_queue(d); }
    if(e.resonance>-1) { d.param=RESONANCE; d.data = *(uint32_t *)&e.resonance; add_delta_to_queue(d); }
    if(e.lfo_source>-1) { d.param=LFO_SOURCE; d.data = *(uint32_t *)&e.lfo_source; add_delta_to_queue(d); }
    if(e.lfo_target>-1) { d.param=LFO_TARGET; d.data = *(uint32_t *)&e.lfo_target; add_delta_to_queue(d); }
    if(e.adsr_target>-1) { d.param=ADSR_TARGET; d.data = *(uint32_t *)&e.adsr_target; add_delta_to_queue(d); }
    if(e.adsr_a>-1) { d.param=ADSR_A; d.data = *(uint32_t *)&e.adsr_a; add_delta_to_queue(d); }
    if(e.adsr_d>-1) { d.param=ADSR_D; d.data = *(uint32_t *)&e.adsr_d; add_delta_to_queue(d); }
    if(e.adsr_s>-1) { d.param=ADSR_S; d.data = *(uint32_t *)&e.adsr_s; add_delta_to_queue(d); }
    if(e.adsr_r>-1) { d.param=ADSR_R; d.data = *(uint32_t *)&e.adsr_r; add_delta_to_queue(d); }

    // Add this last -- this is a trigger, that if sent alongside osc setup parameters, you want to run after those
    if(e.velocity>-1) { d.param=VELOCITY; d.data = *(uint32_t *)&e.velocity; add_delta_to_queue(d); }
}

void reset_osc(uint8_t i ) {
    // set all the synth state to defaults
    synth[i].osc = i; // self-reference to make updating oscs easier
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

void reset_oscs() {
    for(uint8_t i=0;i<OSCS;i++) reset_osc(i);
}



// The synth object keeps held state, whereas events are only deltas/changes
esp_err_t oscs_init() {
    // FM init happens later for mem reason
    ks_init();
    filters_init();
    events = (struct delta*)malloc(sizeof(struct delta) * EVENT_FIFO_LEN);
    synth = (struct event*) malloc(sizeof(struct event) * OSCS);
    msynth = (struct mod_event*) malloc(sizeof(struct mod_event) * OSCS);
    fbl = (float**) malloc(sizeof(float*) * 2); // one per core
    scratchbuf = (float**) malloc(sizeof(float*) * 2); // one per core
    block = (int16_t *) malloc(sizeof(int16_t) * BLOCK_SIZE);

    reset_oscs();

    // Set all the events to empty
    for(uint16_t i=0;i<EVENT_FIFO_LEN;i++) events[i].time = UINT32_MAX;

    // Make a fencepost last event with no next, time of end-1, and call it start
    events[0].next = NULL;
    events[0].time = UINT32_MAX - 1;
    global.next_event_write = 1;
    global.event_start = &events[0];
    global.event_qsize = 1;

    xQueueSemaphore = xSemaphoreCreateMutex();


    // Create rendering threads, one per core so we can deal with dan ellis float math
    fbl[0] = (float*)malloc(sizeof(float) * BLOCK_SIZE);
    fbl[1] = (float*)malloc(sizeof(float) * BLOCK_SIZE);
    scratchbuf[0] = (float*)malloc(sizeof(float) * BLOCK_SIZE);
    scratchbuf[1] = (float*)malloc(sizeof(float) * BLOCK_SIZE);
    xTaskCreatePinnedToCore(&render_task, "render_task0", 4096, NULL, 1, &renderTask[0], 0);
    xTaskCreatePinnedToCore(&render_task, "render_task1", 4096, NULL, 1, &renderTask[1], 1);

    delay_ms(500);
    // And the fill audio buffer thread, combines, does volume & filters
    xTaskCreatePinnedToCore(&fill_audio_buffer_task, "fill_audio_buff", 4096, NULL, 1, &fillbufferTask, 0);

    // Grab the idle handles while we're here
    idleTask0 = xTaskGetIdleTaskHandleForCPU(0);
    idleTask1 = xTaskGetIdleTaskHandleForCPU(1);
    return ESP_OK;
}


#define MAX_TASKS 8
unsigned long last_task_counters[MAX_TASKS] = {0};

void show_debug(uint8_t type) { 
    TaskStatus_t *pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x, i;
    const char* const tasks[] = { "render_task0", "render_task1", "mcast_task", "main", "fill_audio_buff", "wifi", "idle0", "idle1", 0 }; 
    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, NULL );
    unsigned long counter_since_last[MAX_TASKS];
    unsigned long ulTotalRunTime = 0;
    TaskStatus_t xTaskDetails;

    for(i=0;i<MAX_TASKS;i++) { // for each name
        for(x=0; x<uxArraySize; x++) { // for each task
            if(strcmp(pxTaskStatusArray[x].pcTaskName, tasks[i])==0) {
                counter_since_last[i] = pxTaskStatusArray[x].ulRunTimeCounter - last_task_counters[i];
                last_task_counters[i] = pxTaskStatusArray[x].ulRunTimeCounter;
                ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
            }
        }
        // Have to get these specially as the task manager calls them both "IDLE" and swaps their orderings around
        if(strcmp("idle0", tasks[i])==0) { 
            vTaskGetInfo(idleTask0, &xTaskDetails, pdFALSE, eRunning);
            counter_since_last[i] = xTaskDetails.ulRunTimeCounter - last_task_counters[i];
            last_task_counters[i] = xTaskDetails.ulRunTimeCounter;
            ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
        }
        if(strcmp("idle1", tasks[i])==0) { 
            vTaskGetInfo(idleTask1, &xTaskDetails, pdFALSE, eRunning);
            counter_since_last[i] = xTaskDetails.ulRunTimeCounter - last_task_counters[i];
            last_task_counters[i] = xTaskDetails.ulRunTimeCounter;
            ulTotalRunTime = ulTotalRunTime + counter_since_last[i];
        }

    }
    printf("------ CPU usage since last call to debug()\n");
    for(i=0;i<MAX_TASKS;i++) {
        printf("%-15s\t%-15ld\t\t%2.2f%%\n", tasks[i], counter_since_last[i], (float)counter_since_last[i]/ulTotalRunTime * 100.0);
    }   
    printf("------\nEvent queue size %d / %d\n", global.event_qsize, EVENT_FIFO_LEN);
    /* The array is no longer needed, free the memory it consumes. */
    vPortFree( pxTaskStatusArray );

    if(type>1) {
        struct delta * ptr = global.event_start;
        uint16_t q = global.event_qsize;
        if(q > 25) q = 25;
        for(i=0;i<q;i++) {
            printf("%d time %u osc %d param %d - %f %d\n", i, ptr->time, ptr->osc, ptr->param, *(float *)&ptr->data, *(int *)&ptr->data);
            ptr = ptr->next;
        }
    }
    if(type>2) {
        // print out all the osc data
        printf("global: filter %f resonance %f volume %f status %d\n", global.filter_freq, global.resonance, global.volume, global.status);
        printf("mod global: filter %f resonance %f\n", mglobal.filter_freq, mglobal.resonance);
        for(uint8_t i=0;i<OSCS;i++) {
            printf("osc %d: status %d amp %f wave %d freq %f duty %f adsr_target %d lfo_target %d lfo source %d velocity %f E: %d,%d,%2.2f,%d step %f \n",
                i, synth[i].status, synth[i].amp, synth[i].wave, synth[i].freq, synth[i].duty, synth[i].adsr_target, synth[i].lfo_target, synth[i].lfo_source, 
                synth[i].velocity, synth[i].adsr_a, synth[i].adsr_d, synth[i].adsr_s, synth[i].adsr_r, synth[i].step);
            if(type>3) printf("mod osc %d: amp: %f, freq %f duty %f\n", i, msynth[i].amp, msynth[i].freq, msynth[i].duty);
        }
    }
}


   
void oscs_deinit() {
    free(block);
    free(fbl[0]); 
    free(fbl[1]); 
    free(fbl);
    free(scratchbuf[0]);
    free(scratchbuf[1]);
    free(scratchbuf);

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
         .dma_buf_count = 8, // TODO: check these numbers
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
void play_event(struct delta d) {
    if(d.param == MIDI_NOTE) { synth[d.osc].midi_note = *(uint16_t *)&d.data; synth[d.osc].freq = freq_for_midi_note(*(uint16_t *)&d.data); } 
    if(d.param == WAVE) synth[d.osc].wave = *(int16_t *)&d.data; 
    if(d.param == PHASE) synth[d.osc].phase = *(float *)&d.data;
    if(d.param == PATCH) synth[d.osc].patch = *(int16_t *)&d.data;
    if(d.param == DUTY) synth[d.osc].duty = *(float *)&d.data;
    if(d.param == FEEDBACK) synth[d.osc].feedback = *(float *)&d.data;
    if(d.param == FREQ) synth[d.osc].freq = *(float *)&d.data;
    if(d.param == ADSR_TARGET) synth[d.osc].adsr_target = (int8_t) d.data;

    if(d.param == ADSR_A) synth[d.osc].adsr_a = *(int16_t *)&d.data;
    if(d.param == ADSR_D) synth[d.osc].adsr_d = *(int16_t *)&d.data;
    if(d.param == ADSR_S) synth[d.osc].adsr_s = *(float *)&d.data;
    if(d.param == ADSR_R) synth[d.osc].adsr_r = *(int16_t *)&d.data;

    if(d.param == LFO_SOURCE) { synth[d.osc].lfo_source = *(int8_t *)&d.data; synth[*(int8_t *)&d.data].status = IS_LFO_SOURCE; }
    if(d.param == LFO_TARGET) synth[d.osc].lfo_target = *(int8_t *)&d.data; 

    // For global changes, just make the change, no need to update the per-osc synth
    if(d.param == VOLUME) global.volume = *(float *)&d.data;
    if(d.param == FILTER_FREQ) global.filter_freq = *(float *)&d.data;
    if(d.param == RESONANCE) global.resonance = *(float *)&d.data;

    // Triggers / envelopes 
    // The only way a sound is made is if velocity (note on) is >0.
    if(d.param == VELOCITY && *(float *)&d.data > 0) { // New note on (even if something is already playing on this osc)
        synth[d.osc].amp = *(float *)&d.data; // these could be decoupled, later
        synth[d.osc].velocity = *(float *)&d.data;
        synth[d.osc].status = AUDIBLE;
        // Take care of FM & KS first -- no special treatment for ADSR/LFO
        if(synth[d.osc].wave==FM) { fm_note_on(d.osc); } 
        else if(synth[d.osc].wave==KS) { ks_note_on(d.osc); } 
        else {
            // an osc came in with a note on.
            // Start the ADSR clock
            synth[d.osc].adsr_on_clock = esp_timer_get_time() / 1000;

            // Restart the waveforms, adjusting for phase if given
            if(synth[d.osc].wave==SINE) sine_note_on(d.osc);
            if(synth[d.osc].wave==SAW) saw_note_on(d.osc);
            if(synth[d.osc].wave==TRIANGLE) triangle_note_on(d.osc);
            if(synth[d.osc].wave==PULSE) pulse_note_on(d.osc);
            if(synth[d.osc].wave==PCM) pcm_note_on(d.osc);

            // Also trigger the LFO source, if we have one
            if(synth[d.osc].lfo_source >= 0) {
                if(synth[synth[d.osc].lfo_source].wave==SINE) sine_lfo_trigger(synth[d.osc].lfo_source);
                if(synth[synth[d.osc].lfo_source].wave==SAW) saw_lfo_trigger(synth[d.osc].lfo_source);
                if(synth[synth[d.osc].lfo_source].wave==TRIANGLE) triangle_lfo_trigger(synth[d.osc].lfo_source);
                if(synth[synth[d.osc].lfo_source].wave==PULSE) pulse_lfo_trigger(synth[d.osc].lfo_source);
                if(synth[synth[d.osc].lfo_source].wave==PCM) pcm_lfo_trigger(synth[d.osc].lfo_source);
            }

        }
    } else if(synth[d.osc].velocity > 0 && d.param == VELOCITY && *(float *)&d.data == 0) { // new note off
        synth[d.osc].velocity = 0;
        if(synth[d.osc].wave==FM) { fm_note_off(d.osc); }
        else if(synth[d.osc].wave==KS) { ks_note_off(d.osc); }
        else {
            // osc note off, start release
            synth[d.osc].adsr_on_clock = -1;
            synth[d.osc].adsr_off_clock = esp_timer_get_time() / 1000;
        }
    }

}

// Apply an LFO & ADSR, if any, to the osc
void hold_and_modify(uint8_t osc) {
    // Copy all the modifier variables
    msynth[osc].amp = synth[osc].amp;
    msynth[osc].duty = synth[osc].duty;
    msynth[osc].freq = synth[osc].freq;

    // Modify the synth params by scale -- ADSR scale is (original * scale)
    float scale = compute_adsr_scale(osc);
    if(synth[osc].adsr_target & TARGET_AMP) msynth[osc].amp = msynth[osc].amp * scale;
    if(synth[osc].adsr_target & TARGET_DUTY) msynth[osc].duty = msynth[osc].duty * scale;
    if(synth[osc].adsr_target & TARGET_FREQ) msynth[osc].freq = msynth[osc].freq * scale;
    if(synth[osc].adsr_target & TARGET_FILTER_FREQ) mglobal.filter_freq = (mglobal.filter_freq * scale);
    if(synth[osc].adsr_target & TARGET_RESONANCE) mglobal.resonance = mglobal.resonance * scale;

    // And the LFO -- LFO scale is (original + (original * scale))
    scale = compute_lfo_scale(osc);
    if(synth[osc].lfo_target & TARGET_AMP) msynth[osc].amp = msynth[osc].amp + (msynth[osc].amp * scale);
    if(synth[osc].lfo_target & TARGET_DUTY) msynth[osc].duty = msynth[osc].duty + (msynth[osc].duty * scale);
    if(synth[osc].lfo_target & TARGET_FREQ) msynth[osc].freq = msynth[osc].freq + (msynth[osc].freq * scale);
    if(synth[osc].lfo_target & TARGET_FILTER_FREQ) mglobal.filter_freq = mglobal.filter_freq + (mglobal.filter_freq * scale);
    if(synth[osc].lfo_target & TARGET_RESONANCE) mglobal.resonance = mglobal.resonance + (mglobal.resonance * scale);
}


void render_task() {
    uint8_t start, end, core;
    if(xPortGetCoreID() == 0) {
        start = 0; end = (OSCS/2); core = 0;
    } else {
        start = (OSCS/2); end = OSCS; core = 1;
    }
    printf("I'm rendering on core %d and i'm handling oscs %d up until %d\n", xPortGetCoreID(), start, end);
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for(uint16_t i=0;i<BLOCK_SIZE;i++) fbl[core][i] = 0; 
        for(uint8_t osc=start; osc<end; osc++) {
            if(synth[osc].status==AUDIBLE) { // skip oscs that are silent or LFO sources from playback
                hold_and_modify(osc); // apply ADSR / LFO
                if(synth[osc].wave == FM) render_fm(fbl[core], osc);
                if(synth[osc].wave == NOISE) render_noise(fbl[core], osc);
                if(synth[osc].wave == SAW) render_saw(fbl[core], scratchbuf[core], osc);
                if(synth[osc].wave == PULSE) render_pulse(fbl[core], scratchbuf[core], osc);
                if(synth[osc].wave == TRIANGLE) render_triangle(fbl[core], scratchbuf[core], osc);
                if(synth[osc].wave == SINE) render_sine(fbl[core], osc);
                if(synth[osc].wave == KS) render_ks(fbl[core], osc);
                if(synth[osc].wave == PCM) render_pcm(fbl[core], osc);
            }
        }
        // Tell the fill buffer task that i'm done rendering
        xTaskNotifyGive(fillbufferTask);
    }
}

// This takes scheduled events and plays them at the right time
void fill_audio_buffer_task() {
    while(1) {
        // Check to see which sounds to play 
        int64_t sysclock = esp_timer_get_time() / 1000;

        // put a semaphore around this so that the mcastTask doesn't touch these while i'm running  
        xSemaphoreTake(xQueueSemaphore, portMAX_DELAY);
        while(sysclock >= global.event_start->time) {
            play_event(*global.event_start);
            global.event_start->time = UINT32_MAX;
            global.event_qsize--;
            global.event_start = global.event_start->next;
        }
        xSemaphoreGive(xQueueSemaphore);

        // Save the current global synth state to the modifiers         
        mglobal.resonance = global.resonance;
        mglobal.filter_freq = global.filter_freq;

        // Tell the rendering threads to start rendering
        xTaskNotifyGive(renderTask[0]);
        xTaskNotifyGive(renderTask[1]);

        // And wait for each of them to come back
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

        for(int16_t i=0; i < BLOCK_SIZE; ++i) {
            // Soft clipping.
            float sign = 1; 

            // Mix all the oscillator buffers into one
            float fsample = fbl[0][i] + fbl[1][i];

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


// parse a received event string and turn the message into deltas on the queue
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
            if(mode=='a') e.amp=atof(message+start);
            if(mode=='A') parse_adsr(&e, message+start);
            if(mode=='b') e.feedback=atof(message+start);
            if(mode=='c') client = atoi(message + start); 
            if(mode=='d') e.duty=atof(message + start);
            if(mode=='D') {
                uint8_t type = atoi(message + start);
                show_debug(type); 
            }
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
                uint8_t osc = atoi(message + start); 
                if(osc > OSCS-1) { reset_oscs(); } else { reset_osc(osc); }
            }
            if(mode=='T') e.adsr_target = atoi(message + start); 
            if(mode=='v') e.osc=(atoi(message + start) % OSCS); // allow osc wraparound
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
    delay_ms(100);
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
            delay_ms(100);
        }
        // turn on midi
        global.status = MIDI_MODE | RUNNING;
        // Play a MIDI sound before shutting down oscs
        midi_tone();
        delay_ms(500);
        // stop rendering
        vTaskDelete(fillbufferTask);

        fm_deinit(); // have to free RAM to start the BLE stack
        oscs_deinit();
        midi_init();
    }
}

void power_monitor() {
    power_status_t power_status;

    const esp_err_t ret = power_read_status(&power_status);
    if(ret != ESP_OK)
        return;
    /*
    char buf[100];
    snprintf(buf, sizeof(buf),
        "powerStatus: power_source=\"%s\",charge_status=\"%s\",wall_v=%0.3f,battery_v=%0.3f\n",
        (power_status.power_source == POWER_SOURCE_WALL ? "wall" : "battery"),
        (power_status.charge_status == POWER_CHARGE_STATUS_CHARGING ? "charging" :
            (power_status.charge_status == POWER_CHARGE_STATUS_CHARGED ? "charged" : " discharging")),
        power_status.wall_voltage/1000.0,
        power_status.battery_voltage/1000.0
        );

    printf(buf);
    */
    battery_mask = 0;

    switch(power_status.charge_status) {
        case POWER_CHARGE_STATUS_CHARGED:
            battery_mask = battery_mask | BATTERY_STATE_CHARGED;
            break;
        case POWER_CHARGE_STATUS_CHARGING:
            battery_mask = battery_mask | BATTERY_STATE_CHARGING;
            break;
        case POWER_CHARGE_STATUS_DISCHARGING:
            battery_mask = battery_mask | BATTERY_STATE_DISCHARGING;
            break;        
    }

    float voltage = power_status.wall_voltage/1000.0;
    if(voltage > 3.95) battery_mask = battery_mask | BATTERY_VOLTAGE_4; else 
    if(voltage > 3.80) battery_mask = battery_mask | BATTERY_VOLTAGE_3; else 
    if(voltage > 3.60) battery_mask = battery_mask | BATTERY_VOLTAGE_2; else 
    if(voltage > 3.30) battery_mask = battery_mask | BATTERY_VOLTAGE_1;
}


void app_main() {
    check_init(&global_init, "global state");
    check_init(&esp_event_loop_create_default, "Event");
    // if power init fails, we don't have blinkinlabs board, set board level to 0
    if(check_init(&power_init, "power")) {
        printf("No power IC, assuming DIY Alles\n");
        global.board_level = DEVBOARD; 
    }
    if(global.board_level == ALLES_BOARD_V2) {
        printf("Detected revB Alles\n");
        TimerHandle_t power_monitor_timer = xTimerCreate(
            "power_monitor",
            pdMS_TO_TICKS(5000),
            pdTRUE,
            NULL,
            power_monitor);
        xTimerStart(power_monitor_timer, 0);
    }
    check_init(&setup_i2s, "i2s");
    check_init(&oscs_init, "oscs");
    check_init(&buttons_init, "buttons"); // only one button for the protoboard, 4 for the blinkinlabs

    wifi_manager_start();
    wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &wifi_connected);
    // Wait for wifi to connect
    while(!(global.status & WIFI_MANAGER_OK)) {
        delay_ms(1000);
        wifi_tone();
    };
    delay_ms(500);
    reset_oscs();


    create_multicast_ipv4_socket();

    // Pin the UDP task to the 2nd core so the audio / main core runs on its own without getting starved
    xTaskCreatePinnedToCore(&mcast_listen_task, "mcast_task", 4096, NULL, 2, &mcastTask, 1);

    // Allocate the FM RAM after the captive portal HTTP server is passed, as you can't have both at once
    fm_init();

    // Schedule a "turning on" sound
    bleep();

    // Spin this core until the power off button is pressed, parsing events and making sounds
    while(global.status & RUNNING) {
        delay_ms(10);
    }

    // If we got here, the power off button was pressed 
    // The idea here is we go into a low power deep sleep mode waiting for a GPIO pin to turn us back on
    // The battery can still charge during this, but let's turn off audio, wifi, multicast, midi 

    // Play a "turning off" sound
    debleep();
    delay_ms(500);

    // Stop mulitcast listening, wifi, midi
    vTaskDelete(mcastTask);
    esp_wifi_stop();
    if(global.status & MIDI_MODE) midi_deinit();


    // TODO: Where did these come from? JTAG?
    gpio_pullup_dis(14);
    gpio_pullup_dis(15);

    esp_sleep_enable_ext1_wakeup((1ULL<<BUTTON_WAKEUP),ESP_EXT1_WAKEUP_ALL_LOW);

    esp_deep_sleep_start();
}

