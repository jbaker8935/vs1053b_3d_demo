#include "f256lib.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/demo.h"
#include "../include/demos.h"
#include "../include/3d_object.h"
#include "../include/geometry_kernel.h"
#include "../include/game_state.h"


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
    vgk_no_near_far_coloring = true;
}

static void demo1_hidden_line(void) {
    vgk_model_hidden_line_init(&g_model_cube, 0);
    vgk_hidden_line_enable();
}

static void demo1_nearfar(void) {
    vgk_no_near_far_coloring = false;
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
    vgk_no_near_far_coloring = true;
}

static void demo1_exit(void) {
    vgk_hidden_line_disable();
    vgk_no_near_far_coloring = false;
}

static const DemoEvent demo1_events[] = {
    { "Wireframe",           3, DEMO_EVENT_ONESHOT,  demo1_wireframe   },
    { "Hidden line removal", 3, DEMO_EVENT_ONESHOT,  demo1_hidden_line },
    { "Near/far coloring",   3, DEMO_EVENT_ONESHOT,  demo1_nearfar     },
    { "Rotation",            5, DEMO_EVENT_PERFRAME, demo1_rotate      },
    { "Scaling",             5, DEMO_EVENT_PERFRAME, demo1_scale       },
};

static const Demo demo1 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTG QE RF Exit: X", "Simple Cube Primitive" },
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
    vgk_no_near_far_coloring = false;
    g_cube2 = g_model_cube; g_cube2.object_color = 0x0505;
    g_cube3 = g_model_cube; g_cube3.object_color = 0x0707;
    g_cube4 = g_model_cube; g_cube4.object_color = 0x0909;
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
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTG QE RF Exit: X", "Multiple Objects" },
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
    { .slot=6, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=100, .pos_z=-2000 },            
    { .slot=5, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=0, .pos_z=0 },
    { .slot=5, .yaw=9, .pitch=42, .roll=0, .scale=192, .pos_x=0, .pos_y=0, .pos_z=0 },
};

static const Model3D * const demo3_models[] = { &g_model_anaconda, &g_model_projectile,
     &g_model_starfield, &g_model_starfield };

static void demo3_enter(void) {
    reset_camera();
    vgk_model_slot_init(&g_model_starfield, 5);
    vgk_model_slot_init(&g_model_projectile, 6);
    // graphics background
	POKE(0xD00D, 0x00);
    POKE(0xD00E, 0x00);
    POKE(0xD00F, 0x00);    
    bitmapSetVisible(0, true);
}

static void demo3_setup(void) {
    vgk_model_hidden_line_init(&g_model_anaconda, 1);
    vgk_hidden_line_enable();
    vgk_no_near_far_coloring = false;
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
    // Increment Z to move anaconda toward camera (camera is at z=1400)
    g_demo_instances[0].pos_z = (int16_t)(g_demo_instances[0].pos_z + 24);
    g_demo_instances[0].pos_x = (int16_t)(g_demo_instances[0].pos_x - 2);

    // Increment Z to move projectile toward camera (camera is at z=1400)
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
    // Increment Z to move anaconda toward camera (camera is at z=1400)
    g_demo_instances[0].pos_z = (int16_t)(g_demo_instances[0].pos_z + 24);
    g_demo_instances[0].pos_x = (int16_t)(g_demo_instances[0].pos_x - 2);

    // Increment Z to move second anaconda toward camera (camera is at z=1400)
    g_demo_instances[1].pos_z = (int16_t)(g_demo_instances[1].pos_z + 48);
    g_demo_instances[1].pos_y = (int16_t)(g_demo_instances[1].pos_x + 2);
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
    vgk_no_near_far_coloring = false;
}

static const DemoEvent demo3_events[] = {
    { "Anaconda incoming...",  2,  DEMO_EVENT_ONESHOT,  demo3_setup },
    { "Flyby",                 8, DEMO_EVENT_PERFRAME, demo3_fly   },
    { "Oh, No",                1, DEMO_EVENT_ONESHOT, demo3_setup_anaconda_2   }, 
    { "Double Anaconda Flyby", 8, DEMO_EVENT_PERFRAME, demo3_ohno_fly   },   
};

static const Demo demo3 = {
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTG QE RF Exit: X", "Anaconda Flyby" },
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
    { .slot=5, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=0, .pos_y=0, .pos_z=0 },
    { .slot=5, .yaw=9, .pitch=42, .roll=0, .scale=192, .pos_x=0, .pos_y=0, .pos_z=0 },     
};

static void demo4_enter(void) {
    reset_camera();    
    // graphics background
	POKE(0xD00D, 0x00);
    POKE(0xD00E, 0x00);
    POKE(0xD00F, 0x00);    
    bitmapSetVisible(0, true);
    vgk_hidden_line_enable();
    vgk_no_near_far_coloring = true;
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
    vgk_no_near_far_coloring = false;
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
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTG QE RF Exit: X", "Anaconda Color Cycling" },
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
    ctx->wireframe.camera.position.z = 1400;
    ctx->wireframe.camera.yaw   = 0;
    ctx->wireframe.camera.pitch = 0;
    ctx->wireframe.camera.roll  = 0;
    ctx->wireframe.camera.moved = true;
    vgk_hidden_line_enable();
    vgk_no_near_far_coloring = false;
}

static uint8_t demo5_orbit_angle;

static void demo5_orbit(void) {
    // Circle-strafe around the scene center (0,0). Keep camera aimed at center.
    const int16_t radius = 1000;
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
    .title           = { "VS1053b Geometry Kernel Demo", "Camera Control:WASDTG QE RF Exit: X", "Scene with Occlusion" },
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
// Demo registry
// =============================================================================

static const Demo * const g_all_demos[] = {
    &demo1, &demo2, &demo3, &demo4, &demo5
};
#define DEMO_COUNT 5u

void demos_register(void) {
    demo_engine_init(g_all_demos, DEMO_COUNT);
}
