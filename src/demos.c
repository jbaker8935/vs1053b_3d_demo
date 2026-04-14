#include "f256lib.h"
#include <stdio.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/demo.h"
#include "../include/demos.h"
#include "../include/3d_object.h"
#include "../include/geometry_kernel.h"
#include "../include/game_state.h"
#include "../include/timer.h"
#include "../include/vgm.h"
extern void start_vgm_playback(void);  /* defined in main.c */
extern void stop_vgm_playback(void);   /* defined in main.c */
extern void start_vgm_fx_himem(uint32_t himem_addr, uint32_t len); /* defined in main.c */
#include "../include/vgm_assets.h"

extern int32_t mathSignedMultiply(int16_t a, int16_t b);

// =============================================================================
// Demo 1 — Simple Cube
// Wireframe → hidden-line → near/far coloring → rotation → scaling
// =============================================================================

static const SceneObjectParams demo1_init[] = {
    { .slot=0, .yaw=16, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=-100, .pos_z= 300 }
};

static const Model3D * const demo1_models[] = { &g_model_cube };

static void demo1_wireframe(void) {
    vgk_hidden_line_disable();
    vgk_near_far_coloring_enable(false);
}

static void demo1_hidden_line(void) {
    vgk_model_hidden_line_init(&g_model_cube, 0);
    vgk_hidden_line_enable();
}

static void demo1_nearfar(void) {
    vgk_near_far_coloring_enable(true);
}

static void demo1_rotate(void) {
    g_demo_instances[0].yaw   = (uint8_t)(g_demo_instances[0].yaw   + 3u);
    g_demo_instances[0].pitch = (uint8_t)(g_demo_instances[0].pitch + 2u);
}

static void demo1_scale(void) {
    uint8_t s = g_demo_instances[0].scale;
    if (g_instance_scale_dir[0] == 0) {
        g_instance_scale_dir[0] = 2;
    }
    if (g_instance_scale_dir[0] > 0) {
        s = (s < (220u - 3u)) ? (uint8_t)(s + 3u) : 220u;
        if (s >= 220u) { g_instance_scale_dir[0] = -2; }
    } else {
        s = (s > (64u + 3u)) ? (uint8_t)(s - 3u) : 64u;
        if (s <= 64u) { g_instance_scale_dir[0] = 2; }
    }
    g_demo_instances[0].scale = s;
}

static void demo1_enter(void) {
    reset_camera();
    vgk_hidden_line_disable();
    vgk_near_far_coloring_enable(false);
    vgk_model_slot_init(&g_model_cube, 0);    
}

static void demo1_exit(void) {
    vgk_hidden_line_disable();
    vgk_near_far_coloring_enable(true);
}

static const DemoEvent demo1_events[] = {
    { "Wireframe",           3, DEMO_EVENT_ONESHOT,  demo1_wireframe   },
    { "Hidden line removal", 3, DEMO_EVENT_ONESHOT,  demo1_hidden_line },
    { "Near/far coloring",   3, DEMO_EVENT_ONESHOT,  demo1_nearfar     },
    { "Rotation",            5, DEMO_EVENT_PERFRAME, demo1_rotate      },
    { "Scaling",             5, DEMO_EVENT_PERFRAME, demo1_scale       },
};

static const Demo demo1 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTGC QE RF Exit: X", "Simple Cube Primitive" },
    .event_count     = 5,
    .events          = demo1_events,
    .instance_count  = 1,
    .initial_instances = demo1_init,
    .initial_models  = demo1_models,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = false,
    .on_enter        = demo1_enter,
    .on_exit         = demo1_exit,
};

// =============================================================================
// Demo 2 — Multiple Cubes with distinct colors
// Loads cube geometry into slots 2-4 with different object_color values
// =============================================================================

static Model3D g_cube2, g_cube3, g_cube4;

static void demo2_enter(void) {
    reset_camera();
    vgk_hidden_line_disable();
    vgk_near_far_coloring_enable(true);
    g_cube2 = g_model_cube; g_cube2.object_color = 0x0505;
    g_cube3 = g_model_cube; g_cube3.object_color = 0x0707;
    g_cube4 = g_model_cube; g_cube4.object_color = 0x0909;
    vgk_model_slot_init(&g_model_cube, 0);
    vgk_model_slot_init(&g_cube2, 2);
    vgk_model_slot_init(&g_cube3, 3);
    vgk_model_slot_init(&g_cube4, 4);
    g_demo_models[0] = &g_model_cube;
    g_demo_models[1] = &g_cube2;
    g_demo_models[2] = &g_cube3;
    g_demo_models[3] = &g_cube4;
}

static const SceneObjectParams demo2_init[] = {
    { .slot=0, .yaw=0,   .pitch=0, .roll=0, .scale=128, .pos_x=-250, .pos_y=0, .pos_z=200 },
    { .slot=2, .yaw=64,  .pitch=0, .roll=0, .scale=128, .pos_x= 250, .pos_y=0, .pos_z=200 },
    { .slot=3, .yaw=128, .pitch=0, .roll=0, .scale=128, .pos_x=-250, .pos_y=0, .pos_z=600 },
    { .slot=4, .yaw=192, .pitch=0, .roll=0, .scale=128, .pos_x= 250, .pos_y=0, .pos_z=600 },
};

