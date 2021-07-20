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
int main(int argc, char ** argv) {
    sync_init();
    start_amy();
    reset_oscs();
    live_start();
    // For now, indicate ip address via commandline
/*
    if(argc==1) create_multicast_ipv4_socket(NULL, 0);
    else if(argc==2) create_multicast_ipv4_socket(argv[1], 0);
    else if(argc==3) create_multicast_ipv4_socket(argv[1], atoi(argv[2]));
    else {
        printf("Did not understand arguments: ./alles [local_ip] [client_offset]\n"); 
        return 1;
    }
*/
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

