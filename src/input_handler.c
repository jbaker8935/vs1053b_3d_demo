#include "f256lib.h"
#include "../include/input.h"
#include "../include/input_handler.h"
#include "../include/game_state.h"

#include <stddef.h>

static void record_key_state(InputState *state, char key, bool pressed) {
switch (key) {
case 'w': case 'W': state->hold.w = pressed; break;
case 'a': case 'A': state->hold.a = pressed; break;
case 's': case 'S': state->hold.s = pressed; break;
case 'd': case 'D': state->hold.d = pressed; break;
case 't': case 'T': state->hold.t = pressed; break;
case 'g': case 'G': state->hold.g = pressed; break;
case 'q': case 'Q': state->hold.rotateLeft = pressed; break;
case 'e': case 'E': state->hold.rotateRight = pressed; break;
case 'r': case 'R':
state->hold.rotateUp = pressed;
if (pressed) state->edge.rotateUp = true;
break;
case 'f': case 'F':
state->hold.rotateDown = pressed;
if (pressed) state->edge.rotateDown = true;
break;
case 'x': case 'X': if (pressed) state->edge.exit = true; break;
case '-': case '_': if (pressed) state->edge.swapStereo = true; break;
case 'c': case 'C':
if (pressed) state->edge.resetCam = true;
break;
}
}

void input_handler_init(void) {
input_state_init();
}

void input_handler_poll(void) {
InputState *state = input_state_data();

while (kernelGetPending() > 0) {
kernelNextEvent();
size_t type = kernelEventData.type;

if (type == kernelEvent(key.PRESSED) || type == kernelEvent(key.RELEASED)) {
bool pressed = (type == kernelEvent(key.PRESSED));
record_key_state(state, kernelEventData.key.ascii, pressed);
}
}
}