static void demo2_arrange(void) {
    // initial static display — no action required (positions set by start_demo)
}

static void demo2_rotate(void) {
    for (uint8_t i = 0; i < g_demo_instance_count; ++i) {
        g_demo_instances[i].yaw   = (uint8_t)(g_demo_instances[i].yaw   + 3u);
        g_demo_instances[i].pitch = (uint8_t)(g_demo_instances[i].pitch + 2u);
    }
}

static const DemoEvent demo2_events[] = {
    { "Four cubes - static",   5, DEMO_EVENT_ONESHOT,  demo2_arrange },
    { "Four cubes - rotating", 7, DEMO_EVENT_PERFRAME, demo2_rotate  },
};

static const Demo demo2 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTGC QE RF Exit: X", "Multiple Objects" },
    .event_count     = 2,
    .events          = demo2_events,
    .instance_count  = 4,
    .initial_instances = demo2_init,
    .initial_models  = NULL,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = false,
    .on_enter        = demo2_enter,
    .on_exit         = NULL,
};

// =============================================================================
// Demo 3 — Anaconda Flyby
// Anaconda starts far away and flies toward/past the camera
// =============================================================================

static const SceneObjectParams demo3_init[] = {
    { .slot=1, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=100, .pos_z=-2000 },
    { .slot=4, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=100, .pos_z=-2000 },            
    { .slot=5, .yaw=228, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=200, .pos_z=2400 },
    { .slot=5, .yaw=9, .pitch=9, .roll=9, .scale=192, .pos_x=0, .pos_y=200, .pos_z=2400 },
};

static const Model3D * const demo3_models[] = { &g_model_anaconda, &g_model_projectile,
     &g_model_starfield, &g_model_starfield };

static void demo3_enter(void) {
    reset_camera();
    vgk_model_slot_init(&g_model_anaconda, 1);
    vgk_model_slot_init(&g_model_starfield, 5);
    vgk_model_slot_init(&g_model_projectile, 4);
    // graphics background
	POKE(0xD00D, 0x00);
    POKE(0xD00E, 0x00);
    POKE(0xD00F, 0x00);    
    bitmapSetVisible(0, true);
}

static void demo3_setup(void) {
    vgk_model_hidden_line_init(&g_model_anaconda, 1);
    vgk_hidden_line_enable();
    vgk_near_far_coloring_enable(true);
}

static void demo3_setup_anaconda_2(void) {
    // overwrite projectile slot with second anaconda pose for "Oh No" event
    g_demo_instances[1].slot = 1;
    g_demo_models[1] = &g_model_anaconda;
    // shift x positions
    g_demo_instances[0].pos_x = (int16_t)(g_demo_instances[0].pos_x - 100);
    g_demo_instances[1].pos_x = (int16_t)(g_demo_instances[1].pos_x + 100);
    // reset Y and Z positions
    g_demo_instances[0].pos_y = 100;
    g_demo_instances[0].pos_z = -2000;
    g_demo_instances[1].pos_y = 100;
    g_demo_instances[1].pos_z = -2000;
}

static void demo3_fly(void) {
    // Increment Z to move anaconda toward camera 
    g_demo_instances[0].pos_z = (int16_t)(g_demo_instances[0].pos_z + 24);
    g_demo_instances[0].pos_x = (int16_t)(g_demo_instances[0].pos_x - 2);

    // Increment Z to move projectile toward camera
    g_demo_instances[1].pos_z = (int16_t)(g_demo_instances[1].pos_z + 48);
    g_demo_instances[1].pos_y = (int16_t)(g_demo_instances[1].pos_y + 2);
    g_demo_instances[1].roll  = (uint8_t)(g_demo_instances[1].roll + 4);

    // move the starfield with the camera.
    GameContext *ctx = game_state_data();
    g_demo_instances[2].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[2].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[2].pos_z = ctx->wireframe.camera.position.z;
    g_demo_instances[3].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[3].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[3].pos_z = ctx->wireframe.camera.position.z;
}

static void demo3_ohno_fly(void) {
    // Increment Z to move anaconda toward camera 
    g_demo_instances[0].pos_z = (int16_t)(g_demo_instances[0].pos_z + 24);
    g_demo_instances[0].pos_x = (int16_t)(g_demo_instances[0].pos_x - 2);

    // Increment Z to move second anaconda toward camera
    g_demo_instances[1].pos_z = (int16_t)(g_demo_instances[1].pos_z + 48);
    g_demo_instances[1].pos_y = (int16_t)(g_demo_instances[1].pos_y + 2);
    g_demo_instances[1].roll  = (uint8_t)(g_demo_instances[1].roll + 4);

    // move the starfield with the camera.
    GameContext *ctx = game_state_data();
    g_demo_instances[2].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[2].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[2].pos_z = ctx->wireframe.camera.position.z;
    g_demo_instances[3].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[3].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[3].pos_z = ctx->wireframe.camera.position.z;
}

