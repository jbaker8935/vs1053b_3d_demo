#if !defined(SCENE_H)
#define SCENE_H
#include <stdint.h>

// the scene object parameters structure is defined in geometry_kernel.h
#include "geometry_kernel.h"

#define SCENE_COUNT 8

// array used by the sample scene implementation.  callers may modify it
// before invoking `scene_get_screen_edges`.
extern SceneObjectParams g_scene_objects[SCENE_COUNT];

void resetScene(void);
void loadScene(void);
#endif // SCENE_H 

