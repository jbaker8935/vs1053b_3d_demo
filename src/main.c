#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
#include <stdio.h>
#include "../include/game_state.h"
#include "../include/input.h"
#include "../include/input_handler.h"
#include "../include/render.h"
#include "../include/video.h"
#include "../include/3d_math.h"
#include "../include/muVS1053b.h"
#include "../include/geometry_kernel.h"
#include "../include/timer.h"
#include "../include/demo.h"
#include "../include/demos.h"
#include "../include/vgm_himem.h"
#include "../include/vgm.h"

extern void loadVS1053Plugin(void);
extern void boostVSClock(void);
extern void initialize_plugin(void);

/*  Codec Setup */
void init_codec() {
    POKE(0xD620, 0x1F);
    POKE(0xD621, 0x2A);
    POKE(0xD622, 0x01);
    while (PEEK(0xD622) & 0x01)
        ;

    // Set volume to a reasonable level

    POKE(0xD620, 0x68);
    POKE(0xD621, 0x05);
    POKE(0xD622, 0x01);
    while (PEEK(0xD622) & 0x01)
        ;

}


/* -----------------------------------------------------------------------
 * VGM playback state
 * ----------------------------------------------------------------------- */

/* First 512 KiB extended RAM block used as VGM cache.  The full file is
 * loaded here before the animation loop so SD-card SPI reads never occur
 * during playback; buffer refills are fast far copies. */
#define VGM_HIMEM_BASE    0x080000UL

static vgm_player_t    g_vgm_player;
static vgm_himem_ctx_t g_vgm_himem;
static bool            g_vgm_open = false;
static const char     *g_vgm_path = NULL;
static uint8_t         g_raw_hdr[4];   /* direct read from himem after load */
static uint8_t         g_wt_flat[4];   /* flat smoke-test readback          */
static uint8_t         g_wt_xbank[4];  /* bank-crossing smoke readback      */

/* Re-arm the player from the beginning of the cached stream. */
static void vgm_start(void) {
    g_vgm_himem.pos = 0u;  /* reset so vgm_open() reads header from pos 0 */
    g_vgm_open = (vgm_open(&g_vgm_player,
                            vgm_himem_read, vgm_himem_seek,
                            &g_vgm_himem) == VGM_PLAYING);
}

/* Load VGM into high memory (one-time SD read), then start playback.
 * Silently returns on any failure so the demo still runs without audio. */
static void vgm_init(const char *path) {
    if (!vgm_himem_load(path, VGM_HIMEM_BASE, &g_vgm_himem)) return;
    /* Read back first 4 bytes directly from high memory before vgm_open
     * so we can distinguish write-path vs read-path corruption. */
    movedown24((uint32_t)(uintptr_t)g_raw_hdr, VGM_HIMEM_BASE, 4u);
    vgm_start();
}

/* Service one VGM step; loop automatically on end-of-stream. */
static void vgm_tick(void) {
    if (!g_vgm_open) return;
    vgm_status_t s = vgm_service(&g_vgm_player);
    if (s == VGM_DONE) {
        vgm_close(&g_vgm_player);  /* silence chip, restore fixed-rate T0 */
        vgm_start();               /* loop: re-open from cached stream      */
    } else if (s == VGM_ERROR) {
        vgm_close(&g_vgm_player);
        g_vgm_open = false;
    }
}

static void init_models(void) {
    load_model_to_plugin(&g_model_cube, 0);
    load_model_to_plugin(&g_model_anaconda, 1);
}

int main(int argc, char *argv[]) {
    /* Resolve VGM file path from command-line or fall back to default */
    g_vgm_path = (argc >= 2 && argv[1] != NULL && argv[1][0] != '\0')
                 ? argv[1] : NULL;

    f256Init();
    boostVSClock();
    loadVS1053Plugin();
    initialize_plugin();
    vs1053_mute_dac();
    vs1053_disable_dac_interrupt();
    game_state_init(STATE_DEMO);
    input_handler_init();
    video_init();

    // 4:3 aspect 320x240 with vertical fov 90 degrees
    setup_projection_params(120, 160, 120, -64);

    init_models();
    init_codec();
    if (g_vgm_path != NULL) {
      vgm_init(g_vgm_path); /* start VGM after VS1053B is fully set up */
      if (!g_vgm_open) {
        textPrint("Failed to load VGM file (");
        textPrint(g_vgm_path);
        textPrint("); continuing without audio.");
        setAlarm(TIMER_ALARM_GENERAL0, 120); /* two second exit wait */
        while (true) {
          if (checkAlarm(TIMER_ALARM_GENERAL0)) {
            break;
          }
        }
      }
    }
    geometry_kernel_set_yield_cb(vgm_tick);  /* service audio during DSP waits */

    demos_register();
    demo_engine_start(0);

    setAlarm(TIMER_ALARM_GENERAL0, 1);
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

        while (!checkAlarm(TIMER_ALARM_GENERAL0)) {
            vgm_tick();  /* service VGM while waiting for next frame tick */
        }
        setAlarm(TIMER_ALARM_GENERAL0, 1);
    }

    if (g_vgm_open) {
        vgm_close(&g_vgm_player);
    }
    setAlarm(TIMER_ALARM_GENERAL0, 1);  /* one second exit wait */
    while(true) {
        if (checkAlarm(TIMER_ALARM_GENERAL0)) {
            break;
        }
    }
    return 0;
}
