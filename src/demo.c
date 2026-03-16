#include "f256lib.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/demo.h"
#include "../include/game_state.h"
#include "../include/geometry_kernel.h"
#include "../include/render.h"
#include "../include/timer.h"

// ---------------------------------------------------------------------------
// Exported SoA animation state
// ---------------------------------------------------------------------------
SceneObjectParams g_demo_instances[DEMO_MAX_INSTANCES];
uint8_t           g_demo_instance_count;
uint8_t g_instance_yaw_rate[DEMO_MAX_INSTANCES];
uint8_t g_instance_pitch_rate[DEMO_MAX_INSTANCES];
uint8_t g_instance_roll_rate[DEMO_MAX_INSTANCES];
int8_t  g_instance_scale_dir[DEMO_MAX_INSTANCES];
bool    g_demo_aabb_overlay;
const Model3D *g_demo_models[DEMO_MAX_INSTANCES];

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static const Demo * const *g_demos;
static uint8_t  g_demo_count;
static uint8_t  g_demo_idx;
static uint8_t  g_event_idx;
static uint16_t g_event_frame;
static uint16_t g_idle_frame;
static uint8_t  g_anim_tick;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static void print_centered(uint8_t row, const char *text) {
    // Always clear the full row first
    textGotoXY(0, row);
    textPrint((char *)"                                                                                ");
    if (!text) return;
    uint8_t len = (uint8_t)strlen(text);
    uint8_t col = (len < DEMO_TEXT_COLS) ? (DEMO_TEXT_COLS - len) / 2u : 0u;
    textGotoXY(col, row);
    textPrint((char *)text);
}

static void display_demo_titles(void) {
    const Demo *d = g_demos[g_demo_idx];
    for (uint8_t r = 0; r < 3u; ++r) {
        print_centered(r, d->title[r]);
    }
}

static void start_event(void) {
    const Demo      *d = g_demos[g_demo_idx];
    const DemoEvent *e = &d->events[g_event_idx];
    g_event_frame = 0;
    g_anim_tick   = 0;
    g_idle_frame  = 0;
    print_centered(3u, e->description);
    if (e->type == DEMO_EVENT_ONESHOT && e->callback) {
        e->callback();
    }
}

static void start_demo(uint8_t idx) {
    const Demo *d = g_demos[idx];
    g_demo_idx            = idx;
    g_event_idx           = 0;
    g_idle_frame          = 0;
    g_demo_aabb_overlay   = false;
    g_demo_instance_count = d->instance_count;
    memcpy(g_demo_instances, d->initial_instances,
           d->instance_count * sizeof(SceneObjectParams));
    if (d->initial_models) {
        memcpy(g_demo_models, d->initial_models,
               d->instance_count * sizeof(const Model3D *));
    }
    memset(g_instance_yaw_rate,   0, sizeof(g_instance_yaw_rate));
    memset(g_instance_pitch_rate, 0, sizeof(g_instance_pitch_rate));
    memset(g_instance_roll_rate,  0, sizeof(g_instance_roll_rate));
    memset(g_instance_scale_dir,  0, sizeof(g_instance_scale_dir));
    display_demo_titles();
    if (d->on_enter) {
        d->on_enter();
    }
    start_event();
}

static void advance_event(void) {
    const Demo *d = g_demos[g_demo_idx];
    ++g_event_idx;
    if (g_event_idx >= d->event_count) {
        uint8_t next = (uint8_t)(g_demo_idx + 1u);
        if (next >= g_demo_count) {
            next = 0;
        }
        if (d->on_exit) {
            d->on_exit();
        }
        start_demo(next);
    } else {
        start_event();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void demo_engine_init(const Demo * const *demos, uint8_t count) {
    g_demos      = demos;
    g_demo_count = count;
}

void demo_engine_start(uint8_t index) {
    start_demo(index);
}

bool demo_engine_update(InputState *input) {
    if (input->edge.exit) {
        return false;
    }

    bool camera_held = input->hold.w    || input->hold.a  || input->hold.s   ||
                       input->hold.d    || input->hold.t  || input->hold.g   ||
                       input->hold.rotateLeft  || input->hold.rotateRight    ||
                       input->hold.rotateUp    || input->hold.rotateDown;
    if (camera_held) {
        game_state_update_3d(input);
        g_idle_frame = 0;
    } else {
        if (g_idle_frame < 0xFFFFu) {
            ++g_idle_frame;
        }
    }

    if (g_event_frame < 0xFFFFu) {
        ++g_event_frame;
    }
    ++g_anim_tick;

    const Demo      *d = g_demos[g_demo_idx];
    const DemoEvent *e = &d->events[g_event_idx];

    if (e->type == DEMO_EVENT_PERFRAME && g_anim_tick >= DEMO_ANIM_FRAME_INTERVAL) {
        g_anim_tick = 0;
        if (e->callback) {
            e->callback();
        }
    }

    uint16_t duration_frames = (uint16_t)e->duration_secs * (uint16_t)T0_TICK_FREQ;
    bool advance = (duration_frames > 0u && g_event_frame >= duration_frames);
    // Idle timeout only applies to open-ended events (duration_secs == 0)
    if (!advance && e->duration_secs == 0u && g_idle_frame >= DEMO_IDLE_TIMEOUT_FRAMES) {
        advance = true;
    }

    if (advance) {
        advance_event();
    }

    return true;
}

void demo_engine_render(uint8_t draw_layer) {
    const Demo *d = g_demos[g_demo_idx];
    if (d->use_scene_api) {
        scene_get_screen_edges(g_demo_instance_count, g_demo_instances,
                               d->near_color, d->far_color, draw_layer);
        if (g_demo_aabb_overlay) {
            render_scene_aabb_overlay(draw_layer);
        }
    } else {
        for (uint8_t i = 0; i < g_demo_instance_count; ++i) {
            const SceneObjectParams *inst = &g_demo_instances[i];
            setup_object_params(inst->pitch, inst->yaw, inst->roll, inst->scale,
                                inst->pos_x, inst->pos_y, inst->pos_z);
            geometry_kernel_load(inst->slot);
            geometry_kernel_reset();
            trigger_geometry_kernel();
            geometry_kernel_wait_complete(10000);
            get_screen_edges_with_depth((Model3D *)g_demo_models[i], draw_layer);
            geometry_kernel_yield(); /* service audio after per-object SCI readback */
        }
    }
}
