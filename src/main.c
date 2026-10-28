#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include "debugScreen.h"

#define MQTT_PORT          1883
#define MAX_PACKET         2048
#define MAX_CLIENTS        4
#define MAX_SUBS            16
#define MAX_SUBSCRIPTIONS  (MAX_CLIENTS * MAX_SUBS)
#define MAX_TOPIC           128
#define NET_MEMORY_SIZE    (1024 * 1024)

static SceUID log_mutex = -1;
static SceUID clients_mutex = -1;
static SceUID sub_mutex = -1;

static int active_clients[MAX_CLIENTS];

typedef struct {
    int client;
    char topic[MAX_TOPIC];
} Subscription;

static Subscription subscriptions[MAX_SUBSCRIPTIONS];

/* --------------------------------------------------------- */
/* Logging                                                    */
/* --------------------------------------------------------- */

static void log_msg(const char *msg)
{
    if (log_mutex >= 0) {
        sceKernelLockMutex(log_mutex, 1, NULL);
    }

    printf("%s\n", msg);

    if (log_mutex >= 0) {
        sceKernelUnlockMutex(log_mutex, 1);
    }
}

/* --------------------------------------------------------- */
/* Socket helpers                                             */
/* --------------------------------------------------------- */

static int recv_exact(int client, void *buffer, int length)
{
    unsigned char *p = (unsigned char *)buffer;
    int received = 0;

    while (received < length) {
        int r = sceNetRecv(
            client,
            p + received,
            length - received,
            0
        );

        if (r <= 0) {
            return -1;
        }

        received += r;
    }

    return received;
}

static int send_exact(int client, const void *buffer, int length)
{
    const unsigned char *p = (const unsigned char *)buffer;
    int sent = 0;

    while (sent < length) {
        int r = sceNetSend(
            client,
            p + sent,
            length - sent,
            0
        );

        if (r <= 0) {
            return -1;
        }

        sent += r;
    }

    return sent;
}

/* --------------------------------------------------------- */
/* MQTT packet reader                                         */
/* Returns body length; header is written to *header.         */
/* --------------------------------------------------------- */

static int read_packet(
    int client,
    unsigned char *header,
    unsigned char *body,
    int capacity
)
{
    unsigned char encoded;
    int remaining = 0;
    int multiplier = 1;
    int count = 0;

    if (recv_exact(client, header, 1) < 0) {
        return -1;
    }

    do {
        if (count >= 4) {
            return -1;
        }

        if (recv_exact(client, &encoded, 1) < 0) {
            return -1;
        }

        remaining += (encoded & 127) * multiplier;
        multiplier *= 128;
        count++;

    } while ((encoded & 128) != 0);

    if (remaining < 0 || remaining > capacity) {
        return -1;
    }

    if (remaining > 0) {
        if (recv_exact(client, body, remaining) < 0) {
            return -1;
        }
    }

    return remaining;
}

/* --------------------------------------------------------- */
/* Client slots                                               */
/* --------------------------------------------------------- */

static int reserve_client(int client)
{
    int i;
    int result = -1;

    sceKernelLockMutex(clients_mutex, 1, NULL);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (active_clients[i] == 0) {
            active_clients[i] = client;
            result = 0;
            break;
        }
    }

    sceKernelUnlockMutex(clients_mutex, 1);

    return result;
}

static void release_client(int client)
{
    int i;

    sceKernelLockMutex(clients_mutex, 1, NULL);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (active_clients[i] == client) {
            active_clients[i] = 0;
            break;
        }
    }

    sceKernelUnlockMutex(clients_mutex, 1);

    sceKernelLockMutex(sub_mutex, 1, NULL);

    for (i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].client == client) {
            subscriptions[i].client = 0;
            subscriptions[i].topic[0] = '\0';
        }
    }

    sceKernelUnlockMutex(sub_mutex, 1);
}

/* --------------------------------------------------------- */
/* MQTT CONNECT                                               */
/* --------------------------------------------------------- */

