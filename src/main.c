#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    if (vita2d_init() == 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    vita2d_set_clear_color(RGBA8(30, 30, 40, 255));

    SceCtrlData pad;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        vita2d_start_drawing();
        vita2d_clear_screen();

        vita2d_draw_rectangle(
            100, 100, 200, 200,
            RGBA8(255, 0, 0, 255)
        );

        vita2d_draw_rectangle(
            380, 100, 200, 200,
            RGBA8(0, 255, 0, 255)
        );

        vita2d_draw_rectangle(
            660, 100, 200, 200,
            RGBA8(0, 100, 255, 255)
        );

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}