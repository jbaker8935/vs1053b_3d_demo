#include "../include/input.h"
#include "../include/game_state.h"

#include <string.h>

static InputState g_input __attribute__((section(".bss")));

bool swapStereo = true;

InputState *input_state_data(void) {
return &g_input;
}

void input_state_init(void) {
memset(&g_input, 0, sizeof(g_input));
swapStereo = true;
}

void input_state_clear_hold(InputState *state) {
state->hold.w = false;
state->hold.a = false;
state->hold.s = false;
state->hold.d = false;
state->hold.t = false;
state->hold.g = false;
state->hold.rotateLeft = false;
state->hold.rotateRight = false;
state->hold.rotateUp = false;
state->hold.rotateDown = false;
}

void input_state_clear_edges(InputState *state) {
state->edge.rotateLeft = false;
state->edge.rotateRight = false;
state->edge.rotateUp = false;
state->edge.rotateDown = false;
state->edge.resetCam = false;
state->edge.exit = false;
state->edge.swapStereo = false;
}