static void demo3_exit(void) {
    // restore background color
    POKE(0xD00D, 0x33);
    POKE(0xD00E, 0x33);
    POKE(0xD00F, 0x33);
    vgk_hidden_line_disable();
    bitmapSetVisible(0, false);
    vgk_near_far_coloring_enable(true);
}

static const DemoEvent demo3_events[] = {
    { "Anaconda incoming...",  2,  DEMO_EVENT_ONESHOT,  demo3_setup },
    { "Flyby",                 8, DEMO_EVENT_PERFRAME, demo3_fly   },
    { "Oh, No",                1, DEMO_EVENT_ONESHOT, demo3_setup_anaconda_2   }, 
    { "Double Anaconda Flyby", 8, DEMO_EVENT_PERFRAME, demo3_ohno_fly   },   
};

static const Demo demo3 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTGC QE RF Exit: X", "Anaconda Flyby" },
    .event_count     = 4,
    .events          = demo3_events,
    .instance_count  = 4,
    .initial_instances = demo3_init,
    .initial_models  = demo3_models,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = false,
    .on_enter        = demo3_enter,
    .on_exit         = demo3_exit,
};

// =============================================================================
// Demo 4 — Anaconda Color Cycling
// Object color cycles through palette entries each animation frame
// =============================================================================

static const uint16_t demo4_colors[] = {
    0x0F0B, 0x0A0F, 0x0F0C, 0x0C0F, 0x050A, 0x0A05, 0x0F02, 0x020F
};
static uint8_t  g_d4_color_idx;
static Model3D  g_d4_anaconda;

static const SceneObjectParams demo4_init[] = {
    { .slot=1, .yaw=20, .pitch=10, .roll=0, .scale=160, .pos_x=-100, .pos_y=200, .pos_z=400 },
    { .slot=5, .yaw=228, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=200, .pos_z=2400 },
    { .slot=5, .yaw=9, .pitch=9, .roll=9, .scale=192, .pos_x=0, .pos_y=200, .pos_z=2400 },    
};

static void demo4_enter(void) {
    reset_camera();    
    // graphics background
	POKE(0xD00D, 0x00);
    POKE(0xD00E, 0x00);
    POKE(0xD00F, 0x00);    
    bitmapSetVisible(0, true);
    vgk_hidden_line_enable();
    vgk_near_far_coloring_enable(true);
    g_d4_anaconda = g_model_anaconda;
    g_d4_color_idx = 0;
    vgk_model_slot_init(&g_d4_anaconda, 1);
    vgk_model_slot_init(&g_model_starfield, 5);
    g_demo_models[0] = &g_d4_anaconda;
    g_demo_models[1] = &g_model_starfield;
    g_demo_models[2] = &g_model_starfield;
}

static void demo4_static(void) {
    // initial pose — no anim
}

static void demo4_cycle(void) {
    g_d4_color_idx = (uint8_t)((g_d4_color_idx + 1u) % 8u);
    g_d4_anaconda.object_color = demo4_colors[g_d4_color_idx];
    g_demo_instances[0].yaw = (uint8_t)(g_demo_instances[0].yaw + 2u);
    GameContext *ctx = game_state_data();
    g_demo_instances[1].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[1].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[1].pos_z = ctx->wireframe.camera.position.z;
    g_demo_instances[2].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[2].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[2].pos_z = ctx->wireframe.camera.position.z;       
}

static void demo4_exit(void) {
    bitmapSetVisible(0, false);    
    vgk_hidden_line_disable();    
    vgk_model_slot_init(&g_model_anaconda, 1);
    vgk_near_far_coloring_enable(true);
    // restore background color
    POKE(0xD00D, 0x33);
    POKE(0xD00E, 0x33);
    POKE(0xD00F, 0x33);
}

static const DemoEvent demo4_events[] = {
    { "Anaconda - default colors",  4, DEMO_EVENT_ONESHOT,  demo4_static },
    { "Object color cycling",       8, DEMO_EVENT_PERFRAME, demo4_cycle  },
};

static const Demo demo4 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTGC QE RF Exit: X", "Anaconda Color Cycling" },
    .event_count     = 2,
    .events          = demo4_events,
    .instance_count  = 3,
    .initial_instances = demo4_init,
    .initial_models  = NULL,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = false,
    .on_enter        = demo4_enter,
    .on_exit         = demo4_exit,
};

// =============================================================================
// Demo 5 — Four Cube Scene with AABB Overlay
// Camera orbits; AABB boxes drawn in second event
// =============================================================================

