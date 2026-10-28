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
    int x = 380;
    int y = 172;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        if (pad.buttons & SCE_CTRL_LEFT)  x -= 3;
        if (pad.buttons & SCE_CTRL_RIGHT) x += 3;
        if (pad.buttons & SCE_CTRL_UP)    y -= 3;
        if (pad.buttons & SCE_CTRL_DOWN)  y += 3;

        if (x < 0) x = 0;
        if (x > 760) x = 760;
        if (y < 0) y = 0;
        if (y > 344) y = 344;

        vita2d_start_drawing();
        vita2d_clear_screen();

        /* Передняя грань */
        vita2d_draw_rectangle(
            x, y, 200, 200,
            RGBA8(50, 145, 255, 255)
        );

        /* Верхняя грань */
        vita2d_draw_rectangle(
            x + 35, y - 35, 200, 35,
            RGBA8(100, 190, 255, 255)
        );

        /* Правая грань */
        vita2d_draw_rectangle(
            x + 200, y, 35, 200,
            RGBA8(25, 75, 170, 255)
        );

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}