#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define SW 960.0f
#define SH 544.0f

typedef struct {
    float x, y, z;
} Vec3;

static const Vec3 cube[8] = {
    {-1,-1,-1}, { 1,-1,-1},
    { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1},
    { 1, 1, 1}, {-1, 1, 1}
};

static const int faces[6][4] = {
    {0,1,2,3}, {5,4,7,6},
    {4,0,3,7}, {1,5,6,2},
    {3,2,6,7}, {4,5,1,0}
};

static const unsigned int colors[6] = {
    RGBA8(255,65,65,255),
    RGBA8(255,220,45,255),
    RGBA8(55,220,100,255),
    RGBA8(50,145,255,255),
    RGBA8(255,135,35,255),
    RGBA8(245,245,250,255)
};

static float ax = -0.45f;
static float ay = 0.65f;

static Vec3 rotate_vertex(Vec3 v)
{
    float sx = sinf(ax), cx = cosf(ax);
    float sy = sinf(ay), cy = cosf(ay);

    float y = v.y * cx - v.z * sx;
    float z = v.y * sx + v.z * cx;

    Vec3 r = {
        v.x * cy + z * sy,
        y,
        -v.x * sy + z * cy
    };

    return r;
}

static void project(Vec3 v, float *x, float *y)
{
    Vec3 r = rotate_vertex(v);
    float depth = 4.0f - r.z;
    float scale = 150.0f / depth;

    *x = SW * 0.5f + r.x * scale;
    *y = SH * 0.5f - r.y * scale;
}

static void draw_face(int f)
{
    float x[4], y[4];

    for (int i = 0; i < 4; i++) {
        project(cube[faces[f][i]], &x[i], &y[i]);
    }

    vita2d_draw_triangle(
        x[0], y[0], x[1], y[1], x[2], y[2], colors[f]
    );

    vita2d_draw_triangle(
        x[0], y[0], x[2], y[2], x[3], y[3], colors[f]
    );
}

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    if (vita2d_init() == 0) {
        sceKernelExitProcess(1);
        return 1;
    }

    vita2d_set_clear_color(RGBA8(30,30,40,255));

    SceCtrlData pad;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        ay += 0.012f;

        if (pad.buttons & SCE_CTRL_LEFT)  ay -= 0.04f;
        if (pad.buttons & SCE_CTRL_RIGHT) ay += 0.04f;
        if (pad.buttons & SCE_CTRL_UP)    ax -= 0.04f;
        if (pad.buttons & SCE_CTRL_DOWN)  ax += 0.04f;

        vita2d_start_drawing();
        vita2d_clear_screen();

        for (int f = 0; f < 6; f++) {
            draw_face(f);
        }

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}