static const SceneObjectParams demo5_init[] = {
    { .slot=0, .yaw=0,   .pitch=0, .roll=0, .scale=128, .pos_x=-300, .pos_y=0, .pos_z=400 },
    { .slot=0, .yaw=64,  .pitch=0, .roll=0, .scale=128, .pos_x= 300, .pos_y=0, .pos_z=400 },
    { .slot=0, .yaw=128, .pitch=0, .roll=0, .scale=128, .pos_x=-300, .pos_y=0, .pos_z=800 },
    { .slot=0, .yaw=192, .pitch=0, .roll=0, .scale=128, .pos_x= 300, .pos_y=0, .pos_z=800 },
};

static void demo5_enter(void) {
    GameContext *ctx = game_state_data();
    ctx->wireframe.camera.position.x = 0;
    ctx->wireframe.camera.position.y = 200;
    ctx->wireframe.camera.position.z = 2400;
    ctx->wireframe.camera.yaw   = 0;
    ctx->wireframe.camera.pitch = 0;
    ctx->wireframe.camera.roll  = 0;
    ctx->wireframe.camera.moved = true;
    vgk_hidden_line_enable();
    vgk_near_far_coloring_enable(true);
    vgk_model_slot_init(&g_model_cube, 0);    
}

static uint8_t demo5_orbit_angle;

static void demo5_orbit(void) {
    // Circle-strafe around the scene center (0,0). Keep camera aimed at center.
    const int16_t radius = 1600;
    const int16_t center_x = 0;
    const int16_t center_z = 600;

    demo5_orbit_angle = (uint8_t)(demo5_orbit_angle + 2u);
    int16_t sinv = sin_table[demo5_orbit_angle];
    int16_t cosv = sin_table[(uint8_t)(demo5_orbit_angle + 64u)];

    // Q14 multiply: (sinv * radius) >> 14
    int16_t dx = (int16_t)((mathSignedMultiply(cosv,radius)) >> 14);
    int16_t dz = (int16_t)((mathSignedMultiply(sinv,radius)) >> 14);

    GameContext *ctx = game_state_data();
    ctx->wireframe.camera.position.x = (int16_t)(center_x + dx);
    ctx->wireframe.camera.position.z = (int16_t)(center_z + dz);

    // Face toward center: yaw = 64 - orbit_angle (sin/yaw mapping for this math setup)
    ctx->wireframe.camera.yaw = (uint8_t)(64u - demo5_orbit_angle);
    ctx->wireframe.camera.moved = true;
}

static void demo5_enable_aabb(void) {
    g_demo_aabb_overlay = true;
}

static void demo5_disable_aabb(void) {
    g_demo_aabb_overlay = false;
}

static void demo5_exit(void) {
    g_demo_aabb_overlay = false;
}

static const DemoEvent demo5_events[] = {
    { "Camera orbit",       4, DEMO_EVENT_PERFRAME, demo5_orbit       },
    { "AABB overlay",       1, DEMO_EVENT_ONESHOT,  demo5_enable_aabb },
    { "AABB overlay",       4, DEMO_EVENT_PERFRAME, demo5_orbit       },    
    { "AABB off",           1, DEMO_EVENT_ONESHOT,  demo5_disable_aabb},
    { "AABB off",           4, DEMO_EVENT_PERFRAME, demo5_orbit       },        
};

static const Demo demo5 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTGC QE RF Exit: X", "Scene with Occlusion" },
    .event_count     = 5,
    .events          = demo5_events,
    .instance_count  = 4,
    .initial_instances = demo5_init,
    .initial_models  = NULL,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = true,
    .on_enter        = demo5_enter,
    .on_exit         = demo5_exit,
};

// =============================================================================
// Demo 6 — Boing! 
// Inspired by the Amiga Boing Ball demo.  A truncated icosahedron falls under
// simulated gravity, bouncing elastically off the screen floor and side walls.
// Yaw rotation reverses direction on every bounce.
// =============================================================================

/* World-space bounce parameters (object fixed at BOING_Z, camera at z=2000)  *
 * At z=400 (view_z=1000) the projection scale is 160/1000 = 0.16 px/unit:    *
 *   screen_x = 160 + pos_x * 0.16   → ±700 ≈ screen left/right              *
 *   screen_y = 120 - (pos_y-200)*0.16 → Y=300 ≈ top,  Y=-300 ≈ bottom        */
#define BOING_Z          100    /* fixed depth — close to camera for large object */
#define BOING_START_Y    300    /* initial world Y (near screen top)              */
#define BOING_FLOOR_Y  (-300)   /* floor — near screen bottom                     */
#define BOING_WALL_X     700    /* left/right walls (±)                           */
#define BOING_GRAVITY      2    /* world-units/frame² downward acceleration       */
#define BOING_VX_INIT      8    /* initial horizontal speed (world-units/f)       */
#define BOING_YAW_RATE     4    /* yaw increment per frame (uint8 wrap)           */

static int16_t g_boing_pos_x;
static int16_t g_boing_pos_y;
static int16_t g_boing_vel_x;
static int16_t g_boing_vel_y;
static int8_t  g_boing_yaw_dir;   /* +1 = forward, -1 = reverse */

