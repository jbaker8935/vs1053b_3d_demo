#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
bool w, a, s, d, t, g;
bool rotateLeft;
bool rotateRight;
bool rotateUp;
bool rotateDown;
} InputHold;

typedef struct {
bool rotateLeft;
bool rotateRight;
bool rotateUp;
bool rotateDown;
bool firePrimary;
bool resetCam;
bool exit;
bool swapStereo; /* Toggle L/R channel output */
} InputEdge;

typedef struct {
InputHold hold;
InputEdge edge;
} InputState;

InputState *input_state_data(void);
void input_state_init(void);
void input_state_clear_edges(InputState *state);
void input_state_clear_hold(InputState *state);

extern bool swapStereo; /* global state for whether stereo channels are swapped */

#endif // INPUT_H
