#include "alles.h"


extern uint8_t battery_mask;
extern uint8_t ipv4_quartet;
extern char githash[8];
int16_t client_id;
int64_t clocks[255];
int64_t ping_times[255];
uint8_t alive = 1;

extern int64_t computed_delta ; // can be negative no prob, but usually host is larger # than client
extern uint8_t computed_delta_set ; // have we set a delta yet?
extern int64_t last_ping_time;

amy_err_t sync_init() {
    client_id = -1; // for now
    for(uint8_t i=0;i<255;i++) { clocks[i] = 0; ping_times[i] = 0; }
    return AMY_OK;
}



void update_map(uint8_t client, uint8_t ipv4, int64_t time) {
    // I'm called when I get a sync response or a regular ping packet
    // I update a map of booted devices.

    //printf("[%d %d] Got a sync response client %d ipv4 %d time %lld\n",  ipv4_quartet, client_id, client , ipv4, time);
    clocks[ipv4] = time;
    int64_t my_sysclock = amy_sysclock();
    ping_times[ipv4] = my_sysclock;

    // Now I basically see what index I would be in the list of booted synths (clocks[i] > 0)
    // And I set my client_id to that index
    uint8_t last_alive = alive;
    uint8_t my_new_client_id = 255;
    alive = 0;
    for(uint8_t i=0;i<255;i++) {
        if(clocks[i] > 0) { 
            if(my_sysclock < (ping_times[i] + (PING_TIME_MS * 2))) { // alive
                //printf("[%d %d] Checking my time %lld against ipv4 %d's of %lld, client_id now %d ping_time[%d] = %lld\n", 
                //    ipv4_quartet, client_id, my_sysclock, i, clocks[i], my_new_client_id, i, ping_times[i]);
                alive++;
            } else {
                //printf("[ipv4 %d client %d] clock %d is dead, ping time was %lld time now is %lld.\n", ipv4_quartet, client_id, i, ping_times[i], my_sysclock);
                clocks[i] = 0;
                ping_times[i] = 0;
            }
            // If this is not me....
            if(i != ipv4_quartet) {
                // predicted time is what we think the alive node should be at by now
                int64_t predicted_time = (my_sysclock - ping_times[i]) + clocks[i];
                if(my_sysclock >= predicted_time) my_new_client_id--;
            } else {
                my_new_client_id--;
            }
        } else {
            // if clocks[] is 0, no need to check
            my_new_client_id--;
        }
    }
    if(client_id != my_new_client_id || last_alive != alive) {
        printf("[%d] my client_id is now %d. %d alive\n", ipv4_quartet, my_new_client_id, alive);
        client_id = my_new_client_id;
    }
}

void handle_sync(int64_t time, int8_t index) {
    // I am called when I get an s message, which comes along with host time and index
    int64_t sysclock = amy_sysclock();
    char message[100];
    // Before I send, i want to update the map locally
    update_map(client_id, ipv4_quartet, sysclock);
    // Send back sync message with my time and received sync index and my client id & battery status (if any)
    sprintf(message, "_s%lldi%dc%dr%dy%dZ", sysclock, index, client_id, ipv4_quartet, battery_mask);
    mcast_send(message, strlen(message));
    // Update computed delta (i could average these out, but I don't think that'll help too much)
    //int64_t old_cd = computed_delta;
    computed_delta = time - sysclock;
    computed_delta_set = 1;
    //if(old_cd != computed_delta) printf("Changed computed_delta from %lld to %lld on sync\n", old_cd, computed_delta);
}

void ping(int64_t sysclock) {
    char message[100];
    //printf("[%d %d] pinging with %lld\n", ipv4_quartet, client_id, sysclock);
    sprintf(message, "_s%lldi-1c%dr%dy%dZ", sysclock, client_id, ipv4_quartet, battery_mask);
    update_map(client_id, ipv4_quartet, sysclock);
    mcast_send(message, strlen(message));
    last_ping_time = sysclock;
}


void alles_parse_message(char *message, uint16_t length) {
    uint8_t mode = 0;
    int16_t client = -1;
    int64_t sync = -1;
    int8_t sync_index = -1;
    uint8_t ipv4 = 0;
    uint16_t start = 0;
    uint16_t c = 0;

    // Parse the AMY stuff out of the message first
    struct i_event e = amy_parse_message(message);
    uint8_t sync_response = 0;

    // Then pull out any alles-specific modes in this message - c,i,r,s, _
    while(c < length+1) {
        uint8_t b = message[c];
        if(b == '_' && c==0) sync_response = 1;
        if( ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) || b == 0) {  // new mode or end
            if(mode=='c') client = atoi(message + start); 
            if(mode=='i') sync_index = atoi(message + start);
            if(mode=='r') ipv4=atoi(message + start);
            if(mode=='s') sync = atol(message + start); 
            mode = b;
            start = c + 1;
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
        // TODO -- not that it matters, but the below could probably be one or two lines long instead
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
            if(for_me) amy_add_i_event(e);
        }
    }
}