static const SceneObjectParams demo6_init[] = {
    { .slot=2, .yaw=16,  .pitch=16, .roll=0, .scale=128,
      .pos_x=0, .pos_y=BOING_START_Y, .pos_z=BOING_Z }
};

static const Model3D * const demo6_models[] = {
    &g_model_truncated_icosahedron
};

static void demo6_enter(void) {
    reset_camera();
    vgk_model_slot_init(&g_model_truncated_icosahedron, 2);
    vgk_hidden_line_enable();
    vgk_near_far_coloring_enable(true);
    g_boing_pos_x   =  0;
    g_boing_pos_y   =  BOING_START_Y;
    g_boing_vel_x   =  BOING_VX_INIT;
    g_boing_vel_y   =  0;
    g_boing_yaw_dir =  1;
    stop_vgm_playback();  /* Only kick FX audio plays during this demo */
}

static void demo6_exit(void) {
    vgm_close();            /* silence any in-progress kick FX before leaving */
    start_vgm_playback();  /* restart background music VGM from the beginning */
    vgk_hidden_line_disable();
    vgk_near_far_coloring_enable(true);
}

static void demo6_static(void) {
    /* on_enter already positioned the object */
}

static void demo6_bounce(void) {
    /* apply gravity and integrate position */
    g_boing_vel_y = (int16_t)(g_boing_vel_y - BOING_GRAVITY);
    g_boing_pos_y = (int16_t)(g_boing_pos_y + g_boing_vel_y);
    g_boing_pos_x = (int16_t)(g_boing_pos_x + g_boing_vel_x);

    /* floor bounce — center kick */
    if (g_boing_pos_y < BOING_FLOOR_Y) {
        g_boing_pos_y   = BOING_FLOOR_Y;
        g_boing_vel_y   = (int16_t)(-g_boing_vel_y);
        g_boing_yaw_dir = (int8_t)(-g_boing_yaw_dir);
        start_vgm_fx_himem(KICK_CENTER_ADDR, KICK_CENTER_LEN);
    }

    /* side-wall bounces — directional kick */
    if (g_boing_pos_x > BOING_WALL_X) {
        g_boing_pos_x   =  BOING_WALL_X;
        g_boing_vel_x   = (int16_t)(-g_boing_vel_x);
        g_boing_yaw_dir = (int8_t)(-g_boing_yaw_dir);
        start_vgm_fx_himem(KICK_RIGHT_ADDR, KICK_RIGHT_LEN);
    } else if (g_boing_pos_x < -BOING_WALL_X) {
        g_boing_pos_x   = -BOING_WALL_X;
        g_boing_vel_x   = (int16_t)(-g_boing_vel_x);
        g_boing_yaw_dir = (int8_t)(-g_boing_yaw_dir);
        start_vgm_fx_himem(KICK_LEFT_ADDR, KICK_LEFT_LEN);
    }

    /* push updated position to the instance */
    g_demo_instances[0].pos_x = g_boing_pos_x;
    g_demo_instances[0].pos_y = g_boing_pos_y;

    /* advance yaw in the current direction (uint8_t wraps naturally) */
    uint8_t yaw_step = (g_boing_yaw_dir > 0)
                           ? (uint8_t)BOING_YAW_RATE
                           : (uint8_t)(256u - BOING_YAW_RATE);
    g_demo_instances[0].yaw = (uint8_t)(g_demo_instances[0].yaw + yaw_step);
}

static const DemoEvent demo6_events[] = {
    { "Boing!",  2,  DEMO_EVENT_ONESHOT,  demo6_static  },
    { "Boing!",  30, DEMO_EVENT_PERFRAME, demo6_bounce  },
};

static const Demo demo6 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTGC QE RF Exit: X", "Boing!" },
    .event_count     = 2,
    .events          = demo6_events,
    .instance_count  = 1,
    .initial_instances = demo6_init,
    .initial_models  = demo6_models,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = false,
    .on_enter        = demo6_enter,
    .on_exit         = demo6_exit,
};

// =============================================================================
// Demo 7 — Orbit Combat
// Four anacondas orbit the origin while the player flies and fires.
// =============================================================================

#define DEMO7_ANACONDA_COUNT         4u
#define DEMO7_STARFIELD_COUNT        2u
#define DEMO7_ORBIT_RADIUS           1200
#define DEMO7_ORBIT_Y                100
#define DEMO7_ORBIT_STEP             1u
#define DEMO7_PROJECTILE_SPEED       160u
#define DEMO7_PROJECTILE_MAX_DIST    4000u
#define DEMO7_PROJECTILE_SPAWN_OFFSET 128u
#define DEMO7_PROJECTILE_SCALE       128u
#define DEMO7_ANACONDA_SCALE         128u
#define DEMO7_HIT_RADIUS             200
#define DEMO7_HIT_RADIUS_SQ          ((int32_t)DEMO7_HIT_RADIUS * (int32_t)DEMO7_HIT_RADIUS)
#define DEMO7_HIT_DURATION_FRAMES    T0_TICK_FREQ
#define DEMO7_STARFIELD_SLOT         0u
#define DEMO7_PROJECTILE_SLOT        5u

