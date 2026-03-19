#include "f256lib.h"

#include "../include/render.h"

#include <string.h>

#include "../include/3d_object.h"
#include "../include/3d_pipeline.h"
#include "../include/draw_line.h"
#include "../include/geometry_kernel.h"
#include "../include/video.h"
#include "../include/input.h"
#include "../include/game_state.h"
#include "../include/scene.h"
#include "../include/demo.h"

uint8_t out_edge_count = 0;

// Shared display buffer state for proper double buffering
static uint8_t g_visible_layer = 1; // Start with Layer 1 visible since Layer 0 is used for the cockpit overlay

static bool scene_meta_has_visible_aabb(const SceneObjectMeta *meta) {
    return meta->edge_count != 0 &&
           meta->aabb_min_x <= meta->aabb_max_x &&
           meta->aabb_min_y <= meta->aabb_max_y;
}

void render_scene_aabb_overlay(uint8_t draw_layer) {
    SceneResult result;

    scene_get_result(&result);
    for (uint8_t obj = 0; obj < result.n_objects; ++obj) {
        SceneObjectMeta meta;
        uint16_t min_x;
        uint16_t max_x;
        uint8_t min_y;
        uint8_t max_y;

        scene_get_object_meta(obj, &meta);
        geometry_kernel_yield();
        if (!scene_meta_has_visible_aabb(&meta)) {
            continue;
        }

        min_x = (uint16_t)meta.aabb_min_x;
        max_x = (uint16_t)meta.aabb_max_x;
        min_y = (uint8_t)meta.aabb_min_y;
        max_y = (uint8_t)meta.aabb_max_y;

        add_line_to_list(min_x, min_y, max_x, min_y, 15);
        add_line_to_list(max_x, min_y, max_x, max_y, 15);
        add_line_to_list(max_x, max_y, min_x, max_y, 15);
        add_line_to_list(min_x, max_y, min_x, min_y, 15);
    }

    if (g_line_count != 0) {
        draw_lines_asm(draw_layer);
        reset_line_list();
    }
}

__attribute__((noinline)) void render_frame(GameContext *ctx) {
    uint8_t draw_layer = g_visible_layer==1 ? 2 : 1; // 1 or 2

    bitmapSetActive(draw_layer);

    Camera *camera = &ctx->wireframe.camera;
    if (camera->moved) {
        setup_camera_params(camera->pitch, camera->yaw, camera->roll,
                            camera->position.x, camera->position.y,
                            camera->position.z);
        camera->moved = false;
    }

    demo_engine_render(draw_layer);

    video_wait_vblank();
    bitmapSetVisible(draw_layer, true);
    bitmapSetVisible(g_visible_layer, false);
    dmaBitmapClear(g_visible_layer);
    g_visible_layer = draw_layer;
}