static int handle_connect(
    int client,
    const unsigned char *body,
    int length
)
{
    int pos = 0;
    unsigned int protocol_length;
    unsigned char connect_flags;
    unsigned char connack[4] = {
        0x20, 0x02, 0x00, 0x00
    };

    if (length < 10) {
        return -1;
    }

    protocol_length =
        ((unsigned int)body[pos] << 8) |
        body[pos + 1];
    pos += 2;

    if (protocol_length != 4 ||
        pos + 4 + 1 + 1 + 2 > (unsigned int)length) {
        return -1;
    }

    if (memcmp(body + pos, "MQTT", 4) != 0) {
        return -1;
    }

    pos += 4;

    /* This prototype supports MQTT 3.1.1 only. */
    if (body[pos++] != 4) {
        connack[3] = 0x01;
        send_exact(client, connack, sizeof(connack));
        return -1;
    }

    connect_flags = body[pos++];

    /* Reject reserved bit and unsupported username/password. */
    if ((connect_flags & 0x01) != 0 ||
        (connect_flags & 0xC0) != 0) {
        return -1;
    }

    /* Skip keep-alive. */
    pos += 2;

    if (pos + 2 > length) {
        return -1;
    }

    /* Validate that a client ID field is present. */
    {
        unsigned int id_length =
            ((unsigned int)body[pos] << 8) |
            body[pos + 1];

        pos += 2;

        if (pos + (int)id_length > length) {
            return -1;
        }
    }

    if (send_exact(client, connack, sizeof(connack)) < 0) {
        return -1;
    }

    log_msg("MQTT: CONNECT accepted");
    return 0;
}

/* --------------------------------------------------------- */
/* Forward QoS 0 PUBLISH to clients subscribed to exact topic */
/* --------------------------------------------------------- */

static void forward_publish(
    int publisher,
    const unsigned char *packet,
    int packet_length,
    const char *topic
)
{
    int i;

    (void)publisher;

    sceKernelLockMutex(sub_mutex, 1, NULL);

    for (i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (subscriptions[i].client != 0 &&
            strcmp(subscriptions[i].topic, topic) == 0) {

            /*
             * Send the complete original MQTT packet.
             * This prototype forwards QoS 0 only.
             */
            if (send_exact(
                    subscriptions[i].client,
                    packet,
                    packet_length
                ) < 0) {
                /*
                 * The client thread will remove stale
                 * subscriptions when it detects disconnect.
                 */
            }
        }
    }

    sceKernelUnlockMutex(sub_mutex, 1);
}

/* --------------------------------------------------------- */
/* MQTT PUBLISH                                               */
/* --------------------------------------------------------- */

static int handle_publish(
    int client,
    unsigned char header,
    const unsigned char *body,
    int length
)
{
    unsigned int topic_length;
    char topic[MAX_TOPIC];
    unsigned char packet[MAX_PACKET];
    int packet_length;
    int qos;

    qos = (header >> 1) & 0x03;

    /* Only QoS 0 and non-retained messages are supported. */
    if (qos != 0 || (header & 0x01) != 0) {
        return -1;
    }

    if (length < 2) {
        return -1;
    }

    topic_length =
        ((unsigned int)body[0] << 8) |
        body[1];

    if (topic_length == 0 ||
        topic_length >= MAX_TOPIC ||
        2 + (int)topic_length > length) {
        return -1;
    }

    memcpy(topic, body + 2, topic_length);
    topic[topic_length] = '\0';

    /*
     * Reconstruct the complete MQTT packet so that it can
     * be forwarded to subscribers.
     */
    packet[0] = header;

    {
        int remaining = length;
        int pos = 1;

        do {
            unsigned char digit =
                (unsigned char)(remaining % 128);

            remaining /= 128;

            if (remaining > 0) {
                digit |= 0x80;
            }

            packet[pos++] = digit;
        } while (remaining > 0);

        if (pos + length > MAX_PACKET) {
            return -1;
        }

        memcpy(packet + pos, body, length);
        packet_length = pos + length;
    }

    forward_publish(
        client,
        packet,
        packet_length,
        topic
    );

    return 0;
}

/* --------------------------------------------------------- */
/* MQTT SUBSCRIBE                                             */
/* Supports exact topics and requested QoS 0 only.           */
/* --------------------------------------------------------- */

