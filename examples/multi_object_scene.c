#define F256LIB_IMPLEMENTATION
#include "f256lib.h"

#include "../include/geometry_kernel.h"
#include "../include/3d_object.h"
#include "../include/video.h"

static SceneObjectParams scene_objs[16] = {
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-400, .pos_y=0, .pos_z=400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-400, .pos_y= 0, .pos_z=-400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 400, .pos_y=0, .pos_z=-400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 400, .pos_y= 0, .pos_z=400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-800, .pos_y=-0, .pos_z=800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-800, .pos_y= 0, .pos_z=-800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 800, .pos_y=0, .pos_z=-800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 800, .pos_y= 0, .pos_z=800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-1200, .pos_y=0, .pos_z=1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-1200, .pos_y= 0, .pos_z=-1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 1200, .pos_y=0, .pos_z=-1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 1200, .pos_y= 0, .pos_z=1200 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-1600, .pos_y=-0, .pos_z=1600 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-1600, .pos_y= 0, .pos_z=-1600 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 1600, .pos_y=0, .pos_z=-1600 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 1600, .pos_y= 0, .pos_z=1600 }    
};

static uint8_t visible_layer = 1;

void app_init(void) {

    vgk_plugin_init();
    vgk_projection_params(240, 160, 120, -128);
    vgk_model_save(&g_model_cube, 0);
    vgk_cam_params(0, 0, 0, 0, 200, 2400);
    vgk_hidden_line(true);
}

void app_frame(uint16_t cam_x, int16_t cam_z, uint8_t cam_yaw) {
    uint8_t draw_layer = (visible_layer == 1) ? 2 : 1;
    dmaBitmapClear(draw_layer);
    vgk_cam_params(0, cam_yaw, 0, cam_x, 200, cam_z);
    vgk_reset();
    vgk_trigger();
    uint8_t status = vgk_wait_complete(10000);
    if (status == 1) {
        // retrieve and draw edges for the whole scene (all objects at once)
        vgk_scrn_edges_render(draw_layer, 0x0B);

    } else if (status == 0){
        textPrint("Error: Geometry kernel timeout.\n");
    } else {
        textPrint("Error: Geometry kernel error during processing.: ");
        textPrintUInt(status);
        textPrint("\n");
    }
    video_wait_vblank();
    bitmapSetVisible(draw_layer, true);
    bitmapSetVisible(visible_layer, false);
    visible_layer = draw_layer;
}

static uint8_t camera_orbit_angle;
static uint16_t camera_x;
static uint16_t camera_z;
static uint8_t camera_yaw;

static void camera_orbit(void) {

    const int16_t radius = 4000;
    const int16_t center_x = 0;
    const int16_t center_z = 0;

    camera_orbit_angle = (uint8_t)(camera_orbit_angle + 1u);
    int16_t sinv = sin_table[camera_orbit_angle];
    int16_t cosv = sin_table[(uint8_t)(camera_orbit_angle + 64u)];

    // Q14 multiply: (sinv * radius) >> 14
    int16_t dx = (int16_t)((mathSignedMultiply(cosv,radius)) >> 14);
    int16_t dz = (int16_t)((mathSignedMultiply(sinv,radius)) >> 14);

    camera_x = (int16_t)(center_x + dx);
    camera_z = (int16_t)(center_z + dz);

    // Face toward center: yaw = 64 - orbit_angle (sin/yaw mapping for this math setup)
    camera_yaw = (uint8_t)(64u - camera_orbit_angle);

}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    uint16_t version=0;
    bool occlusion = true;
    video_init();
    textGotoXY(0, 0);textPrint("Initializing...\n");
    app_init();

    textGotoXY(0, 1);
    version = vgk_plugin_version();
    if(version == 0) {
        uint16_t probe = vs1053_mem_read(VGK_PLUGIN_SIGNATURE);
        textPrint("Geometry plugin not detected.\n");
        textPrint("Probe read: ");
        textPrintUInt(probe);
        textPrint("\nPress any key to exit.\n");
        getchar();
        return 1;
    }
    textPrint("Geometry Plugin Version: ");
    textPrintUInt(version/256);
    textPrint(".");
    textPrintUInt(version%256);
    textPrint("\n");
    textPrint("App Init Complete. \n");    
    // use scene API for multi-object demo
    vgk_scene_mode(true);
    vgk_scene_objects(16, scene_objs);
    for (uint8_t loop = 0; loop < 4; loop++) {
        textGotoXY(0, 0);
        textPrint("Camera orbiting: Occlusion ");
        textPrint(occlusion ? "ON " : "OFF");

        if(!occlusion) {
            vgk_scene_occlusion(false);
        } else {
            vgk_scene_occlusion(true);
        }
        for(uint16_t frame=0; frame < 256; frame++) {
            if(frame % 2 == 0) {
                camera_orbit(); // updates camera_x, camera_z, camera_yaw
            }
            app_frame(camera_x, camera_z, camera_yaw);
        }
        occlusion = !occlusion;
    }
    textPrint("\nDone! Press any key to exit.\n");
    getchar();
    return 0;
}


