// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"
#include <pthread.h>
#include <unistd.h>

uint8_t board_level = ALLES_DESKTOP;
uint8_t status = RUNNING;

// AMY synth states
extern struct state global;
extern uint32_t event_counter;
extern uint32_t message_counter;
extern int16_t channel;
extern int16_t device_id;

uint8_t battery_mask = 0;

extern uint8_t ipv4_quartet;
uint8_t quartet_offset = 0;
extern int get_first_ip_address(char *host);
extern void print_devices();
char *local_ip;

int main(int argc, char ** argv) {
    sync_init();
    start_amy();
    reset_oscs();

    // For now, indicate ip address via commandline
    local_ip = (char*)malloc(sizeof(char)*1025);
    local_ip[0] = 0;    
    get_first_ip_address(local_ip);

    int opt;
    while((opt = getopt(argc, argv, ":i:d:c:o:l")) != -1) 
    { 
        switch(opt) 
        { 
            case 'i':
                strcpy(local_ip, optarg);
                break;
            case 'd': 
                device_id = atoi(optarg);
                break;
            case 'c': 
                channel = atoi(optarg);
                break; 
            case 'o': 
                quartet_offset = atoi(optarg);
                break; 
            case 'l':
                print_devices();
                return 0;
                break;
            case ':': 
                printf("option needs a value\n"); 
                break; 
            case '?': 
                printf("unknown option: %c\n", optopt);
                break; 
        } 
    }
   
    live_start();
    create_multicast_ipv4_socket();
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, mcast_listen_task, NULL);

#ifdef MAC_MIDI
    midi_init();
#endif
    // make a bleep
    bleep();

    while(status & RUNNING) {
        usleep(THREAD_USLEEP);
    }

    // Play a "turning off" sound
    debleep();
    return 0;
}

