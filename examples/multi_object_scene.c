#define F256LIB_IMPLEMENTATION
#include "f256lib.h"

#include "../include/geometry_kernel.h"
#include "../include/3d_object.h"
#include "../include/video.h"

static SceneObjectParams scene_objs[8] = {
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-400, .pos_y=0, .pos_z=400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-400, .pos_y= 0, .pos_z=-400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 400, .pos_y=0, .pos_z=-400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 400, .pos_y= 0, .pos_z=400 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-800, .pos_y=-0, .pos_z=800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x=-800, .pos_y= 0, .pos_z=-800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 800, .pos_y=0, .pos_z=-800 },
    { .slot=0, .yaw=0, .pitch=0, .roll=0, .scale=128, .pos_x= 800, .pos_y= 0, .pos_z=800 }
};

static uint8_t visible_layer = 1;
void app_init(void) {
    vs1053_clock_boost();
    vs1053_plugin_load();     
    vgk_plugin_init();
    vs1053_dac_mute();
    vs1053_dac_interrupt_disable(); // Decoder RAM is being used.  
    vgk_projection_params_init(120, 160, 120, -64);
    vgk_model_slot_init(&g_model_cube, 0);
    vgk_cam_params_set(0, 0, 0, 0, 200, 2400);
    vgk_hidden_line_enable();
}

void app_frame(uint16_t cam_x, int16_t cam_z, uint8_t cam_yaw) {
    uint8_t draw_layer = (visible_layer == 1) ? 2 : 1;
    dmaBitmapClear(draw_layer);
    vgk_cam_params_set(0, cam_yaw, 0, cam_x, 200, cam_z);
    vgk_reset();
    vgk_trigger();
    uint8_t status = vgk_wait_complete(10000);
    if (status == 1) {
        // retrieve and draw edges for the whole scene (all objects at once)
        vgk_scene_scrn_edges_get(8, scene_objs, 0x0B, 0x0D, draw_layer);
    } else if (status == 0){
        textPrint("Error: Geometry kernel timeout.\n");
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

    const int16_t radius = 1800;
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

    bool occlusion = true;
    video_init();
    app_init();
    textGotoXY(0, 0);
    textPrint("App Init Complete. \n");
    // use scene API for multi-object demo
    vgk_scene_enable();
    vgk_scene_set_descriptor(8, scene_objs);
    for (uint8_t loop = 0; loop < 4; loop++) {
        textGotoXY(0, 0);
        textPrint("Camera orbiting: Occlusion ");
        textPrint(occlusion ? "ON " : "OFF");

        if(!occlusion) {
            vgk_scene_no_occlusion_enable();
        } else {
            vgk_scene_no_occlusion_disable();
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


