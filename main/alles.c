#include "alles.h"


void alles_parse_message(char *message, uint16_t length) {
    uint8_t mode = 0;
    int16_t client = -1;
    int64_t sync = -1;
    int8_t sync_index = -1;
    uint8_t ipv4 = 0;
    uint16_t start = 0;
    uint16_t c = 0;

    // Parse the AMY stuff out of the message first
    struct event e = amy_parse_message(message);
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
            if(for_me) amy_add_event(e);
        }
    }
}