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
        int n = sceNetRecv(sock, buf + total,
                           (unsigned int)(len - total), 0);

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
        int n = sceNetSend(sock, buf + total,
                           (unsigned int)(len - total), 0);

        if (n <= 0)
            return -1;

        total += n;
    }

    return total;
}

static void handle_client(int client)
{
    unsigned char header;
    unsigned char encoded;
    unsigned int remaining = 0;
    unsigned int multiplier = 1;
    int count = 0;

    log_msg("[TCP] Client connected");

    while (1) {
        if (recv_exact(client, &header, 1) < 0) {
            log_msg("[TCP] Client disconnected");
            break;
        }

        if (recv_exact(client, &encoded, 1) < 0) {
            log_msg("[MQTT] Invalid packet length");
            break;
        }

        remaining = 0;
        multiplier = 1;
        count = 0;

        do {
            remaining += (unsigned int)(encoded & 127) * multiplier;
            count++;

            if (encoded & 128) {
                if (count >= 4 ||
                    recv_exact(client, &encoded, 1) < 0) {
                    log_msg("[MQTT] Invalid length encoding");
                    return;
                }

                multiplier *= 128;
            } else {
                break;
            }
        } while (1);

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
            char line[80];
            snprintf(line, sizeof(line),
                     "[MQTT] Packet type: 0x%X",
                     (header >> 4) & 0x0F);
            log_msg(line);
        }

        if ((header >> 4) == 1) {
            unsigned char connack[] = { 0x20, 0x02, 0x00, 0x00 };

            if (send_exact(client, connack, sizeof(connack)) < 0) {
                log_msg("[MQTT] CONNACK send failed");
                break;
            }

            log_msg("[MQTT] CONNECT received");
            log_msg("[MQTT] CONNACK sent");
        } else if ((header >> 4) == 14) {
            log_msg("[MQTT] DISCONNECT received");
            break;
        } else {
            log_msg("[MQTT] Packet not implemented yet");
        }
    }

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
    log_msg("[MQTT] TCP server is running");
    log_msg("Port: 1883");
    log_msg("------------------------------");
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