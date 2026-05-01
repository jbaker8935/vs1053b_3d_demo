#ifndef __OSCAR64__
#define F256LIB_IMPLEMENTATION
#endif
#include "f256lib.h"

#include "../include/geometry_kernel.h"
#include "../include/3d_object.h"
#include "../include/video.h"

static uint8_t visible_layer = 1;
void app_init(void) {
    textGotoXY(0, 0);
    vgk_plugin_init();
    vgk_projection_params(240, 160, 120, -128);
    vgk_model_save(&g_model_cube, 0);
    vgk_cam_params(0, 0, 0, 0, 200, 2400);
    vgk_hidden_line(true);
    vgk_edge_coloring(VGK_EC_NEAR_FAR);
}

void app_frame(uint8_t scale, uint8_t yaw, uint8_t pitch) {
    uint8_t draw_layer = (visible_layer == 1) ? 2 : 1;
    dmaBitmapClear(draw_layer);
    vgk_obj_params(pitch, yaw, 0, scale, 0, 0, -300);
    vgk_reset();
    vgk_trigger();
    uint8_t status = vgk_wait_complete(10000);
    if (status == 1) {
        vgk_scrn_edges_render(draw_layer, 0x0B);
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
    uint16_t version = 0;
    video_init();
    app_init();
    textGotoXY(0, 1);
    version = vgk_plugin_version();
    if (version == 0) {
        textPrint("Geometry plugin not detected.\n");
        return 1;
    }
    textPrint("Geometry Plugin Version: ");
    textPrintUInt(version/256);
    textPrint(".");
    textPrintUInt(version%256);
    textPrint("\n");
    textPrint("App Init Complete. \n");    
    // make cube active object
    vgk_model_select(0);
    for (uint8_t loops = 0; loops < 4; loops++) {
        for(uint8_t frame=0; frame < 128; frame++) {
            app_frame(frame<64 ? 128 + frame : 192 - (frame - 64), obj_yaw++, obj_pitch++);
        }
    }
    textPrint("Done! Press any key to exit.\n");
    getchar();
    return 0;
}
