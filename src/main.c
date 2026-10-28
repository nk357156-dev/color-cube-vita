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

static int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    if (vita2d_init() < 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    vita2d_set_clear_color(RGBA8(18, 22, 34, 255));

    SceCtrlData pad;

    /* Угол условного поворота: от -100 до 100 */
    int angle_x = 0;
    int angle_y = 0;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        int sx = (int)pad.lx - 128;
        int sy = (int)pad.ly - 128;

        if (sx < -STICK_DEADZONE)
            angle_y -= ((-sx - STICK_DEADZONE) / 20) + 1;
        else if (sx > STICK_DEADZONE)
            angle_y += ((sx - STICK_DEADZONE) / 20) + 1;

        if (sy < -STICK_DEADZONE)
            angle_x -= ((-sy - STICK_DEADZONE) / 20) + 1;
        else if (sy > STICK_DEADZONE)
            angle_x += ((sy - STICK_DEADZONE) / 20) + 1;

        angle_x = clamp(angle_x, -100, 100);
        angle_y = clamp(angle_y, -100, 100);

        /*
         * Это визуальная имитация наклона:
         * прямоугольники остаются плоскими,
         * но верхняя и боковая грани меняют размер.
         */
        int depth_x = DEPTH + angle_y / 4;
        int depth_y = DEPTH + angle_x / 4;

        depth_x = clamp(depth_x, 12, 58);
        depth_y = clamp(depth_y, 12, 58);

        int x = (SCREEN_W - CUBE_W - depth_x) / 2;
        int y = (SCREEN_H - CUBE_H + depth_y) / 2;

        vita2d_start_drawing();
        vita2d_clear_screen();

        /* Верхняя грань */
        vita2d_draw_rectangle(
            x + depth_x, y - depth_y,
            CUBE_W, depth_y,
            RGBA8(110, 205, 255, 255)
        );

        /* Правая грань */
        vita2d_draw_rectangle(
            x + CUBE_W, y,
            depth_x, CUBE_H,
            RGBA8(28, 72, 155, 255)
        );

        /* Передняя грань */
        vita2d_draw_rectangle(
            x, y, CUBE_W, CUBE_H,
            RGBA8(45, 145, 245, 255)
        );

        /* Рёбра передней грани */
        vita2d_draw_rectangle(
            x, y, CUBE_W, EDGE,
            RGBA8(190, 230, 255, 255)
        );
        vita2d_draw_rectangle(
            x, y + CUBE_H - EDGE, CUBE_W, EDGE,
            RGBA8(15, 60, 120, 255)
        );
        vita2d_draw_rectangle(
            x, y, EDGE, CUBE_H,
            RGBA8(190, 230, 255, 255)
        );
        vita2d_draw_rectangle(
            x + CUBE_W - EDGE, y, EDGE, CUBE_H,
            RGBA8(15, 60, 120, 255)
        );

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}