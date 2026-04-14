#include "f256lib.h"
#include "../include/game_state.h"
#include "../include/3d_object.h"
#include "../include/input.h"
#include "../include/geometry_kernel.h"

#include <string.h>

extern void vgk_cam_params_set(uint8_t pitch, uint8_t yaw, uint8_t roll,
                                 int16_t pos_x, int16_t pos_y, int16_t pos_z);

extern int32_t mathSignedMultiply(int16_t a, int16_t b);

static GameContext g_ctx __attribute__((section(".bss")));

GameMode gameMode = STATE_DEMO;

void reset_camera(void) {
    vec3_t cam_pos = {0, 200, 2400};
    camera_init(&g_ctx.wireframe.camera, cam_pos);
    Camera *camera = &g_ctx.wireframe.camera;
    vgk_cam_params_set(
        camera->pitch, camera->yaw, camera->roll,
        camera->position.x, camera->position.y, camera->position.z);
    camera->moved = true;
}

GameContext *game_state_data(void) {
return &g_ctx;
}

void game_state_init(GameMode mode) {
memset(&g_ctx, 0, sizeof(g_ctx));
g_ctx.mode = mode;
reset_camera();
}

void game_state_increment_frame(void) {
g_ctx.frameCounter++;
}

int16_t game_state_clamp16(int16_t value, int16_t min, int16_t max) {
if (value < min) return min;
if (value > max) return max;
return value;
}

void game_state_camera_basis_get(int16_t *fwd_x, int16_t *fwd_y,
int16_t *fwd_z, int16_t *right_x, int16_t *right_z) {
uint8_t yaw = g_ctx.wireframe.camera.yaw;
uint8_t pitch = g_ctx.wireframe.camera.pitch;
int16_t cy = sin_table[(uint8_t)(yaw + 64) & 0xFF];
int16_t sy = sin_table[yaw];
int16_t sp = sin_table[pitch];
int16_t cp = sin_table[(uint8_t)(pitch + 64) & 0xFF];

if (fwd_x != NULL) {
int32_t tmp_fx = mathSignedMultiply(sy, cp);
*fwd_x = (int16_t)(-(tmp_fx >> 14));
}
if (fwd_y != NULL) {
*fwd_y = sp;
}
if (fwd_z != NULL) {
int32_t tmp_fz = mathSignedMultiply(cy, cp);
*fwd_z = (int16_t)(-(tmp_fz >> 14));
}
if (right_x != NULL) {
*right_x = cy;
}
if (right_z != NULL) {
*right_z = (int16_t)-sy;
}
}

void game_state_update_3d(InputState *input) {
// increment camera position based on orientation
int16_t speed = 35;
int16_t fwd_x;
int16_t fwd_y;
int16_t fwd_z;
int16_t right_x;
int16_t right_z;

game_state_camera_basis_get(&fwd_x, &fwd_y, &fwd_z, &right_x, &right_z);

if (input->hold.w) {
int16_t dx = (int16_t)(mathSignedMultiply(fwd_x, speed) >> 14);
int16_t dz = (int16_t)(mathSignedMultiply(fwd_z, speed) >> 14);
int16_t dy = (int16_t)(mathSignedMultiply(fwd_y, speed) >> 14);
g_ctx.wireframe.camera.position.x = game_state_clamp16(g_ctx.wireframe.camera.position.x + dx, -8192, 8192);
g_ctx.wireframe.camera.position.z = game_state_clamp16(g_ctx.wireframe.camera.position.z + dz, -8192, 8192);
g_ctx.wireframe.camera.position.y = game_state_clamp16(g_ctx.wireframe.camera.position.y + dy, -8192, 8192);
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.s) {
int16_t dx = (int16_t)(mathSignedMultiply(fwd_x, speed) >> 14);
int16_t dz = (int16_t)(mathSignedMultiply(fwd_z, speed) >> 14);
int16_t dy = (int16_t)(mathSignedMultiply(fwd_y, speed) >> 14);
g_ctx.wireframe.camera.position.x = game_state_clamp16(g_ctx.wireframe.camera.position.x - dx, -8192, 8192);
g_ctx.wireframe.camera.position.z = game_state_clamp16(g_ctx.wireframe.camera.position.z - dz, -8192, 8192);
g_ctx.wireframe.camera.position.y = game_state_clamp16(g_ctx.wireframe.camera.position.y - dy, -8192, 8192);
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.a) {
int16_t dx = (int16_t)(mathSignedMultiply(right_x, speed) >> 14);
int16_t dz = (int16_t)(mathSignedMultiply(right_z, speed) >> 14);
g_ctx.wireframe.camera.position.x = game_state_clamp16(g_ctx.wireframe.camera.position.x - dx, -8192, 8192);
g_ctx.wireframe.camera.position.z = game_state_clamp16(g_ctx.wireframe.camera.position.z - dz, -8192, 8192);
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.d) {
int16_t dx = (int16_t)(mathSignedMultiply(right_x, speed) >> 14);
int16_t dz = (int16_t)(mathSignedMultiply(right_z, speed) >> 14);
g_ctx.wireframe.camera.position.x = game_state_clamp16(g_ctx.wireframe.camera.position.x + dx, -8192, 8192);
g_ctx.wireframe.camera.position.z = game_state_clamp16(g_ctx.wireframe.camera.position.z + dz, -8192, 8192);
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.t) {
g_ctx.wireframe.camera.position.y = game_state_clamp16(g_ctx.wireframe.camera.position.y + speed, -8192, 8192);
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.g) {
g_ctx.wireframe.camera.position.y = game_state_clamp16(g_ctx.wireframe.camera.position.y - speed, -8192, 8192);
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.rotateLeft) {
g_ctx.wireframe.camera.yaw += 2;
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.rotateRight) {
g_ctx.wireframe.camera.yaw -= 2;
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.rotateUp || input->edge.rotateUp) {
g_ctx.wireframe.camera.pitch += 2;
g_ctx.wireframe.camera.moved = true;
}
if (input->hold.rotateDown || input->edge.rotateDown) {
g_ctx.wireframe.camera.pitch -= 2;
g_ctx.wireframe.camera.moved = true;
}
}
