#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "../include/3d_object.h"
#include "../include/input.h"

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

typedef enum {
STATE_DEMO
} GameMode;

typedef struct {
Camera camera;
uint8_t instance_count;
} Wireframe3D;

typedef struct {
GameMode mode;
uint32_t frameCounter;
Wireframe3D wireframe;
} GameContext;

GameContext *game_state_data(void);
void game_state_init(GameMode mode);
void game_state_update_3d(InputState *input);
void game_state_increment_frame(void);
void reset_camera(void);
int16_t game_state_clamp16(int16_t value, int16_t min, int16_t max);

extern GameMode gameMode; // defined in game_state.c

#endif // GAME_STATE_H
