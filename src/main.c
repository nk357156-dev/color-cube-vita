#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#define MQTT_PORT 1883
#define BACKLOG 4

int main(void)
{
    SceNetInitParam net_init;
    static unsigned char net_pool[256 * 1024];

    memset(&net_init, 0, sizeof(net_init));
    net_init.memory = net_pool;
    net_init.size = sizeof(net_pool);
    net_init.flags = 0;

    int ret = sceNetInit(&net_init);
    if (ret < 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    ret = sceNetCtlInit();
    if (ret < 0 && ret != SCE_NET_CTL_ERROR_NOT_INITIALIZED) {
        sceNetTerm();
        sceKernelExitProcess(2);
        return 2;
    }

    int server = sceNetSocket(
        "mqtt-server",
        SCE_NET_AF_INET,
        SCE_NET_SOCK_STREAM,
        0
    );

    if (server < 0) {
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(3);
        return 3;
    }

    int opt = 1;
    sceNetSetsockopt(
        server,
        SCE_NET_SOL_SOCKET,
        SCE_NET_SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

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

    if (ret < 0) {
        sceNetClose(server);
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(4);
        return 4;
    }

    ret = sceNetListen(server, BACKLOG);
    if (ret < 0) {
        sceNetClose(server);
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(5);
        return 5;
    }

    /* Сервер работает до закрытия приложения. */
    while (1) {
        SceNetSockaddrIn client_addr;
        unsigned int client_len = sizeof(client_addr);

        int client = sceNetAccept(
            server,
            (SceNetSockaddr *)&client_addr,
            &client_len
        );

        if (client >= 0) {
            /* Пока только принимаем подключение. */
            sceNetClose(client);
        } else {
            sceKernelDelayThread(100000);
        }
    }

    sceNetClose(server);
    sceNetCtlTerm();
    sceNetTerm();
    sceKernelExitProcess(0);
    return 0;
}