// multicast.c

#include <stdio.h>
#include <stddef.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "lwip/netdb.h"
#include "wifi.h"

#include "alles.h"

static const char *TAG = "multicast";
static const char *V4TAG = "mcast-ipv4";

int sock= -1;

extern void deserialize_event(char * message, uint16_t length);

extern esp_ip4_addr_t s_ip_addr;
extern uint8_t battery_mask;


int8_t ipv4_quartet;
int16_t client_id;
int64_t clocks[255];
int64_t ping_times[255];
uint8_t alive = 1;
int64_t computed_delta = 0; // can be negative no prob, but usually host is larger # than client
uint8_t computed_delta_set = 0; // have we set a delta yet?


int64_t last_ping_time = PING_TIME_MS; // do the first ping at 10s in to wait for other synths to announce themselves

static int socket_add_ipv4_multicast_group(bool assign_source_if) {
    struct ip_mreq imreq = { 0 };
    struct in_addr iaddr = { 0 };
    int err = 0;

    // Configure source interface
    esp_netif_ip_info_t ip_info = { 0 };
    err = esp_netif_get_ip_info(get_example_netif(), &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(V4TAG, "Failed to get IP address info. Error 0x%x", err);
        goto err;
    }
    inet_addr_from_ip4addr(&iaddr, &ip_info.ip);
    // Configure multicast address to listen to
    err = inet_aton(MULTICAST_IPV4_ADDR, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ESP_LOGE(V4TAG, "Configured IPV4 multicast address '%s' is invalid.", MULTICAST_IPV4_ADDR);
        // Errors in the return value have to be negative
        err = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
        ESP_LOGW(V4TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.", MULTICAST_IPV4_ADDR);
    }

    if (assign_source_if) {
        // Assign the IPv4 multicast source interface, via its IP
        // (only necessary if this socket is IPV4 only)
        err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
                         sizeof(struct in_addr));
        if (err < 0) {
            ESP_LOGE(V4TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
            goto err;
        }
    }

    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         &imreq, sizeof(struct ip_mreq));
    if (err < 0) {
        ESP_LOGE(V4TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
        goto err;
    }

 err:
    return err;
}

void create_multicast_ipv4_socket(void) {
    struct sockaddr_in saddr = { 0 };
    sock = -1;
    int err = 0;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(V4TAG, "Failed to create socket. Error %d", errno);
    }

    // Bind the socket to any address
    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(UDP_PORT);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
    if (err < 0) {
        ESP_LOGE(V4TAG, "Failed to bind socket. Error %d", errno);
    }

    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = MULTICAST_TTL;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));
    if (err < 0) {
        ESP_LOGE(V4TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
    }

    uint8_t loopback_val = 1;
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                     &loopback_val, sizeof(uint8_t));
    if (err < 0) {
        ESP_LOGE(V4TAG, "Failed to set IP_MULTICAST_LOOP. Error %d", errno);
    }

    // this is also a listening socket, so add it to the multicast
    // group for listening...
    err = socket_add_ipv4_multicast_group(true);

    // All set, socket is configured for sending and receiving
}

// Send a multicast message 
void mcast_send(char * message, uint16_t len) {
    char addrbuf[32] = { 0 };
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res;


    hints.ai_family = AF_INET; // For an IPv4 socket
    int err = getaddrinfo(MULTICAST_IPV4_ADDR, NULL, &hints, &res);
    if (err < 0) {
        ESP_LOGE(TAG, "getaddrinfo() failed for IPV4 destination address. error: %d", err);
    }
    if (res == 0) {
        ESP_LOGE(TAG, "getaddrinfo() did not return any addresses");
    }
    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(UDP_PORT);
    inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, addrbuf, sizeof(addrbuf)-1);
    //ESP_LOGI(TAG, "Sending to IPV4 multicast address %s:%d...",  addrbuf, UDP_PORT);
    err = sendto(sock, message, len, 0, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (err < 0) {
        ESP_LOGE(TAG, "IPV4 sendto failed. errno: %d", errno);
    }
}

