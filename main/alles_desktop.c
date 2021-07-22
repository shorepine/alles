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

uint8_t battery_mask = 0;

extern uint8_t ipv4_quartet;
uint8_t quartet_offset = 0;
extern int get_first_ip_address(char *host);

char *local_ip;

int main(int argc, char ** argv) {
    sync_init();
    start_amy();
    reset_oscs();
    live_start();

    // For now, indicate ip address via commandline
    local_ip = (char*)malloc(sizeof(char)*1025);
    local_ip[0] = 0;    
    if(get_first_ip_address(local_ip) && argc < 2) {
        printf("couldn't get local ip. try ./alles local_ip\n");
        return 1;
    } else {
        if (argc>1) strcpy(local_ip, argv[1]);
    }
    if(argc>2) quartet_offset = atoi(argv[2]);

    create_multicast_ipv4_socket();
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, mcast_listen_task, NULL);

    // make a bleep
    bleep();

    while(status & RUNNING) {
        usleep(THREAD_USLEEP);
    }

    // Play a "turning off" sound
    debleep();
    return 0;
}

