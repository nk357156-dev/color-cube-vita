#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#define MQTT_PORT 1883
#define BACKLOG 4

int main(void)
{
    int ret;
    int server = -1;

    static unsigned char net_pool[256 * 1024];
    SceNetInitParam net_init;

    /* Load system network modules */
    ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (ret < 0) {
        sceKernelExitProcess(10);
        return 10;
    }

    ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET_CTL);
    if (ret < 0) {
        sceKernelExitProcess(11);
        return 11;
    }

    /* Initialize network stack */
    memset(&net_init, 0, sizeof(net_init));
    net_init.memory = net_pool;
    net_init.size = sizeof(net_pool);
    net_init.flags = 0;

    ret = sceNetInit(&net_init);
    if (ret < 0) {
        sceKernelExitProcess(12);
        return 12;
    }

    ret = sceNetCtlInit();
    if (ret < 0) {
        sceNetTerm();
        sceKernelExitProcess(13);
        return 13;
    }

    /* Create TCP server socket */
    server = sceNetSocket(
        "mqtt-server",
        SCE_NET_AF_INET,
        SCE_NET_SOCK_STREAM,
        0
    );

    if (server < 0) {
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(14);
        return 14;
    }

    /* Allow address reuse */
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

    /* Bind to all network interfaces on port 1883 */
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
        sceNetSocketClose(server);
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(15);
        return 15;
    }

    /* Start listening */
    ret = sceNetListen(server, BACKLOG);
    if (ret < 0) {
        sceNetSocketClose(server);
        sceNetCtlTerm();
        sceNetTerm();
        sceKernelExitProcess(16);
        return 16;
    }

    /* Accept TCP connections */
    while (1) {
        SceNetSockaddrIn client_addr;
        unsigned int client_len = sizeof(client_addr);

        int client = sceNetAccept(
            server,
            (SceNetSockaddr *)&client_addr,
            &client_len
        );

        if (client >= 0) {
            /* TCP test only: close the client connection */
            sceNetSocketClose(client);
        } else {
            sceKernelDelayThread(100000);
        }
    }

    /* Unreachable during normal operation */
    sceNetSocketClose(server);
    sceNetCtlTerm();
    sceNetTerm();

    sceKernelExitProcess(0);
    return 0;
}