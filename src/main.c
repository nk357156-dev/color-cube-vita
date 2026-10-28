#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include "debugScreen.h"

#define MQTT_PORT 1883
#define BACKLOG 4
#define MAX_PACKET 2048
#define MAX_TEXT 512

static unsigned char net_pool[256 * 1024];
static unsigned char packet[MAX_PACKET];

static void log_msg(const char *msg)
{
    psvDebugScreenPrintf("%s\n", msg);
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
    unsigned int multiplier = 1;
    unsigned int result = 0;
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

static void safe_text(
    const unsigned char *src,
    unsigned int len,
    char *dst,
    unsigned int dst_size
)
{
    unsigned int i;
    unsigned int out = 0;

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

static void handle_publish(int client, unsigned char header,
                           unsigned int len)
{
    unsigned int topic_len;
    unsigned int offset;
    unsigned int payload_len;
    unsigned int qos;
    char topic[MAX_TEXT];
    char message[MAX_TEXT];
    char line[MAX_TEXT + 32];

    if (len < 2) {
        log_msg("[MQTT] Invalid PUBLISH packet");
        return;
    }

    topic_len = ((unsigned int)packet[0] << 8) | packet[1];

    if (topic_len == 0 || topic_len > len - 2) {
        log_msg("[MQTT] Invalid topic");
        return;
    }

    offset = 2 + topic_len;
    qos = (header >> 1) & 3;

    if (qos == 3) {
        log_msg("[MQTT] Invalid PUBLISH QoS");
        return;
    }

    if (qos == 1 || qos == 2) {
        if (offset + 2 > len) {
            log_msg("[MQTT] Missing packet identifier");
            return;
        }
    }

    safe_text(packet + 2, topic_len, topic, sizeof(topic));

    if (qos > 0) {
        unsigned int packet_id = offset;

        offset += 2;

        if (qos == 1) {
            unsigned char ack[] = {
                0x40, 0x02,
                packet[packet_id],
                packet[packet_id + 1]
            };

            if (send_exact(client, ack, sizeof(ack)) < 0)
                log_msg("[MQTT] PUBACK send failed");
        } else {
            log_msg("[MQTT] QoS 2 is not supported yet");
            return;
        }
    }

    payload_len = len - offset;

    safe_text(
        packet + offset,
        payload_len,
        message,
        sizeof(message)
    );

    log_msg("");
    log_msg("=== MQTT MESSAGE ===");

    snprintf(line, sizeof(line), "Topic: %s", topic);
    log_msg(line);

    snprintf(line, sizeof(line), "Message: %s", message);
    log_msg(line);

    log_msg("====================");
}

static void handle_client(int client)
{
    log_msg("[TCP] Client connected");

    while (1) {
        unsigned char header;
        unsigned int remaining;

        if (recv_exact(client, &header, 1) < 0) {
            log_msg("[TCP] Client disconnected");
            break;
        }

        if (read_remaining_length(client, &remaining) < 0) {
            log_msg("[MQTT] Invalid packet length");
            break;
        }

        if (remaining > MAX_PACKET) {
            log_msg("[MQTT] Packet too large");
            break;
        }

        if (remaining > 0 &&
            recv_exact(client, packet, (int)remaining) < 0) {
            log_msg("[MQTT] Incomplete packet");
            break;
        }

        {
            char line[64];

            snprintf(line, sizeof(line),
                     "[MQTT] Packet type: 0x%X",
                     (header >> 4) & 0x0F);
            log_msg(line);
        }

        switch ((header >> 4) & 0x0F) {
        case 1: {
            unsigned char connack[] = { 0x20, 0x02, 0x00, 0x00 };

            if (send_exact(client, connack, sizeof(connack)) < 0) {
                log_msg("[MQTT] CONNACK send failed");
                goto done;
            }

            log_msg("[MQTT] CONNECT received");
            log_msg("[MQTT] CONNACK sent");
            break;
        }

        case 3:
            handle_publish(client, header, remaining);
            break;

        case 12: {
            unsigned char pingresp[] = { 0xD0, 0x00 };

            if (send_exact(client, pingresp, sizeof(pingresp)) < 0) {
                log_msg("[MQTT] PINGRESP send failed");
                goto done;
            }

            log_msg("[MQTT] PINGREQ -> PINGRESP");
            break;
        }

        case 14:
            log_msg("[MQTT] DISCONNECT received");
            goto done;

        default:
            log_msg("[MQTT] Packet not implemented yet");
            break;
        }
    }

done:
    sceNetSocketClose(client);
}

int main(void)
{
    int ret;
    int server;

    psvDebugScreenInit();
    psvDebugScreenPrintf("\e[2J\e[H");

    log_msg("Vita MQTT Broker");
    log_msg("------------------------------");
    log_msg("Starting network...");

    ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (ret < 0) {
        log_msg("[ERROR] NET module failed");
        sceKernelDelayThread(5000000);
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
        log_msg("[ERROR] Network control failed");
        sceNetTerm();
        sceKernelExitProcess(13);
        return 13;
    }

    server = sceNetSocket(
        "mqtt-server",
        SCE_NET_AF_INET,
        SCE_NET_SOCK_STREAM,
        0
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
            server,
            SCE_NET_SOL_SOCKET,
            SCE_NET_SO_REUSEADDR,
            &opt,
            sizeof(opt)
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
            server,
            (SceNetSockaddr *)&addr,
            sizeof(addr)
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
    log_msg("Waiting for MQTT client...");

    while (1) {
        SceNetSockaddrIn client_addr;
        unsigned int client_len = sizeof(client_addr);

        int client = sceNetAccept(
            server,
            (SceNetSockaddr *)&client_addr,
            &client_len
        );

        if (client >= 0) {
            handle_client(client);
            log_msg("Waiting for MQTT client...");
        } else {
            sceKernelDelayThread(100000);
        }
    }

    return 0;
}