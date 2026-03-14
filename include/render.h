#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include "../include/game_state.h"

void render_frame(GameContext *ctx);
void render_scene_aabb_overlay(uint8_t draw_layer);

#endif // RENDER_H