typedef struct {
    bool    active;
    bool    hit_active;
    uint8_t slot;
    uint8_t orbit_angle;
    uint8_t yaw;
    uint8_t frames_remaining;
    int16_t pos_x;
    int16_t pos_y;
    int16_t pos_z;
    Model3D model;
} Demo7AnacondaState;

typedef struct {
    bool     active;
    bool     just_spawned;
    uint16_t distance_traveled;
    uint8_t  yaw;
    uint8_t  pitch;
    uint8_t  roll;
    int16_t  pos_x;
    int16_t  pos_y;
    int16_t  pos_z;
    int16_t  dir_x;
    int16_t  dir_y;
    int16_t  dir_z;
} Demo7ProjectileState;

static const uint8_t demo7_anaconda_slots[DEMO7_ANACONDA_COUNT] = { 1u, 2u, 3u, 4u };
static const uint16_t demo7_hit_colors[] = {
    0x0F0B, 0x0A0F, 0x0F02, 0x020F, 0x050A, 0x0A05, 0x0F0C, 0x0C0F
};
static const SceneObjectParams demo7_init[] = {
    { .slot=DEMO7_STARFIELD_SLOT, .yaw=228, .pitch=0, .roll=0, .scale=128,
      .pos_x=0, .pos_y=200, .pos_z=2400 }
};

static Demo7AnacondaState  g_demo7_anacondas[DEMO7_ANACONDA_COUNT];
static Demo7ProjectileState g_demo7_projectile;

static bool demo7_all_anacondas_inactive(void) {
    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        if (g_demo7_anacondas[i].active || g_demo7_anacondas[i].hit_active) {
            return false;
        }
    }

    return true;
}

static void demo7_orbit_position_update(Demo7AnacondaState *ship) {
    int16_t sinv = sin_table[ship->orbit_angle];
    int16_t cosv = sin_table[(uint8_t)(ship->orbit_angle + 64u)];

    ship->pos_x = (int16_t)(mathSignedMultiply(cosv, DEMO7_ORBIT_RADIUS) >> 14);
    ship->pos_y = DEMO7_ORBIT_Y;
    ship->pos_z = (int16_t)(mathSignedMultiply(sinv, DEMO7_ORBIT_RADIUS) >> 14);
    ship->yaw = (uint8_t)(0u - ship->orbit_angle);
}

static void demo7_reset_runtime(void) {
    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        Demo7AnacondaState *ship = &g_demo7_anacondas[i];
        ship->active = true;
        ship->hit_active = false;
        ship->slot = demo7_anaconda_slots[i];
        ship->orbit_angle = (uint8_t)(i * 64u);
        ship->frames_remaining = 0u;
        ship->model = g_model_anaconda;
        ship->model.object_color = g_model_anaconda.object_color;
        demo7_orbit_position_update(ship);
    }

    g_demo7_projectile.active = false;
    g_demo7_projectile.just_spawned = false;
    g_demo7_projectile.distance_traveled = 0u;
    g_demo7_projectile.yaw = 0u;
    g_demo7_projectile.pitch = 0u;
    g_demo7_projectile.roll = 0u;
    g_demo7_projectile.pos_x = 0;
    g_demo7_projectile.pos_y = 0;
    g_demo7_projectile.pos_z = 0;
    g_demo7_projectile.dir_x = 0;
    g_demo7_projectile.dir_y = 0;
    g_demo7_projectile.dir_z = 0;
}

