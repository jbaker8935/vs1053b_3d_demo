#ifndef DEMO_H
#define DEMO_H

#include <stdbool.h>
#include <stdint.h>
#include "../include/geometry_kernel.h"
#include "../include/input.h"

#define DEMO_MAX_INSTANCES 8
#define DEMO_MAX_EVENTS    16
#define DEMO_MAX_DEMOS     8
#define DEMO_TEXT_COLS     80
// Frames of no camera input before auto-advancing (30 fps * 5 s)
#define DEMO_IDLE_TIMEOUT_FRAMES (5u * 30u)

// Event type identifiers
#define DEMO_EVENT_ONESHOT  0u   // callback called once when event begins
#define DEMO_EVENT_PERFRAME 1u   // callback called every DEMO_ANIM_FRAME_INTERVAL render frames

typedef void (*demo_fn_t)(void);

typedef struct {
    const char *description;    // subtitle shown at text row 3 (NULL = clear row)
    uint8_t     duration_secs;  // auto-advance after this many seconds (0 = advance only by idle)
    uint8_t     type;           // DEMO_EVENT_ONESHOT or DEMO_EVENT_PERFRAME
    demo_fn_t   callback;       // NULL ok
} DemoEvent;

typedef struct {
    const char               *title[3];            // text rows 0-2 (NULL = blank that row)
    uint8_t                   event_count;
    const DemoEvent          *events;
    uint8_t                   instance_count;
    const SceneObjectParams  *initial_instances;   // copied to RAM on demo start
    const Model3D * const    *initial_models;      // parallel to initial_instances; NULL = set by on_enter
    uint8_t                   near_color;          // used only by scene API path
    uint8_t                   far_color;           // used only by scene API path
    bool                      use_scene_api;       // true = scene_get_screen_edges; false = single-object loop
    demo_fn_t                 on_enter;            // setup called before first event; NULL ok
    demo_fn_t                 on_exit;             // teardown after last event; NULL ok
} Demo;

// ---------------------------------------------------------------------------
// Per-instance mutable animation state (SoA for 6502 indexed access)
// ---------------------------------------------------------------------------
extern SceneObjectParams g_demo_instances[DEMO_MAX_INSTANCES];
extern uint8_t           g_demo_instance_count;
extern uint8_t g_instance_yaw_rate[DEMO_MAX_INSTANCES];
extern uint8_t g_instance_pitch_rate[DEMO_MAX_INSTANCES];
extern uint8_t g_instance_roll_rate[DEMO_MAX_INSTANCES];
extern int8_t  g_instance_scale_dir[DEMO_MAX_INSTANCES];

// Set to true by demo callbacks to draw AABB box overlay this frame
extern bool g_demo_aabb_overlay;
// Model pointer for each instance — used by single-object render path.
// Populated from Demo.initial_models on demo start; on_enter can override.
extern const Model3D *g_demo_models[DEMO_MAX_INSTANCES];

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void demo_engine_init(const Demo * const *demos, uint8_t count);
void demo_engine_start(uint8_t index);
bool demo_engine_update(InputState *input);   // returns false when exit pressed
void demo_engine_render(uint8_t draw_layer);

#endif // DEMO_H
