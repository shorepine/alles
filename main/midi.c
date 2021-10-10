#include "alles.h"
#ifdef VIRTUAL_MIDI
// Handle MIDI events from virtual MIDI  


extern struct event default_event();
extern void mcast_send(char * message, uint16_t len);


uint8_t midi_osc = 0;
#define CHANNELS 16
uint8_t program_bank[CHANNELS];
uint8_t program[CHANNELS];

extern void* mac_midi_run(void *varargp);
#include <pthread.h>

// take an event and make it a string and send it to everyone!
// This is only used for MIDI relay, so we only need to send events that MIDI parses.
void serialize_event(struct event e, uint16_t client) {
    char message[MAX_RECEIVE_LEN];
    // TODO -- patch / wave
    sprintf(message, "c%dl%fn%dv%dt%lldZ", 
        client, e.velocity, e.midi_note, e.osc, e.time );
    //printf("sending %s\n", message);
    mcast_send(message, strlen(message));
}


// TODO don't schedule notes to me, or ignore them 
// TODO this "synth" should not get a client ID  ????
void callback_midi_message_received(uint8_t source, uint16_t timestamp, uint8_t midi_status, uint8_t *remaining_message, size_t len) {
    // source is 1 if this came in through uart... (0 if local?)
    //printf("got midi message source %d: status %02x -- ", source, midi_status);
    //for(int i=0;i<len;i++) printf("%02x ", remaining_message[i]);
    //printf("\n");
    uint8_t channel = midi_status & 0x0F;
    uint8_t message = midi_status & 0xF0;
    if(len > 0) {
        uint8_t data1 = remaining_message[0];
        if(message == 0x90) {  // note on 
            uint8_t data2 = remaining_message[1];
            //printf("note on channel %d note %d velocity %d\n", channel, data1, data2);
            struct event e = default_event();
            e.time = get_sysclock();
            if(program_bank[channel] > 0) {
                e.wave = ALGO;
                //e.patch = ((program_bank[channel]-1) * 128) + program[channel];
            } else {
                //e.wave = program[channel];
            }
            e.osc = 0; // midi_osc;
            e.midi_note = data1;
            e.velocity = (float)data2/127.0;
            e.amp = 1; // for now
            //note_map[midi_osc] = data1;
            if(channel == 0) {
                serialize_event(e,256);
            } else {
                serialize_event(e, channel - 1);
            }
            //midi_osc = (midi_osc + 1) % (OSCS);

        } else if (message == 0x80) { 
            // note off
            struct event e = default_event();
            e.velocity = 0;
            e.time = get_sysclock();
            e.osc = 0;
            if(channel == 0) {
                serialize_event(e,256);
            } else {
                serialize_event(e, channel - 1);
            }

            //uint8_t data2 = remaining_message[1];
            //printf("note off channel %d note %d\n", channel, data1);
            // for now, only handle broadcast note offs... will have to refactor if i go down this path farther
            // assume this is the new envelope command we keep putting off-- like "$e30a0" where e is an event number 

            //for(uint8_t v=0;v<OSCS;v++) {
             //   if(note_map[v] == data1) {
            /*
                    struct event e = default_event();
                    e.amp = 0;
                    e.osc = 0;
                    e.time = esp_timer_get_time() / 1000;
                    e.velocity = data2; // note off velocity, not used... yet
                    serialize_event(e, 256);
                    */
              //  }
           // }
                        
        } else if(message == 0xC0) { // program change 
            printf("program change channel %d to %d\n", channel, data1);
            program[channel] = data1;
        } else if(message == 0xB0) {
            // control change
            uint8_t data2 = remaining_message[1];
            if(data1 == 0x20) { // fine mode for bank change, logic uses this
                program_bank[channel] = data2;
                printf("bank change fine channel %d to %d\n", channel, data2);
            }
            // Bank select for program change
            if(data1 == 0x00) { 
                // if this is 0 because we're using coarse/fine, the fine will immediate overwrite it, nbd
                program_bank[channel] = data2;
                printf("bank change coarse channel %d to %d\n", channel, data2);
            }
            // feedback
            // duty cycle
            // pitch bend (?) 
            // amplitude / volume
        }
    }
}





void midi_deinit() {
    // kill the thread
}

void midi_init() {
    for(uint8_t c=0;c<CHANNELS;c++) {
        program_bank[c] = 0;
        program[c] = 0;
    }
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, mac_midi_run, NULL);
}

#endif // ifdef VIRTUAL_MIDI

