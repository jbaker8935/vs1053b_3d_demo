#define F256LIB_IMPLEMENTATION
#include "f256lib.h"

#include "../include/geometry_kernel.h"
#include "../include/3d_object.h"
#include "../include/video.h"

#define SWARM_COUNT 32
#define SWARM_BOUND 2000
#define SWARM_VEL_MAX 48
// Q15 spring factor: -128/32768 ≈ -1/256 (weak pull, allows wide spread)
#define SWARM_SPRING_Q15 ((int16_t)(-128))

static int16_t s_vel_x[SWARM_COUNT];
static int16_t s_vel_y[SWARM_COUNT];
static int16_t s_vel_z[SWARM_COUNT];
static bool    s_swarm_init = false;

__attribute__((noinline))
void object_swarm_update(SceneObjectParams *objs, uint8_t count, uint16_t frame) {
    (void)frame;

    // Seed velocities once from the hardware RNG.
    if (!s_swarm_init) {
        for (uint8_t i = 0; i < count; i++) {
            s_vel_x[i] = (int16_t)((int16_t)(randomRead() & 31) - 16);
            s_vel_y[i] = (int16_t)((int16_t)(randomRead() & 31) - 16);
            s_vel_z[i] = (int16_t)((int16_t)(randomRead() & 31) - 16);
        }
        s_swarm_init = true;
    }

    for (uint8_t i = 0; i < count; i++) {
        SceneObjectParams *o = &objs[i];

        int16_t rx = (int16_t)((int16_t)(randomRead() & 7) - 4);
        int16_t ry = (int16_t)((int16_t)(randomRead() & 7) - 4);
        int16_t rz = (int16_t)((int16_t)(randomRead() & 7) - 4);

        int16_t ax = (int16_t)(mathSignedMultiply(o->pos_x, SWARM_SPRING_Q15) >> 15);
        int16_t ay = (int16_t)(mathSignedMultiply(o->pos_y, SWARM_SPRING_Q15) >> 15);
        int16_t az = (int16_t)(mathSignedMultiply(o->pos_z, SWARM_SPRING_Q15) >> 15);

        // Accumulate into velocity.
        s_vel_x[i] += rx + ax;
        s_vel_y[i] += ry + ay;
        s_vel_z[i] += rz + az;

        // Clamp velocity.
        if      (s_vel_x[i] >  SWARM_VEL_MAX) s_vel_x[i] =  SWARM_VEL_MAX;
        else if (s_vel_x[i] < -SWARM_VEL_MAX) s_vel_x[i] = -SWARM_VEL_MAX;
        if      (s_vel_y[i] >  SWARM_VEL_MAX) s_vel_y[i] =  SWARM_VEL_MAX;
        else if (s_vel_y[i] < -SWARM_VEL_MAX) s_vel_y[i] = -SWARM_VEL_MAX;
        if      (s_vel_z[i] >  SWARM_VEL_MAX) s_vel_z[i] =  SWARM_VEL_MAX;
        else if (s_vel_z[i] < -SWARM_VEL_MAX) s_vel_z[i] = -SWARM_VEL_MAX;

        // Integrate position.
        o->pos_x += s_vel_x[i];
        o->pos_y += s_vel_y[i];
        o->pos_z += s_vel_z[i];

        // Soft-bounce 
        if      (o->pos_x >  SWARM_BOUND) { o->pos_x =  SWARM_BOUND; s_vel_x[i] = (int16_t)(-s_vel_x[i] / 2); }
        else if (o->pos_x < -SWARM_BOUND) { o->pos_x = -SWARM_BOUND; s_vel_x[i] = (int16_t)(-s_vel_x[i] / 2); }
        if      (o->pos_y >  SWARM_BOUND) { o->pos_y =  SWARM_BOUND; s_vel_y[i] = (int16_t)(-s_vel_y[i] / 2); }
        else if (o->pos_y < -SWARM_BOUND) { o->pos_y = -SWARM_BOUND; s_vel_y[i] = (int16_t)(-s_vel_y[i] / 2); }
        if      (o->pos_z >  SWARM_BOUND) { o->pos_z =  SWARM_BOUND; s_vel_z[i] = (int16_t)(-s_vel_z[i] / 2); }
        else if (o->pos_z < -SWARM_BOUND) { o->pos_z = -SWARM_BOUND; s_vel_z[i] = (int16_t)(-s_vel_z[i] / 2); }

        //  per-object rotation.
        o->yaw   += 1;
        o->pitch += (i & 1) ? 1 : 0;
    }
}

