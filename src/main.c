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

#define CODEC_HPO_ATTN_LEFT 0x00
#define CODEC_HPO_ATTN_RIGHT 0x01
#define CODEC_HPO_ATTN_MSTR 0x02
#define CODEC_DAC_ATTN_LEFT 0x03
#define CODEC_DAC_ATTN_RIGHT 0x04
#define CODEC_DAC_ATTN_MSTR 0x05
#define CODEC_DAC_PHASE_CTRL 0x06
#define CODEC_DAC_CHL_CTL 0x07
#define CODEC_DAC_MUTE 0x08
#define CODEC_DAC_DEEMPH_CTL 0x09
#define CODEC_DAC_INTF_CTL 0x0A
#define CODEC_ADC_INTF_CTL 0x0B
#define CODEC_MSTR_MODE 0x0C
#define CODEC_PWR_DOWN_CTL 0x0D
#define CODEC_ADC_ATTN_LEFT 0x0E
#define CODEC_ADC_ATTN_RIGHT 0x0F
#define CODEC_ALC_CTL_1 0x10
#define CODEC_ALC_CTL_2 0x11
#define CODEC_ALC_CTL_3 0x12
#define CODEC_ALC_NOISE_GATE 0x13
#define CODEC_LIMITER_CTL 0x14
#define CODEC_ADC_MUX 0x15
#define CODEC_OUTPUT_MUX 0x16
#define CODEC_RESET 0x17

#define CODEC_CMD_LOW 0xD620
#define CODEC_CMD_HIGH 0xD621
#define CODEC_CTL_STATUS 0xD622
#define CODEC_START 0x01
#define CODEC_BUSY 0x01
#define CODEC_AIN1 0x01  /* SID and SAM2695 MIDI */
#define CODEC_AIN2 0x02  /* Line-in, OPL3 and MIDI Wave Table*/
#define CODEC_AIN3 0x04  /* PWM */
#define CODEC_AIN4 0x08  /* VS1053b */
#define CODEC_AIN5 0x10  /* Line-in onboard header */

#define CODEC_DAC_ATTN_6DB (0x01FF - 2 * 6)  /* .5db per step */
#define CODEC_DAC_ATTN_9DB (0x01FF - 2 * 9)
#define CODEC_HPO_ATTN_0DB 0x0179
#define CODEC_HPO_ATTN_6DB (0x0179 - 6)
#define CODEC_HPO_ATTN_9DB (0x0179 - 9)
#define CODEC_HPO_ATTN_12DB (0x0179 - 12)

#define CODEC_DAC_CHL_CTL_STEREO 0x90
#define CODEC_DAC_CHL_CTL_MONO 0xF0

void codec_write(uint16_t reg, uint16_t val) {
    uint16_t data = (reg << 9) | (val & 0x01FF);
    POKE(CODEC_CMD_LOW, (uint8_t)(data & 0xFF));
    POKE(CODEC_CMD_HIGH, (uint8_t)((data >> 8) & 0xFF));
    POKE(CODEC_CTL_STATUS, CODEC_START);
    while (PEEK(CODEC_CTL_STATUS) & CODEC_BUSY)
        ;
}

/*  Codec Setup */
void init_codec() {
    codec_write(CODEC_ADC_MUX, CODEC_AIN2);
    // Stereo output
    codec_write(CODEC_DAC_CHL_CTL, CODEC_DAC_CHL_CTL_STEREO);
    // Attenuate DAC by 9 dB for headroom ;
    codec_write(CODEC_DAC_ATTN_MSTR, CODEC_DAC_ATTN_9DB);
    // Attenuate headphone output by 9 dB
    codec_write(CODEC_HPO_ATTN_MSTR, CODEC_HPO_ATTN_9DB);
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
    // if (!vgm_himem_is_playable(&g_vgm_himem)) return;
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
