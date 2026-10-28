#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    if (vita2d_init() < 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    vita2d_set_clear_color(RGBA8(30, 30, 40, 255));

    vita2d_color_vertex triangle[3] = {
        { 300.0f, 300.0f, 0.5f, RGBA8(255, 60, 60, 255) },
        { 480.0f, 300.0f, 0.5f, RGBA8(60, 255, 60, 255) },
        { 390.0f, 140.0f, 0.5f, RGBA8(60, 120, 255, 255) }
    };

    SceCtrlData pad;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        vita2d_start_drawing();
        vita2d_clear_screen();

        vita2d_draw_array(
            SCE_GXM_PRIMITIVE_TRIANGLES,
            triangle,
            3
        );

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}