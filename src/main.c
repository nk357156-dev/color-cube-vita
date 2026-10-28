
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include "debugScreen.h"

#define MQTT_PORT 1883
#define BACKLOG 8
#define MAX_PACKET 2048
#define MAX_CLIENTS 4
#define MAX_SUBS 32
#define MAX_TOPIC 127
#define MAX_PAYLOAD 1024

static unsigned char net_pool[256 * 1024];

typedef struct {
    int socket;
    char topic[MAX_TOPIC + 1];
} Subscription;

static Subscription subscriptions[MAX_SUBS];
static SceUID sub_mutex = -1;
static SceUID log_mutex = -1;

static void log_msg(const char *msg)
{
    if (log_mutex >= 0)
        sceKernelLockMutex(log_mutex, 1, NULL);

    psvDebugScreenPrintf("%s\n", msg);

    if (log_mutex >= 0)
        sceKernelUnlockMutex(log_mutex, 1);
}

static int recv_exact(int sock, unsigned char *buf, int len)
{
    int total = 0;

    while (total < len) {
        int n = sceNetRecv(
            sock, buf + total, (unsigned int)(len - total), 0
        );
        if (n <= 0)
            return -1;
        total += n;
    }

    return total;
}

static int send_exact(int sock, const unsigned char *buf, int len)
{
    int total = 0;

    while (total < len) {
        int n = sceNetSend(
            sock, buf + total, (unsigned int)(len - total), 0
        );
        if (n <= 0)
            return -1;
        total += n;
    }

    return total;
}

static int read_remaining_length(int sock, unsigned int *value)
{
    unsigned int result = 0;
    unsigned int multiplier = 1;
    unsigned char digit;
    int count = 0;

    do {
        if (recv_exact(sock, &digit, 1) < 0)
            return -1;

        result += (unsigned int)(digit & 127) * multiplier;
        count++;

        if (count > 4)
            return -1;

        if (!(digit & 128))
            break;

        multiplier *= 128;
    } while (1);

    *value = result;
    return 0;
}

static int encode_remaining_length(
    unsigned int value, unsigned char *out)
{
    int count = 0;

    do {
        unsigned char digit = value % 128;
        value /= 128;

        if (value > 0)
            digit |= 128;

        out[count++] = digit;
    } while (value > 0 && count < 4);

    return count;
}

static void safe_text(
    const unsigned char *src,
    unsigned int len,
    char *dst,
    unsigned int dst_size)
{
    unsigned int i, out = 0;

    if (dst_size == 0)
        return;

    for (i = 0; i < len && out + 1 < dst_size; i++) {
        unsigned char c = src[i];

        if (c >= 32 && c <= 126)
            dst[out++] = (char)c;
        else if (c == '\n' || c == '\r' || c == '\t')
            dst[out++] = ' ';
        else
            dst[out++] = '?';
    }

    dst[out] = '\0';
}

static void remove_subscriptions(int sock)
{
    int i;

    sceKernelLockMutex(sub_mutex, 1, NULL);

    for (i = 0; i < MAX_SUBS; i++) {
        if (subscriptions[i].socket == sock) {
            subscriptions[i].socket = -1;
            subscriptions[i].topic[0] = '\0';
        }
    }

    sceKernelUnlockMutex(sub_mutex, 1);
}

static int add_subscription(int sock, const char *topic)
{
    int i, free_slot = -1;

    sceKernelLockMutex(sub_mutex, 1, NULL);

    for (i = 0; i < MAX_SUBS; i++) {
        if (subscriptions[i].socket == sock &&
            strcmp(subscriptions[i].topic, topic) == 0) {
            sceKernelUnlockMutex(sub_mutex, 1);
            return 0;
        }

        if (subscriptions[i].socket == -1 && free_slot == -1)
            free_slot = i;
    }

    if (free_slot >= 0) {
        subscriptions[free_slot].socket = sock;
        strncpy(subscriptions[free_slot].topic, topic, MAX_TOPIC);
        subscriptions[free_slot].topic[MAX_TOPIC] = '\0';
    }

    sceKernelUnlockMutex(sub_mutex, 1);

    return free_slot >= 0 ? 0 : -1;
}

