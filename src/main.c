#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
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
#include "../include/fat32_stream.h"
#include "../include/vgm.h"

extern void loadVS1053Plugin(void);
extern void boostVSClock(void);
extern void initialize_plugin(void);

/* -----------------------------------------------------------------------
 * VGM playback state
 * ----------------------------------------------------------------------- */
#define VGM_DEFAULT_PATH  "media/vgm/music.vgm"

static vgm_player_t  g_vgm_player;
static fat32_file_t  g_vgm_file;
static bool          g_vgm_open  = false;
static const char   *g_vgm_path  = NULL;

static uint16_t vgm_read_cb(void *ctx, uint8_t *buf, uint16_t len) {
    return (uint16_t)fat32_read((fat32_file_t *)ctx, buf, len);
}
static void vgm_seek_cb(void *ctx, uint32_t offset) {
    fat32_seek((fat32_file_t *)ctx, offset);
}

/* Open (or re-open for looping) the player.  File handle already open. */
static void vgm_start(void) {
    fat32_seek(&g_vgm_file, 0);
    g_vgm_open = (vgm_open(&g_vgm_player, vgm_read_cb, vgm_seek_cb,
                           (void *)&g_vgm_file) == VGM_PLAYING);
}

/* Initialise SD card, open the file, and start playback.
 * Silently returns on any failure so the demo still runs without audio. */
static void vgm_init(const char *path) {
    if (!fat32_init())              return;
    if (!fat32_open(&g_vgm_file, path)) return;
    vgm_start();
}

/* Service one VGM step; loop automatically on end-of-stream. */
static void vgm_tick(void) {
    if (!g_vgm_open) return;
    vgm_status_t s = vgm_service(&g_vgm_player);
    if (s == VGM_DONE) {
        vgm_close(&g_vgm_player);  /* silence chip, restore fixed-rate T0 */
        vgm_start();               /* seek to 0, re-open for looping       */
    } else if (s == VGM_ERROR) {
        vgm_close(&g_vgm_player);
        fat32_close(&g_vgm_file);
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
                 ? argv[1] : VGM_DEFAULT_PATH;

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

    vgm_init(g_vgm_path);  /* start VGM after VS1053B is fully set up */
    geometry_kernel_set_yield_cb(vgm_tick);  /* service audio during DSP waits */

    demos_register();
    demo_engine_start(0);

    setAlarm(TIMER_ALARM_GENERAL0, 1);
    while (true) {
        input_handler_poll();
        InputState *input = input_state_data();
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
        fat32_close(&g_vgm_file);
    }
    setAlarm(TIMER_ALARM_GENERAL0, 1);  /* one second exit wait */
    while(true) {
        if (checkAlarm(TIMER_ALARM_GENERAL0)) {
            break;
        }
    }
    return 0;
}
