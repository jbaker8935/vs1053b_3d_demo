#define F256LIB_IMPLEMENTATION
#include "f256lib.h"

#include "../include/geometry_kernel.h"
#include "../include/3d_object.h"
#include "../include/video.h"
#include "../include/draw_line.h"

static uint8_t visible_layer = 1;
void app_init(void) {
    vs1053_clock_boost();
    vs1053_plugin_load();     
    vgk_plugin_init();
    vs1053_dac_mute();
    vs1053_dac_interrupt_disable(); // Decoder RAM is being used.  
    vgk_projection_params_init(120, 160, 120, -64);
    vgk_model_slot_init(&g_model_cube, 0);
    vgk_cam_params_set(0, 0, 0, 0, 200, 1400);
    vgk_hidden_line_enable();
    vgk_no_near_far_coloring = false;
}

void app_frame(uint8_t scale, uint8_t yaw, uint8_t pitch) {
    uint8_t draw_layer = (visible_layer == 1) ? 2 : 1;
    dmaBitmapClear(draw_layer);
    vgk_obj_params_set(pitch, yaw, 0, scale, 0, 0, -300);
    vgk_reset();
    vgk_trigger();
    uint8_t status = vgk_wait_complete(10000);
    if (status == 1) {
        vgk_scrn_edges_with_depth_get((Model3D *)&g_model_cube, draw_layer);
    } else if (status == 0){
        textPrint("Error: Geometry kernel timeout.\n");
    } 
    video_wait_vblank();
    bitmapSetVisible(draw_layer, true);
    bitmapSetVisible(visible_layer, false);
    visible_layer = draw_layer;
}


int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    uint8_t obj_yaw = 0;
    uint8_t obj_pitch = 0;

    video_init();
    app_init();
    textGotoXY(0, 0);
    textPrint("App Init Complete. \n");
    // make cube active object
    vgk_model_load(0);
    for (uint8_t loops = 0; loops < 4; loops++) {
        for(uint8_t frame=0; frame < 128; frame++) {
            app_frame(frame<64 ? 128 + frame : 192 - (frame - 64), obj_yaw++, obj_pitch++);
        }
    }
    textPrint("Done! Press any key to exit.\n");
    getchar();
    return 0;
}
