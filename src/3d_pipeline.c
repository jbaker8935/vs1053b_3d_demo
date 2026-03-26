#include "f256lib.h"
#include "../include/3d_pipeline.h"
#include "../include/geometry_kernel.h"

// transforms instance vertices/edges to screen space
// output is written to global line list buffer
uint8_t transform_instance_to_screen(const Instance3D *instance, const Camera *camera) {

    vgk_obj_params_set(
        instance->pitch, instance->yaw, instance->roll, instance->scale,
        instance->position.x, instance->position.y, instance->position.z);

    vgk_trigger();
    
    uint8_t wait_result = vgk_wait_complete(10000);
    return wait_result;
}