static void demo7_rebuild_instances(void) {
    uint8_t count = 0u;
    GameContext *ctx = game_state_data();

    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        const Demo7AnacondaState *ship = &g_demo7_anacondas[i];
        if (!ship->active) {
            continue;
        }

        g_demo_instances[count].slot = ship->slot;
        g_demo_instances[count].pitch = 0u;
        g_demo_instances[count].yaw = ship->yaw;
        g_demo_instances[count].roll = 0u;
        g_demo_instances[count].scale = DEMO7_ANACONDA_SCALE;
        g_demo_instances[count].pos_x = ship->pos_x;
        g_demo_instances[count].pos_y = ship->pos_y;
        g_demo_instances[count].pos_z = ship->pos_z;
        g_demo_models[count] = &ship->model;
        ++count;
    }

    if (g_demo7_projectile.active) {
        g_demo_instances[count].slot = DEMO7_PROJECTILE_SLOT;
        g_demo_instances[count].pitch = g_demo7_projectile.pitch;
        g_demo_instances[count].yaw = g_demo7_projectile.yaw;
        g_demo_instances[count].roll = g_demo7_projectile.roll;
        g_demo_instances[count].scale = DEMO7_PROJECTILE_SCALE;
        g_demo_instances[count].pos_x = g_demo7_projectile.pos_x;
        g_demo_instances[count].pos_y = g_demo7_projectile.pos_y;
        g_demo_instances[count].pos_z = g_demo7_projectile.pos_z;
        g_demo_models[count] = &g_model_projectile;
        ++count;
    }

    g_demo_instances[count].slot = DEMO7_STARFIELD_SLOT;
    g_demo_instances[count].pitch = 0u;
    g_demo_instances[count].yaw = 228u;
    g_demo_instances[count].roll = 0u;
    g_demo_instances[count].scale = 128u;
    g_demo_instances[count].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[count].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[count].pos_z = ctx->wireframe.camera.position.z;
    g_demo_models[count] = &g_model_starfield;
    ++count;

    g_demo_instances[count].slot = DEMO7_STARFIELD_SLOT;
    g_demo_instances[count].pitch = 9u;
    g_demo_instances[count].yaw = 9u;
    g_demo_instances[count].roll = 9u;
    g_demo_instances[count].scale = 192u;
    g_demo_instances[count].pos_x = ctx->wireframe.camera.position.x;
    g_demo_instances[count].pos_y = ctx->wireframe.camera.position.y;
    g_demo_instances[count].pos_z = ctx->wireframe.camera.position.z;
    g_demo_models[count] = &g_model_starfield;
    ++count;

    g_demo_instance_count = count;
}

static void demo7_spawn_projectile(void) {
    GameContext *ctx = game_state_data();
    int16_t fwd_x;
    int16_t fwd_y;
    int16_t fwd_z;
    int16_t spawn_dx;
    int16_t spawn_dy;
    int16_t spawn_dz;

    if (g_demo7_projectile.active) {
        return;
    }

    game_state_camera_basis_get(&fwd_x, &fwd_y, &fwd_z, NULL, NULL);
    spawn_dx = (int16_t)(mathSignedMultiply(fwd_x, DEMO7_PROJECTILE_SPAWN_OFFSET) >> 14);
    spawn_dy = (int16_t)(mathSignedMultiply(fwd_y, DEMO7_PROJECTILE_SPAWN_OFFSET) >> 14);
    spawn_dz = (int16_t)(mathSignedMultiply(fwd_z, DEMO7_PROJECTILE_SPAWN_OFFSET) >> 14);

    g_demo7_projectile.active = true;
    g_demo7_projectile.just_spawned = true;
    g_demo7_projectile.distance_traveled = 0u;
    g_demo7_projectile.yaw = ctx->wireframe.camera.yaw;
    g_demo7_projectile.pitch = ctx->wireframe.camera.pitch;
    g_demo7_projectile.roll = ctx->wireframe.camera.roll;
    g_demo7_projectile.pos_x = (int16_t)(ctx->wireframe.camera.position.x + spawn_dx);
    g_demo7_projectile.pos_y = (int16_t)(ctx->wireframe.camera.position.y + spawn_dy);
    g_demo7_projectile.pos_z = (int16_t)(ctx->wireframe.camera.position.z + spawn_dz);
    g_demo7_projectile.dir_x = fwd_x;
    g_demo7_projectile.dir_y = fwd_y;
    g_demo7_projectile.dir_z = fwd_z;
    /* Force camera re-upload on this render frame in case DSP state is stale. */
    ctx->wireframe.camera.moved = true;
}

static void demo7_update_projectile(void) {
    uint16_t remaining;
    uint16_t step;

    if (!g_demo7_projectile.active) {
        return;
    }

    if (g_demo7_projectile.just_spawned) {
        g_demo7_projectile.just_spawned = false;
        return;
    }

    if (g_demo7_projectile.distance_traveled >= DEMO7_PROJECTILE_MAX_DIST) {
        g_demo7_projectile.active = false;
        return;
    }

    remaining = (uint16_t)(DEMO7_PROJECTILE_MAX_DIST - g_demo7_projectile.distance_traveled);
    step = remaining < DEMO7_PROJECTILE_SPEED ? remaining : DEMO7_PROJECTILE_SPEED;
    if (step == 0u) {
        g_demo7_projectile.active = false;
        return;
    }

    g_demo7_projectile.pos_x = (int16_t)(g_demo7_projectile.pos_x +
        (int16_t)(mathSignedMultiply(g_demo7_projectile.dir_x, (int16_t)step) >> 14));
    g_demo7_projectile.pos_y = (int16_t)(g_demo7_projectile.pos_y +
        (int16_t)(mathSignedMultiply(g_demo7_projectile.dir_y, (int16_t)step) >> 14));
    g_demo7_projectile.pos_z = (int16_t)(g_demo7_projectile.pos_z +
        (int16_t)(mathSignedMultiply(g_demo7_projectile.dir_z, (int16_t)step) >> 14));
    g_demo7_projectile.distance_traveled = (uint16_t)(g_demo7_projectile.distance_traveled + step);
}

