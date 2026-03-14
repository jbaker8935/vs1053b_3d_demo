#include "f256lib.h"
#include "../include/scene.h"
#include "../include/geometry_kernel.h"

SceneObjectParams g_scene_objects[SCENE_COUNT] = {
    {.slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x = -400, .pos_y = 0, .pos_z = -400},
    { .slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x = -400, .pos_y = 0, .pos_z = 400},
    { .slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x =  400, .pos_y = 0, .pos_z = -400},
    { .slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x =  400, .pos_y = 0, .pos_z = 400},
    {.slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x = -800, .pos_y = 0, .pos_z = -800},
    { .slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x = -800, .pos_y = 0, .pos_z = 800},
    { .slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x =  800, .pos_y = 0, .pos_z = -800},
    { .slot = 1, .pitch = 0, .yaw = 0, .roll = 0, .scale = 128, .pos_x =  800, .pos_y = 0, .pos_z = 800}    
};

void resetScene() {
    // Reset scene-specific data here
}

void loadScene() {
    scene_set_descriptor(SCENE_COUNT, g_scene_objects);

}