/*
 * Формируем MQTT PUBLISH с QoS 0.
 * Отправка выполняется под блокировкой таблицы подписок,
 * чтобы сокет не был одновременно удалён из таблицы.
 */
static int forward_publish(
    const char *topic,
    const unsigned char *payload,
    unsigned int payload_len)
{
    unsigned char out[MAX_PACKET + 8];
    unsigned int topic_len = (unsigned int)strlen(topic);
    unsigned int remaining = 2 + topic_len + payload_len;
    int pos = 0;
    int i;
    int matched = 0;
    int failed = 0;
    char line[192];

    if (remaining > MAX_PACKET) {
        log_msg("[MQTT] Forward packet too large");
        return -1;
    }

    out[pos++] = 0x30;
    pos += encode_remaining_length(remaining, out + pos);
    out[pos++] = (unsigned char)(topic_len >> 8);
    out[pos++] = (unsigned char)(topic_len & 255);

    memcpy(out + pos, topic, topic_len);
    pos += (int)topic_len;

    if (payload_len > 0) {
        memcpy(out + pos, payload, payload_len);
        pos += (int)payload_len;
    }

    sceKernelLockMutex(sub_mutex, 1, NULL);

    for (i = 0; i < MAX_SUBS; i++) {
        if (subscriptions[i].socket >= 0 &&
            strcmp(subscriptions[i].topic, topic) == 0) {
            int dest = subscriptions[i].socket;

            if (send_exact(dest, out, pos) >= 0) {
                matched++;
            } else {
                failed++;
            }
        }
    }

    sceKernelUnlockMutex(sub_mutex, 1);

    snprintf(line, sizeof(line),
             "[PUB] Delivered: %d, failed: %d",
             matched, failed);
    log_msg(line);

    return failed ? -1 : matched;
}

static void handle_publish(
    int client,
    unsigned char header,
    unsigned char *packet,
    unsigned int len)
{
    unsigned int topic_len;
    unsigned int offset;
    unsigned int payload_len;
    unsigned int qos = (header >> 1) & 3;
    char topic[MAX_TOPIC + 1];
    char message[MAX_PAYLOAD + 1];
    char line[192];

    (void)client;

    if (qos != 0) {
        log_msg("[MQTT] Incoming PUBLISH must use QoS 0");
        return;
    }

    if (len < 2) {
        log_msg("[MQTT] Invalid PUBLISH");
        return;
    }

    topic_len = ((unsigned int)packet[0] << 8) | packet[1];

    if (topic_len == 0 || topic_len > MAX_TOPIC ||
        topic_len > len - 2) {
        log_msg("[MQTT] Invalid topic length");
        return;
    }

    if (memchr(packet + 2, '\0', topic_len) != NULL) {
        log_msg("[MQTT] Invalid topic");
        return;
    }

    memcpy(topic, packet + 2, topic_len);
    topic[topic_len] = '\0';

    offset = 2 + topic_len;
    payload_len = len - offset;

    if (payload_len > MAX_PAYLOAD) {
        log_msg("[MQTT] Payload too large");
        return;
    }

    safe_text(packet + offset, payload_len,
              message, sizeof(message));

    log_msg("");
    log_msg("=== MQTT MESSAGE ===");

    snprintf(line, sizeof(line), "Topic: %s", topic);
    log_msg(line);

    snprintf(line, sizeof(line), "Message: %s", message);
    log_msg(line);

    log_msg("====================");

    forward_publish(topic, packet + offset, payload_len);
}