static int handle_subscribe(
    int client,
    const unsigned char *body,
    int length
)
{
    unsigned char response[2 + MAX_SUBS];
    unsigned int packet_id;
    int pos = 2;
    int count = 0;
    int response_pos = 4;

    if (length < 5) {
        return -1;
    }

    packet_id =
        ((unsigned int)body[0] << 8) |
        body[1];

    response[0] = 0x90;
    response[1] = 0;

    while (pos + 3 <= length && count < MAX_SUBS) {
        unsigned int topic_length;
        char topic[MAX_TOPIC];
        unsigned char requested_qos;
        int slot = -1;
        int i;

        topic_length =
            ((unsigned int)body[pos] << 8) |
            body[pos + 1];

        pos += 2;

        if (topic_length == 0 ||
            topic_length >= MAX_TOPIC ||
            pos + (int)topic_length + 1 > length) {
            return -1;
        }

        memcpy(topic, body + pos, topic_length);
        topic[topic_length] = '\0';
        pos += topic_length;

        requested_qos = body[pos++];

        if (requested_qos != 0 ||
            strchr(topic, '+') != NULL ||
            strchr(topic, '#') != NULL) {
            response[response_pos++] = 0x80;
            count++;
            continue;
        }

        sceKernelLockMutex(sub_mutex, 1, NULL);

        /* Reuse an existing subscription for this client. */
        for (i = 0; i < MAX_SUBSCRIPTIONS; i++) {
            if (subscriptions[i].client == client &&
                strcmp(subscriptions[i].topic, topic) == 0) {
                slot = i;
                break;
            }
        }

        /* Otherwise find an empty slot. */
        if (slot < 0) {
            for (i = 0; i < MAX_SUBSCRIPTIONS; i++) {
                if (subscriptions[i].client == 0) {
                    slot = i;
                    break;
                }
            }
        }

        if (slot >= 0) {
            subscriptions[slot].client = client;
            strncpy(
                subscriptions[slot].topic,
                topic,
                MAX_TOPIC - 1
            );
            subscriptions[slot].topic[MAX_TOPIC - 1] = '\0';
        }

        sceKernelUnlockMutex(sub_mutex, 1);

        response[response_pos++] = slot >= 0 ? 0x00 : 0x80;
        count++;
    }

    if (count == 0 || pos != length) {
        return -1;
    }

    response[2] = (unsigned char)((packet_id >> 8) & 0xFF);
    response[3] = (unsigned char)(packet_id & 0xFF);
    response[1] = (unsigned char)(2 + count);

    if (send_exact(client, response, response_pos) < 0) {
        return -1;
    }

    log_msg("MQTT: SUBSCRIBE handled");
    return 0;
}

/* --------------------------------------------------------- */
/* Client thread                                              */
/* --------------------------------------------------------- */

static int handle_client(int client)
{
    unsigned char header;
    unsigned char body[MAX_PACKET];
    int length;
    int connected = 0;

    for (;;) {
        length = read_packet(
            client,
            &header,
            body,
            sizeof(body)
        );

        if (length < 0) {
            break;
        }

        switch ((header >> 4) & 0x0F) {
            case 1: /* CONNECT */
                if (connected || handle_connect(
                        client,
                        body,
                        length
                    ) < 0) {
                    goto done;
                }

                connected = 1;
                break;

            case 3: /* PUBLISH */
                if (!connected ||
                    handle_publish(
                        client,
                        header,
                        body,
                        length
                    ) < 0) {
                    goto done;
                }
                break;

            case 8: /* SUBSCRIBE */
                if (!connected ||
                    header != 0x82 ||
                    handle_subscribe(
                        client,
                        body,
                        length
                    ) < 0) {
                    goto done;
                }
                break;

            case 12: /* PINGREQ */
            {
                const unsigned char pingresp[2] = {
                    0xD0, 0x00
                };

                if (!connected || length != 0 ||
                    send_exact(
                        client,
                        pingresp,
                        sizeof(pingresp)
                    ) < 0) {
                    goto done;
                }
                break;
            }

            case 14: /* DISCONNECT */
                goto done;

            default:
                /* Unsupported MQTT packet type. */
                goto done;
        }
    }

done:
    release_client(client);
    sceNetSocketClose(client);
    log_msg("MQTT: client disconnected");
    return 0;
}

static int client_thread(SceSize args, void *argp)
{
    int client;

    (void)args;

    if (argp == NULL) {
        sceKernelExitDeleteThread(1);
        return 1;
    }

    /*
     * sceKernelStartThread copies the argument bytes into
     * the new thread's argument area. Do not free argp.
     */
    client = *(const int *)argp;

    handle_client(client);

    sceKernelExitDeleteThread(0);
    return 0;
}

