#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
#include <stdio.h>
#include "../include/game_state.h"
#include "../include/input.h"
#include "../include/input_handler.h"
#include "../include/render.h"
#include "../include/video.h"
#include "../include/3d_math.h"
#include "../include/geometry_kernel.h"
#include "../include/timer.h"
#include "../include/demo.h"
#include "../include/demos.h"
#include "../include/vgm_himem.h"
#include "../include/vgm.h"
#include "../include/codec.h"

extern void vgk_plugin_init(void);
 


/* -----------------------------------------------------------------------
 * VGM playback state
 * ----------------------------------------------------------------------- */

/* First 512 KiB extended RAM block used as VGM cache.  The full file is
 * loaded here before the animation loop so SD-card SPI reads never occur
 * during playback; buffer refills are fast far copies. */
#define VGM_HIMEM_BASE    0x080000UL

static vgm_himem_ctx_t g_vgm_himem;
static bool            g_vgm_open = false;
static const char     *g_vgm_path = NULL;

/* Re-arm the player from the beginning of the cached stream. */
static void start_vgm_playback(void) {
    g_vgm_himem.pos = 0u;  /* reset so vgm_open() reads header from pos 0 */
    g_vgm_open = (vgm_open(vgm_himem_read, vgm_himem_seek,
                           &g_vgm_himem) == VGM_PLAYING);
}

/* Load VGM into high memory (one-time SD read), then start playback.
 * Silently returns on any failure so the demo still runs without audio. */
static void initialize_vgm_playback(const char *path) {
    if (!vgm_himem_load(path, VGM_HIMEM_BASE, &g_vgm_himem)) return;
    start_vgm_playback();
}

/* Service one VGM step; loop automatically on end-of-stream. */
static void process_vgm_tick(void) {
    if (!g_vgm_open) return;
    vgm_status_t s = vgm_service();

    if (s == VGM_DONE) {
        vgm_close();  /* silence chip, restore fixed-rate T0 */
        start_vgm_playback();               /* loop: re-open from cached stream      */
    } else if (s == VGM_ERROR) {
        vgm_close();
        g_vgm_open = false;
    }
}

static void init_models(void) {
    vgk_model_slot_init(&g_model_cube, 0);
    vgk_model_slot_init(&g_model_anaconda, 1);
}

int main(int argc, char *argv[]) {
    /* Resolve VGM file path from command-line or fall back to default */
    g_vgm_path = (argc >= 2 && argv[1] != NULL && argv[1][0] != '\0')
                 ? argv[1] : NULL;

    f256Init();
    vs1053_clock_boost();
    vs1053_plugin_load();
    vgk_plugin_init();
    vs1053_dac_mute();
    vs1053_dac_interrupt_disable(); // Decoder RAM is being used.
    game_state_init(STATE_DEMO);
    input_handler_init();
    video_init();

    // 4:3 aspect 320x240 with vertical fov 90 degrees
    vgk_projection_params_init(120, 160, 120, -64);

    init_models();
    codec_init();
    if (g_vgm_path != NULL) {
      initialize_vgm_playback(g_vgm_path); /* start VGM after VS1053B is fully set up */
      if (!g_vgm_open) {
        textPrint("Failed to load VGM file (");
        textPrint(g_vgm_path);
        textPrint("); continuing without audio.");
        timer_t0_alarm_set(TIMER_ALARM_GENERAL0, 120); /* two second exit wait */
        while (true) {
          if (timer_t0_alarm_check(TIMER_ALARM_GENERAL0)) {
            break;
          }
        }
      }
    }

    vgk_yield_cb_set(process_vgm_tick);  /* service audio during DSP waits */

    demos_register();
    demo_engine_start(0);

    timer_t0_alarm_set(TIMER_ALARM_GENERAL0, 1);
    while (true) {
        input_handler_poll();
        InputState *input = input_state_data();
        if (input->edge.exit) {
            break;
        }        
        if (!demo_engine_update(input)) {
            break;
        }
        render_frame(game_state_data());
        game_state_increment_frame();
        input_state_clear_edges(input);

        while (!timer_t0_alarm_check(TIMER_ALARM_GENERAL0)) {
            process_vgm_tick();  /* service VGM while waiting for next frame tick */
        }
        timer_t0_alarm_set(TIMER_ALARM_GENERAL0, 1);
    }

    if (g_vgm_open) {
        vgm_close();
    }
    timer_t0_alarm_set(TIMER_ALARM_GENERAL0, 1);  /* 1/T0_TICK_FREQ exit wait */
    while(true) {
        if (timer_t0_alarm_check(TIMER_ALARM_GENERAL0)) {
            break;
        }
    }
    return 0;
}