static void handle_subscribe(
    int client,
    unsigned char *packet,
    unsigned int len)
{
    unsigned char response[MAX_PACKET];
    unsigned int pos = 2;
    unsigned int count = 0;
    unsigned int response_pos = 0;
    unsigned char id_hi, id_lo;
    char line[192];

    if (len < 5) {
        log_msg("[MQTT] Invalid SUBSCRIBE");
        return;
    }

    id_hi = packet[0];
    id_lo = packet[1];

    response[0] = 0x90;
    response_pos = 2;
    response[response_pos++] = id_hi;
    response[response_pos++] = id_lo;

    while (pos + 3 <= len && count < MAX_SUBS) {
        unsigned int topic_len =
            ((unsigned int)packet[pos] << 8) | packet[pos + 1];
        char topic[MAX_TOPIC + 1];
        unsigned char requested_qos;
        unsigned char result = 0x80;

        pos += 2;

        if (topic_len == 0 || topic_len > MAX_TOPIC ||
            pos + topic_len + 1 > len) {
            log_msg("[MQTT] Invalid subscription topic");
            break;
        }

        if (memchr(packet + pos, '\0', topic_len) != NULL) {
            log_msg("[MQTT] Invalid topic");
            break;
        }

        memcpy(topic, packet + pos, topic_len);
        topic[topic_len] = '\0';
        pos += topic_len;

        requested_qos = packet[pos++];

        if (requested_qos <= 2 &&
            strchr(topic, '+') == NULL &&
            strchr(topic, '#') == NULL &&
            add_subscription(client, topic) == 0) {
            result = 0x00;
        }

        response[response_pos++] = result;
        count++;

        if (result == 0) {
            snprintf(line, sizeof(line),
                     "[SUB] %s", topic);
            log_msg(line);
        } else {
            snprintf(line, sizeof(line),
                     "[SUB] Rejected: %s", topic);
            log_msg(line);
        }
    }

    if (count == 0) {
        log_msg("[MQTT] No valid subscriptions");
        return;
    }

    {
        unsigned char encoded[4];
        unsigned char final_response[MAX_PACKET];
        unsigned int remaining = response_pos - 2;
        int encoded_len = encode_remaining_length(remaining, encoded);
        int total = 0;

        final_response[total++] = response[0];
        memcpy(final_response + total, encoded, encoded_len);
        total += encoded_len;
        memcpy(final_response + total, response + 2, remaining);
        total += (int)remaining;

        if (send_exact(client, final_response, total) < 0)
            log_msg("[MQTT] SUBACK send failed");
        else
            log_msg("[MQTT] SUBACK sent");
    }
}

static void handle_client(int client)
{
    unsigned char packet[MAX_PACKET];

    log_msg("[TCP] Client connected");

    while (1) {
        unsigned char header;
        unsigned int remaining;

        if (recv_exact(client, &header, 1) < 0)
            break;

        if (read_remaining_length(client, &remaining) < 0) {
            log_msg("[MQTT] Invalid packet length");
            break;
        }

        if (remaining > MAX_PACKET) {
            log_msg("[MQTT] Packet too large");
            break;
        }

        if (remaining > 0 &&
            recv_exact(client, packet, (int)remaining) < 0)
            break;

        switch ((header >> 4) & 0x0F) {
        case 1: {
            unsigned char connack[] = { 0x20, 0x02, 0x00, 0x00 };

            if (send_exact(client, connack, sizeof(connack)) < 0)
                goto done;

            log_msg("[MQTT] CONNECT -> CONNACK");
            break;
        }

        case 3:
            handle_publish(client, header, packet, remaining);
            break;

        case 8:
            if ((header & 0x0F) != 2) {
                log_msg("[MQTT] Invalid SUBSCRIBE flags");
                goto done;
            }

            handle_subscribe(client, packet, remaining);
            break;

        case 12: {
            unsigned char pingresp[] = { 0xD0, 0x00 };

            if (send_exact(client, pingresp, sizeof(pingresp)) < 0)
                goto done;

            break;
        }

        case 14:
            log_msg("[MQTT] DISCONNECT");
            goto done;

        default:
            log_msg("[MQTT] Packet not implemented");
            break;
        }
    }

done:
    remove_subscriptions(client);
    sceNetSocketClose(client);
    log_msg("[TCP] Client disconnected");
}

