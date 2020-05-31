// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"

// midi spec
// one device can have a midi port optionally
// it can act as a broadcast channel (and also play its own audio)
// meaning, i send a message like channel 1, program 23, then note on, channel 1, etc 
// channel == booted ID
// program == sound -- SINE, SQUARE, SAW, TRIANGLE, NOISE, KS, FM0, FM1 -- maybe do banks for FM? 
// right bank 0 is default set here ^
// bank 1 is FM bank 0 and so on 

extern void serialize_event(struct event e, uint16_t client);
extern struct event default_event();

QueueHandle_t uart_queue;
uint8_t midi_voice = 0;
uint8_t program_bank = 0;
uint8_t program = 0;
uint8_t note_map[VOICES];

void read_midi() {
    const int uart_num = UART_NUM_2;
    uint8_t data[128];
    int length = 0;
    while(1) {
        ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&length));
        length = uart_read_bytes(uart_num, data, length, 100);
        for(uint16_t byte=0;byte<length;byte++) {
            if(byte+1 < length) { // ensure this message has at least one byte after
                uint8_t channel = data[byte] & 0x0F;
                uint8_t message = data[byte] & 0xF0;
                uint8_t data1 = data[byte+1];
                if(message == 0x90) {
                    uint8_t data2 = data[byte+2];
                    struct event e = default_event();
                    e.time = esp_timer_get_time() / 1000; // play "now" (use this guy as master clock)
                    // select wave type -- if bank is > 0 it's FM, and patch = ((program_bank-1) * 128) + program
                    if(program_bank > 0) {
                        e.wave = FM;
                        e.patch = ((program_bank-1) * 128) + program;
                    } else { // otherwise wave type just 0-6 like UDP messages
                        e.wave = program;
                    }
                    e.voice = midi_voice;
                    e.midi_note = data1;
                    e.velocity = data2;
                    e.amp = 0.2;
                    // serialize it and send it over UDP to everyone
                    note_map[midi_voice] = data1;
                    if(channel==0) {
                        serialize_event(e, 256); // send to everyone booted
                    } else {
                        serialize_event(e, channel-1); // send to just the one specified
                    }
                    // Just iterate the voice for polyphony. for now
                    midi_voice = (midi_voice+1) % VOICES;
                    byte = byte + 2;
                } else if(message == 0x80) {
                    // note off
                    uint8_t data2 = data[byte+2];
                    byte = byte + 2;
                    // for now, only handle broadcast note offs... will have to refactor if i go down this path farther
                    // assume this is the new envelope command we keep putting off-- like "$e30a0" where e is an event number 
                    for(uint8_t v=0;v<VOICES;v++) {
                        if(note_map[v] == data1) {
                            struct event e= default_event();
                            e.amp = 0;
                            e.voice = v;
                            e.velocity = data2; // note off velocity, not used
                            serialize_event(e, 256);
                        }
                    }
                    
                } else if(message == 0xC0) { // program change 
                    program = data1;
                    byte = byte + 1;
                } else if(message == 0xB0) {
                    // control change
                    uint8_t data2 = data[byte+2];
                    if(data1 == 0x00) { // bank select for program change
                        program_bank = data2;
                    }
                    byte = byte + 2;
                }
            } else {
                // Some other midi message, or garbage, I will skip this for now
            }
        }            
    }
}

void setup_midi() {
    // Setup UART2 to listen for MIDI messages 
    const int uart_num = UART_NUM_2;
    uart_config_t uart_config = {
        .baud_rate = 31250,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    for(uint8_t v=0;v<VOICES;v++) {
        note_map[v] = 0;
    }
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    // TX, RX, CTS/RTS -- Only care about RX here, pin 19 for now
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 18, 19, 21, 5));

    const int uart_buffer_size = (1024 * 2);
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, \
                                          uart_buffer_size, 10, &uart_queue, 0));
}

