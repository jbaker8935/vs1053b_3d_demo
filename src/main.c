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

extern void loadVS1053Plugin(void);
extern void boostVSClock(void);
extern void initialize_plugin(void);

static void init_models(void) {
    load_model_to_plugin(&g_model_cube, 0);
    load_model_to_plugin(&g_model_anaconda, 1);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

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
            // wait for next frame tick
        }
        setAlarm(TIMER_ALARM_GENERAL0, 1);
    }
    return 0;
}