/* --------------------------------------------------------- */
/* Main                                                       */
/* --------------------------------------------------------- */

int main(void)
{
    SceNetInitParam net_init;
    void *net_memory;
    int server;
    int i;
    SceNetSockaddrIn address;

    debugScreenInit();

    printf("Vita MQTT Broker\n");
    printf("Starting...\n");

    for (i = 0; i < MAX_CLIENTS; i++) {
        active_clients[i] = 0;
    }

    memset(subscriptions, 0, sizeof(subscriptions));

    log_mutex = sceKernelCreateMutex(
        "mqtt_log_mutex", 0, 0, NULL
    );

    clients_mutex = sceKernelCreateMutex(
        "mqtt_clients_mutex", 0, 0, NULL
    );

    sub_mutex = sceKernelCreateMutex(
        "mqtt_sub_mutex", 0, 0, NULL
    );

    if (log_mutex < 0 ||
        clients_mutex < 0 ||
        sub_mutex < 0) {
        printf("ERROR: mutex creation failed\n");
        sceKernelDelayThread(5 * 1000 * 1000);
        sceKernelExitProcess(1);
        return 1;
    }

    net_memory = malloc(NET_MEMORY_SIZE);

    if (net_memory == NULL) {
        log_msg("ERROR: no memory for network");
        sceKernelExitProcess(1);
        return 1;
    }

    memset(&net_init, 0, sizeof(net_init));
    net_init.memory = net_memory;
    net_init.size = NET_MEMORY_SIZE;
    net_init.flags = 0;

    if (sceNetInit(&net_init) < 0) {
        log_msg("ERROR: sceNetInit failed");
        free(net_memory);
        sceKernelExitProcess(1);
        return 1;
    }

    if (sceNetCtlInit() < 0) {
        log_msg("WARNING: sceNetCtlInit failed");
    }

    server = sceNetSocket(
        "mqtt_server",
        SCE_NET_AF_INET,
        SCE_NET_SOCK_STREAM,
        0
    );

    if (server < 0) {
        log_msg("ERROR: socket creation failed");
        sceKernelExitProcess(1);
        return 1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = SCE_NET_AF_INET;
    address.sin_port = sceNetHtons(MQTT_PORT);
    address.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);

    if (sceNetBind(
            server,
            (SceNetSockaddr *)&address,
            sizeof(address)
        ) < 0) {
        log_msg("ERROR: bind failed");
        sceNetSocketClose(server);
        sceKernelExitProcess(1);
        return 1;
    }

    if (sceNetListen(server, MAX_CLIENTS) < 0) {
        log_msg("ERROR: listen failed");
        sceNetSocketClose(server);
        sceKernelExitProcess(1);
        return 1;
    }

    printf("Listening on TCP port %d\n", MQTT_PORT);
    printf("Waiting for MQTT clients...\n");

    for (;;) {
        SceNetSockaddrIn peer;
        unsigned int peer_length = sizeof(peer);
        int client;
        SceUID tid;

        client = sceNetAccept(
            server,
            (SceNetSockaddr *)&peer,
            &peer_length
        );

        if (client < 0) {
            sceKernelDelayThread(100 * 1000);
            continue;
        }

        if (reserve_client(client) < 0) {
            log_msg("MQTT: client limit reached");
            sceNetSocketClose(client);
            continue;
        }

        tid = sceKernelCreateThread(
            "mqtt_client",
            client_thread,
            0x10000100,
            64 * 1024,
            0,
            0,
            NULL
        );

        if (tid < 0) {
            release_client(client);
            sceNetSocketClose(client);
            log_msg("ERROR: create client thread failed");
            continue;
        }

        /*
         * Pass the integer value by copying its bytes.
         * No heap allocation and no free() in client_thread.
         */
        if (sceKernelStartThread(
                tid,
                sizeof(client),
                &client
            ) < 0) {
            sceKernelDeleteThread(tid);
            release_client(client);
            sceNetSocketClose(client);
            log_msg("ERROR: start client thread failed");
            continue;
        }

        log_msg("MQTT: client connected");
    }

    return 0;
}