static void demo7_check_hits(void) {
    if (!g_demo7_projectile.active) {
        return;
    }

    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        Demo7AnacondaState *ship = &g_demo7_anacondas[i];
        int32_t dx;
        int32_t dy;
        int32_t dz;
        int32_t distance_sq;

        if (!ship->active || ship->hit_active) {
            continue;
        }

        dx = (int32_t)g_demo7_projectile.pos_x - (int32_t)ship->pos_x;
        dy = (int32_t)g_demo7_projectile.pos_y - (int32_t)ship->pos_y;
        dz = (int32_t)g_demo7_projectile.pos_z - (int32_t)ship->pos_z;
        distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq > DEMO7_HIT_RADIUS_SQ) {
            continue;
        }

        g_demo7_projectile.active = false;
        ship->hit_active = true;
        ship->frames_remaining = DEMO7_HIT_DURATION_FRAMES;
        ship->model.object_color = demo7_hit_colors[0];
        return;
    }
}

static void demo7_update_hits(void) {
    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        Demo7AnacondaState *ship = &g_demo7_anacondas[i];
        uint8_t color_idx;

        if (!ship->active || !ship->hit_active) {
            continue;
        }

        if (ship->frames_remaining == 0u) {
            ship->hit_active = false;
            ship->active = false;
            ship->model.object_color = g_model_anaconda.object_color;
            continue;
        }

        color_idx = (uint8_t)((DEMO7_HIT_DURATION_FRAMES - ship->frames_remaining) %
                              (sizeof(demo7_hit_colors) / sizeof(demo7_hit_colors[0])));
        ship->model.object_color = demo7_hit_colors[color_idx];
        --ship->frames_remaining;
    }

    if (demo7_all_anacondas_inactive()) {
        demo7_reset_runtime();
    }
}

static void demo7_tick(void) {
    InputState *input = input_state_data();

    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        Demo7AnacondaState *ship = &g_demo7_anacondas[i];
        if (!ship->active || ship->hit_active) {
            continue;
        }

        demo7_orbit_position_update(ship);
        ship->orbit_angle = (uint8_t)(ship->orbit_angle + DEMO7_ORBIT_STEP);
    }

    if (input->edge.firePrimary) {
        demo7_spawn_projectile();
    }

    demo7_update_projectile();
    demo7_check_hits();
    if (g_demo7_projectile.active &&
        g_demo7_projectile.distance_traveled >= DEMO7_PROJECTILE_MAX_DIST) {
        g_demo7_projectile.active = false;
    }
    demo7_update_hits();
    demo7_rebuild_instances();
}

static void demo7_enter(void) {
    reset_camera();
    vgk_hidden_line_enable();
    vgk_near_far_coloring_enable(true);
    demo7_reset_runtime();

    for (uint8_t i = 0; i < DEMO7_ANACONDA_COUNT; ++i) {
        vgk_model_slot_init(&g_demo7_anacondas[i].model, g_demo7_anacondas[i].slot);
    }
    vgk_model_slot_init(&g_model_projectile, DEMO7_PROJECTILE_SLOT);
    vgk_model_slot_init(&g_model_starfield, DEMO7_STARFIELD_SLOT);
    /* Pre-load the projectile slot so the DSP geometry plugin performs its
     * first-time slot initialisation now (while camera is at default), not
     * on the fire frame when the camera may have moved. */
    vgk_model_load(DEMO7_PROJECTILE_SLOT);

    POKE(0xD00D, 0x00);
    POKE(0xD00E, 0x00);
    POKE(0xD00F, 0x00);
    bitmapSetVisible(0, true);
    demo7_rebuild_instances();
}

static void demo7_exit(void) {
    g_demo7_projectile.active = false;
    bitmapSetVisible(0, false);
    vgk_hidden_line_disable();
    vgk_near_far_coloring_enable(true);
    POKE(0xD00D, 0x33);
    POKE(0xD00E, 0x33);
    POKE(0xD00F, 0x33);
}

static const DemoEvent demo7_events[] = {
    { "Orbit Combat - Move and Fire", 0, DEMO_EVENT_PERFRAME, demo7_tick }
};

static const Demo demo7 = {
    .title           = { "VS1053b Geometry Kernel Demo",
                         "Camera:WASDTGC QE RF Space:Fire Exit:X",
                         "Orbit Combat" },
    .event_count     = 1,
    .events          = demo7_events,
    .instance_count  = 1,
    .initial_instances = demo7_init,
    .initial_models  = NULL,
    .near_color      = 0x0B,
    .far_color       = 0x0D,
    .use_scene_api   = false,
    .on_enter        = demo7_enter,
    .on_exit         = demo7_exit,
};

// =============================================================================
// Demo registry
// =============================================================================

static const Demo * const g_all_demos[] = {
   &demo6, &demo1, &demo2, &demo3, &demo4, &demo5, 
   &demo7
};
#define DEMO_COUNT 7u

void demos_register(void) {
    demo_engine_init(g_all_demos, DEMO_COUNT);
}