static int client_thread(SceSize args, void *argp)
{
    int client;
    int *client_arg = (int *)argp;

    (void)args;

    client = *client_arg;
    free(client_arg);

    handle_client(client);

    sceKernelExitDeleteThread(0);
    return 0;
}

int main(void)
{
    int ret;
    int server;
    int i;

    psvDebugScreenInit();
    psvDebugScreenPrintf("\e[2J\e[H");

    log_mutex = sceKernelCreateMutex("mqtt_log", 0, 1, NULL);
    sub_mutex = sceKernelCreateMutex("mqtt_subs", 0, 1, NULL);

    for (i = 0; i < MAX_SUBS; i++) {
        subscriptions[i].socket = -1;
        subscriptions[i].topic[0] = '\0';
    }

    log_msg("Vita MQTT Broker");
    log_msg("------------------------------");
    log_msg("Starting network...");

    ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (ret < 0) {
        log_msg("[ERROR] NET module failed");
        sceKernelExitProcess(10);
        return 10;
    }

    SceNetInitParam net_init;
    memset(&net_init, 0, sizeof(net_init));
    net_init.memory = net_pool;
    net_init.size = sizeof(net_pool);
    net_init.flags = 0;

    ret = sceNetInit(&net_init);
    if (ret < 0) {
        log_msg("[ERROR] sceNetInit failed");
        sceKernelExitProcess(12);
        return 12;
    }

    ret = sceNetCtlInit();
    if (ret < 0) {
        log_msg("[ERROR] sceNetCtlInit failed");
        sceNetTerm();
        sceKernelExitProcess(13);
        return 13;
    }

    server = sceNetSocket(
        "mqtt-server", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0
    );

    if (server < 0) {
        log_msg("[ERROR] Socket creation failed");
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(14);
        return 14;
    }

    {
        int opt = 1;
        sceNetSetsockopt(
            server, SCE_NET_SOL_SOCKET,
            SCE_NET_SO_REUSEADDR, &opt, sizeof(opt)
        );
    }

    {
        SceNetSockaddrIn addr;
        memset(&addr, 0, sizeof(addr));

        addr.sin_len = sizeof(addr);
        addr.sin_family = SCE_NET_AF_INET;
        addr.sin_port = sceNetHtons(MQTT_PORT);
        addr.sin_addr.s_addr = SCE_NET_INADDR_ANY;

        ret = sceNetBind(
            server, (SceNetSockaddr *)&addr, sizeof(addr)
        );
    }

    if (ret < 0) {
        log_msg("[ERROR] Bind failed");
        sceNetSocketClose(server);
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(15);
        return 15;
    }

    ret = sceNetListen(server, BACKLOG);
    if (ret < 0) {
        log_msg("[ERROR] Listen failed");
        sceNetSocketClose(server);
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(16);
        return 16;
    }

    log_msg("[NET] Network initialized");
    log_msg("[MQTT] Server running on port 1883");
    log_msg("Exact-topic subscriptions, QoS 0");
    log_msg("Waiting for MQTT clients...");

    while (1) {
        SceNetSockaddrIn client_addr;
        unsigned int client_len = sizeof(client_addr);

        int client = sceNetAccept(
            server, (SceNetSockaddr *)&client_addr, &client_len
        );

        if (client >= 0) {
            int *client_arg = malloc(sizeof(int));

            if (client_arg == NULL) {
                log_msg("[ERROR] Client argument allocation failed");
                sceNetSocketClose(client);
                continue;
            }

            *client_arg = client;

            SceUID tid = sceKernelCreateThread(
                "mqtt-client", client_thread,
                0x10000100, 64 * 1024, 0, 0, NULL
            );

            if (tid < 0) {
                log_msg("[ERROR] Cannot create client thread");
                free(client_arg);
                sceNetSocketClose(client);
            } else if (sceKernelStartThread(
                           tid, sizeof(client_arg), client_arg) < 0) {
                log_msg("[ERROR] Cannot start client thread");
                free(client_arg);
                sceNetSocketClose(client);
                sceKernelDeleteThread(tid);
            }
        } else {
            sceKernelDelayThread(100000);
        }
    }

    return 0;
}