void update_map(uint8_t client, uint8_t ipv4, int64_t time) {
    // I'm called when I get a sync response or a regular ping packet
    // I update a map of booted devices.

    //printf("[%d %d] Got a sync response client %d ipv4 %d time %lld\n",  ipv4_quartet, client_id, client , ipv4, time);
    clocks[ipv4] = time;
    int64_t my_sysclock = (esp_timer_get_time() / 1000) + 1; // we add one here to avoid local race conditions
    ping_times[ipv4] = my_sysclock;

    // Now I basically see what index I would be in the list of booted synths (clocks[i] > 0)
    // And I set my client_id to that index
    uint8_t my_new_client_id = 255;
    alive = 0;
    for(uint8_t i=0;i<255;i++) {
        if(clocks[i] > 0) { 
            if(my_sysclock < (ping_times[i] + (PING_TIME_MS * 2))) { // alive
                //printf("[%d %d] Checking my time %lld against ipv4 %d's of %lld, client_id now %d ping_time[%d] = %lld\n", 
                //    ipv4_quartet, client_id, my_sysclock, i, clocks[i], my_new_client_id, i, ping_times[i]);
                alive++;
            } else {
                printf("[ipv4 %d client %d] clock %d is dead, ping time was %lld time now is %lld.\n", ipv4_quartet, client_id, i, ping_times[i], my_sysclock);
                clocks[i] = 0;
                ping_times[i] = 0;
            }
        }
        if(my_sysclock > clocks[i]) my_new_client_id--;
    }
    if(client_id != my_new_client_id) {
        printf("[ipv4 %d client %d] Updating my client_id to %d. %d alive\n", ipv4_quartet, client_id, my_new_client_id, alive);
        client_id = my_new_client_id;
    }
    //printf("%d devices online\n", alive);
}

void handle_sync(int64_t time, int8_t index) {
    // I am called when I get an s message, which comes along with host time and index
    int64_t sysclock = esp_timer_get_time() / 1000;
    char message[100];
    // Before I send, i want to update the map locally
    update_map(client_id, ipv4_quartet, sysclock);
    // Send back sync message with my time and received sync index and my client id & battery status (if any)
    sprintf(message, "_s%lldi%dc%dr%dt%d", sysclock, index, client_id, ipv4_quartet, battery_mask);
    mcast_send(message, strlen(message));
    // Update computed delta (i could average these out, but I don't think that'll help too much)
    computed_delta = time - sysclock;
    computed_delta_set = 1;
}

void ping(int64_t sysclock) {
    char message[100];
    //printf("[%d %d] pinging with %lld\n", ipv4_quartet, client_id, sysclock);
    sprintf(message, "_s%lldi-1c%dr%dt%d", sysclock, client_id, ipv4_quartet, battery_mask);
    update_map(client_id, ipv4_quartet, sysclock);
    mcast_send(message, strlen(message));
    last_ping_time = sysclock;
}

void mcast_listen_task(void *pvParameters) {
    ipv4_quartet = esp_ip4_addr4(&s_ip_addr);
    client_id = -1; // for now
    for(uint8_t i=0;i<255;i++) { clocks[i] = 0; ping_times[i] = 0; }

    printf("Network listening running on core %d\n",xPortGetCoreID());
    while (1) {

        if (sock < 0) ESP_LOGE(TAG, "Failed to create IPv4 multicast socket");
        // set destination multicast addresses for sending from these sockets
        struct sockaddr_in sdestv4 = {
            .sin_family = PF_INET,
            .sin_port = htons(UDP_PORT),
        };
        // We know this inet_aton will pass because we did it above already
        inet_aton(MULTICAST_IPV4_ADDR, &sdestv4.sin_addr.s_addr);

        // Loop waiting for UDP received, and sending UDP packets if we don't see any.
        int err = 1;
        while (err > 0) {
            struct timeval tv = {
                .tv_sec = 2,
                .tv_usec = 0,
            };
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            int s = select(sock + 1, &rfds, NULL, NULL, &tv);
            if (s < 0) {
                ESP_LOGE(TAG, "Select failed: errno %d", errno);
                err = -1;
                break;
            }
            else if (s > 0) {
                if (FD_ISSET(sock, &rfds)) {
                    // Incoming datagram received
                    char recvbuf[MAX_RECEIVE_LEN];
                    struct sockaddr_in6 raddr; // Large enough for both IPv4 or IPv6
                    socklen_t socklen = sizeof(raddr);
                    int len = recvfrom(sock, recvbuf, sizeof(recvbuf)-1, 0,
                                       (struct sockaddr *)&raddr, &socklen);
                    if (len < 0) {
                        ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
                        err = -1;
                        break;
                    }
                    deserialize_event(recvbuf, (uint16_t)len);
                }
            }
            // Do a ping every so often
            int64_t sysclock = esp_timer_get_time() / 1000;
            if(sysclock > (last_ping_time+PING_TIME_MS)) ping(sysclock);
        }

        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, 0);
        close(sock);
    }

}