static SceneObjectParams scene_objs[32] = {
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-400, .pos_y=0, .pos_z=400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-400, .pos_y= 0, .pos_z=-400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 400, .pos_y=0, .pos_z=-400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 400, .pos_y= 0, .pos_z=400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-800, .pos_y=-0, .pos_z=800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-800, .pos_y= 0, .pos_z=-800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 800, .pos_y=0, .pos_z=-800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 800, .pos_y= 0, .pos_z=800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-1200, .pos_y=0, .pos_z=1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-1200, .pos_y= 0, .pos_z=-1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 1200, .pos_y=0, .pos_z=-1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 1200, .pos_y= 0, .pos_z=1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-1600, .pos_y=-0, .pos_z=1600 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x=-1600, .pos_y= 0, .pos_z=-1600 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 1600, .pos_y=0, .pos_z=-1600 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=64, .pos_x= 1600, .pos_y= 0, .pos_z=1600 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-400, .pos_y=0, .pos_z=400 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-400, .pos_y= 0, .pos_z=-400 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 400, .pos_y=0, .pos_z=-400 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 400, .pos_y= 0, .pos_z=400 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-800, .pos_y=-0, .pos_z=800 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-800, .pos_y= 0, .pos_z=-800 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 800, .pos_y=0, .pos_z=-800 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 800, .pos_y= 0, .pos_z=800 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-1200, .pos_y=0, .pos_z=1200 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-1200, .pos_y= 0, .pos_z=-1200 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 1200, .pos_y=0, .pos_z=-1200 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 1200, .pos_y= 0, .pos_z=1200 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-1600, .pos_y=-0, .pos_z=1600 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x=-1600, .pos_y= 0, .pos_z=-1600 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 1600, .pos_y=0, .pos_z=-1600 },
    { .slot=0, .yaw=32, .pitch=0, .roll=0, .scale=64, .pos_x= 1600, .pos_y= 0, .pos_z=1600 }        
};

static uint8_t visible_layer = 1;
void app_init(void) {   
    vgk_plugin_init();
    vgk_projection_params(240, 160, 120, -128);
    vgk_model_save(&g_model_cube, 0);
    vgk_cam_params(0, 0, 0, -200, 0, 2000);
    vgk_hidden_line(true);
}

__attribute__((noinline))
void app_frame() {
    uint8_t draw_layer = (visible_layer == 1) ? 2 : 1;
    dmaBitmapClear(draw_layer);
    vgk_reset();
    vgk_trigger();
    uint8_t status = vgk_wait_complete(10000);
    if (status == 1) {
        // retrieve and draw edges for the whole scene (all objects at once)
        // textPrint("Rendering scene with edge retrieval... \n");
        vgk_scrn_edges_render(draw_layer, 0x0A);
    } else if (status == 0){
        textPrint("Error: Geometry kernel timeout.\n");
    } else {
        textPrint("Error: Geometry kernel error during processing.: ");
        textPrintUInt(status);
        textPrint("\n");
    }
    video_wait_vblank();
    bitmapSetVisible(draw_layer, true);
    bitmapSetVisible(visible_layer, false);
    visible_layer = draw_layer;
}


int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    uint16_t version=0;

    video_init();
    app_init();
    textGotoXY(0, 1);
    version = vgk_plugin_version();
    if (version == 0) {
        textPrint("Geometry plugin not detected.\n");
        return 1;
    }
    textPrint("Geometry Plugin Version: ");
    textPrintUInt(version/256);
    textPrint(".");
    textPrintUInt(version%256);
    textPrint("\n");
    textPrint("App Init Complete. \n");
    // use scene API for multi-object demo
    vgk_scene_mode(true);
    vgk_scene_objects(32, scene_objs);
    vgk_scene_occlusion(true);    
    for (uint8_t loop = 0; loop < 4; loop++) {

        for(uint16_t frame=0; frame < 256; frame++) {
            object_swarm_update(scene_objs, 32, frame);
            vgk_scene_objects(32, scene_objs);
            app_frame();
        }
    }
    textPrint("\nDone! Press any key to exit.\n");
    getchar();
    return 0;
}


