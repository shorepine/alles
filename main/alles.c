#include "alles.h"


extern uint8_t battery_mask;
extern uint8_t ipv4_quartet;
extern char githash[8];
int16_t client_id;
int64_t clocks[255];
int64_t ping_times[255];
uint8_t alive = 1;

int32_t computed_delta = 0 ; // can be negative no prob, but usually host is larger # than client
uint8_t computed_delta_set = 0; // have we set a delta yet?

extern int64_t last_ping_time;

amy_err_t sync_init() {
    client_id = -1; // for now
    for(uint8_t i=0;i<255;i++) { clocks[i] = 0; ping_times[i] = 0; }
    return AMY_OK;
}



void update_map(int16_t client, uint8_t ipv4, int64_t time) {
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
    sprintf(message, "_U%lldi%dg%dr%dy%dZ", sysclock, index, client_id, ipv4_quartet, battery_mask);
    mcast_send(message, strlen(message));
    // Update computed delta (i could average these out, but I don't think that'll help too much)
    //int64_t old_cd = computed_delta;
    computed_delta = time - sysclock;
    computed_delta_set = 1;
    //if(old_cd != computed_delta) printf("Changed computed_delta from %lld to %lld on sync\n", old_cd, computed_delta);
}

// It's ok that r & y are used by AMY, this is only to return values
void ping(int64_t sysclock) {
    char message[100];
    //printf("[%d %d] pinging with %lld\n", ipv4_quartet, client_id, sysclock);
    sprintf(message, "_U%lldi-1g%dr%dy%dZ", sysclock, client_id, ipv4_quartet, battery_mask);
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

    uint32_t sysclock = amy_sysclock();


    // Parse the AMY stuff out of the message first
    struct event e = amy_parse_message(message);
    uint8_t sync_response = 0;


    if(AMY_IS_SET(e.time)) {
        if(amy_global.latency_ms != 0) {
            if(!computed_delta_set) {
                computed_delta = e.time - sysclock;
                fprintf(stderr,"setting computed delta to %" PRIi32" (e.time is %"PRIu32" sysclock %"PRIu32") max_drift_ms %"PRIu32" latency %"PRIu32"\n", computed_delta, e.time, sysclock, ALLES_MAX_DRIFT_MS, amy_global.latency_ms);
                computed_delta_set = 1;
            }
        }
    }
    // Then pull out any alles-specific modes in this message 
    while(c < length+1) {

        uint8_t b = message[c];
        if(b == '_' && c==0) sync_response = 1;
        if( ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) || b == 0) {  // new mode or end
            if(mode=='g') client = atoi(message + start); 
            if(sync_response) if(mode=='i') sync_index = atoi(message + start);
            if(sync_response) if(mode=='r') ipv4=atoi(message + start);
            if(mode=='U') sync = atol(message + start); 
            mode = b;
            start = c + 1;
        } 
        c++;
    }
    if(sync_response) {
        // If this is a sync response, let's update our local map of who is booted
        //printf("got sync response client %d ipv4 %d sync %lld\n", client, ipv4, sync);
        update_map(client, ipv4, sync);
        length = 0; // don't need to do the rest
    }
    // Only do this if we got some data
    if(length >0) {
        // adjust time in some useful way:
        // if we have a delta OR latency is 0 , AND got a time in this message, use it schedule it properly
        if(( (computed_delta_set || amy_global.latency_ms==0) && AMY_IS_SET(e.time))) {
            // OK, so check for potentially negative numbers here (or really big numbers-sysclock)
            int32_t potential_time = (int32_t)((int32_t)e.time - (int32_t)computed_delta) + amy_global.latency_ms;
            if(potential_time < 0 || (potential_time > (int32_t)(sysclock + amy_global.latency_ms + ALLES_MAX_DRIFT_MS))) {
                //fprintf(stderr,"recomputing time base: message came in with %lld, mine is %lld, computed delta was %lld\n", e.time, sysclock, computed_delta);
                computed_delta = e.time - sysclock;
                //fprintf(stderr,"computed delta now %lld\n", computed_delta);
            }
            e.time = (e.time - computed_delta) + amy_global.latency_ms;
        } else { // else play it asap
            e.time = sysclock + amy_global.latency_ms;
        }

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
            if(for_me) amy_add_event(e);
        }
    }
}