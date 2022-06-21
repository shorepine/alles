// Alles multicast synthesizer
// Brian Whitman
// brian@variogr.am
#include "alles.h"
#include <pthread.h>
#include <unistd.h>

uint8_t board_level = ALLES_DESKTOP;
uint8_t status = RUNNING;
uint8_t debug_on = 0;

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
char *local_ip, *raw_file;

int main(int argc, char ** argv) {
    sync_init();
    start_amy();
    reset_oscs();

    // For now, indicate ip address via commandline
    local_ip = (char*)malloc(sizeof(char)*1025);
    local_ip[0] = 0;    
    raw_file = (char*)malloc(sizeof(char)*1025);
    raw_file[0] = 0;
    get_first_ip_address(local_ip);

    int opt;
    while((opt = getopt(argc, argv, ":i:d:c:r:o:lgh")) != -1) 
    { 
        switch(opt) 
        { 
            case 'i':
                strcpy(local_ip, optarg);
                break;
            case 'r': 
                strcpy(raw_file, optarg);
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
            case 'g':
                DEBUG = 1;
                break;
            case 'h':
                printf("usage: alles\n\t[-i multicast interface ip address, default, autodetect]\n");
                printf("\t[-d sound device id, use -l to list, default, autodetect]\n");
                printf("\t[-c sound channel, default -1 for all channels on device]\n");
                printf("\t[-o offset for client ID, use for multiple copies of this program on the same host, default is 0]\n");
                printf("\t[-l list all sound devices and exit]\n");
                printf("\t[-g show debug info]\n");
                printf("\t[-r output audio to specified raw file (1-channel 16-bit signed int, 44100Hz)\n");
                printf("\t[-h show this help and exit]\n");
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

#ifdef VIRTUAL_MIDI
    midi_init();
#endif
    // make a bleep
    bleep();
    usleep(1000*1000);
    reset_oscs();
    while(status & RUNNING) {
        usleep(THREAD_USLEEP);
    }

    // Play a "turning off" sound
    debleep();
    return 0;
}

