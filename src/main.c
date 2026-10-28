#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define W 960.0f
#define H 544.0f
#define PI 3.14159265358979323846f

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float x, y, z;
    unsigned int color;
} Vertex;

static Vec3 cube[8] = {
    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
    {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}
};

static const int faces[6][4] = {
    {0,1,2,3}, {5,4,7,6}, {4,0,3,7},
    {1,5,6,2}, {3,2,6,7}, {4,5,1,0}
};

static const unsigned int colors[6] = {
    RGBA8(255, 65, 65, 255),
    RGBA8(255, 220, 45, 255),
    RGBA8(55, 220, 100, 255),
    RGBA8(50, 145, 255, 255),
    RGBA8(255, 135, 35, 255),
    RGBA8(245, 245, 250, 255)
};

static float angle_x = -0.45f;
static float angle_y = 0.65f;

static Vec3 rotate_vertex(Vec3 v)
{
    float sx = sinf(angle_x), cx = cosf(angle_x);
    float sy = sinf(angle_y), cy = cosf(angle_y);

    float y = v.y * cx - v.z * sx;
    float z = v.y * sx + v.z * cx;

    Vec3 r = {
        v.x * cy + z * sy,
        y,
        -v.x * sy + z * cy
    };

    return r;
}

static Vertex project_vertex(Vec3 v, unsigned int color)
{
    Vec3 r = rotate_vertex(v);
    float depth = 4.2f - r.z;
    float scale = 155.0f / depth;

    Vertex out = {
        W * 0.5f + r.x * scale,
        H * 0.5f - r.y * scale,
        0.5f,
        color
    };

    return out;
}

static void draw_face(int face)
{
    Vertex v[4];
    float depth = 0.0f;

    for (int i = 0; i < 4; i++) {
        Vec3 p = rotate_vertex(cube[faces[face][i]]);
        depth += p.z;
        v[i] = project_vertex(cube[faces[face][i]], colors[face]);
    }

    Vertex triangles[6] = {
        v[0], v[1], v[2],
        v[0], v[2], v[3]
    };

    vita2d_draw_array(
        SCE_GXM_PRIMITIVE_TRIANGLES,
        triangles, 6
    );

    (void)depth;
}

static void draw_cube(void)
{
    float depths[6];
    int order[6] = {0, 1, 2, 3, 4, 5};

    for (int f = 0; f < 6; f++) {
        depths[f] = 0.0f;

        for (int i = 0; i < 4; i++) {
            Vec3 p = rotate_vertex(cube[faces[f][i]]);
            depths[f] += p.z;
        }
    }

    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 6; j++) {
            if (depths[order[i]] > depths[order[j]]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    for (int i = 0; i < 6; i++)
        draw_face(order[i]);

    unsigned int edge = RGBA8(20, 25, 35, 255);

    for (int f = 0; f < 6; f++) {
        for (int i = 0; i < 4; i++) {
            int a = faces[f][i];
            int b = faces[f][(i + 1) % 4];

            Vertex p = project_vertex(cube[a], edge);
            Vertex q = project_vertex(cube[b], edge);

            vita2d_draw_line(
                p.x, p.y, q.x, q.y, edge
            );
        }
    }
}

int main(void)
{
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 22, 35, 255));

    SceCtrlData pad;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        if (pad.buttons & SCE_CTRL_CIRCLE) {
            angle_x = -0.45f;
            angle_y = 0.65f;
        }

        angle_y += 0.008f;
        angle_x += 0.003f;

        if (pad.buttons & SCE_CTRL_LEFT)
            angle_y -= 0.045f;
        if (pad.buttons & SCE_CTRL_RIGHT)
            angle_y += 0.045f;
        if (pad.buttons & SCE_CTRL_UP)
            angle_x -= 0.045f;
        if (pad.buttons & SCE_CTRL_DOWN)
            angle_x += 0.045f;

        int ax = (int)pad.lx - 128;
        int ay = (int)pad.ly - 128;

        if (abs(ax) > 20)
            angle_y += ax * 0.00035f;
        if (abs(ay) > 20)
            angle_x += ay * 0.00035f;

        vita2d_start_drawing();
        vita2d_clear_screen();

        draw_cube();

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}