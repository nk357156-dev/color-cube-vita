#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

#define SCREEN_W 960
#define SCREEN_H 544

#define CUBE_W 200
#define CUBE_H 200
#define DEPTH 35
#define EDGE 5

#define STICK_DEADZONE 25

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    if (vita2d_init() < 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    vita2d_set_clear_color(RGBA8(18, 22, 34, 255));

    SceCtrlData pad;
    int x = 380;
    int y = 172;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        /* Левый аналоговый стик: диапазон примерно 0–255 */
        int dx = (int)pad.lx - 128;
        int dy = (int)pad.ly - 128;

        if (dx < -STICK_DEADZONE)
            x -= ( (-dx - STICK_DEADZONE) / 20 ) + 1;
        else if (dx > STICK_DEADZONE)
            x += ( ( dx - STICK_DEADZONE) / 20 ) + 1;

        if (dy < -STICK_DEADZONE)
            y -= ( (-dy - STICK_DEADZONE) / 20 ) + 1;
        else if (dy > STICK_DEADZONE)
            y += ( ( dy - STICK_DEADZONE) / 20 ) + 1;

        if (x < 10) x = 10;
        if (x > SCREEN_W - CUBE_W - DEPTH - 10)
            x = SCREEN_W - CUBE_W - DEPTH - 10;

        if (y < DEPTH + 10) y = DEPTH + 10;
        if (y > SCREEN_H - CUBE_H - 10)
            y = SCREEN_H - CUBE_H - 10;

        vita2d_start_drawing();
        vita2d_clear_screen();

        /* Верхняя грань */
        vita2d_draw_rectangle(
            x + DEPTH, y - DEPTH, CUBE_W, DEPTH,
            RGBA8(110, 205, 255, 255)
        );

        /* Правая грань */
        vita2d_draw_rectangle(
            x + CUBE_W, y, DEPTH, CUBE_H,
            RGBA8(28, 72, 155, 255)
        );

        /* Передняя грань */
        vita2d_draw_rectangle(
            x, y, CUBE_W, CUBE_H,
            RGBA8(45, 145, 245, 255)
        );

        /* Рёбра передней грани */
        vita2d_draw_rectangle(x, y, CUBE_W, EDGE,
            RGBA8(190, 230, 255, 255));
        vita2d_draw_rectangle(x, y + CUBE_H - EDGE,
            CUBE_W, EDGE, RGBA8(15, 60, 120, 255));
        vita2d_draw_rectangle(x, y, EDGE, CUBE_H,
            RGBA8(190, 230, 255, 255));
        vita2d_draw_rectangle(x + CUBE_W - EDGE, y,
            EDGE, CUBE_H, RGBA8(15, 60, 120, 255));

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}