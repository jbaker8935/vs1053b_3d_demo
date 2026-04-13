#include "f256lib.h"
#include "../include/geometry_kernel.h"

#include <stdint.h>

#include "../include/3d_object.h"
#include "../include/draw_line.h"

const int16_t sin_table[256] = {
    0,      402,    804,    1205,   1606,   2006,   2404,   2801,   3196,   3590,   3981,   4370,   4756,   5139,
    5520,   5897,   6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,   9102,   9434,   9760,   10080,
    10394,  10702,  11003,  11297,  11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,  13623,  13842,
    14053,  14256,  14449,  14635,  14811,  14978,  15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,
    16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,  16384,  16379,  16364,  16340,  16305,  16261,
    16207,  16143,  16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,  15137,  14978,  14811,  14635,
    14449,  14256,  14053,  13842,  13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,  11585,  11297,
    11003,  10702,  10394,  10080,  9760,   9434,   9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
    6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,   3196,   2801,   2404,   2006,   1606,   1205,
    804,    402,    0,      -402,   -804,   -1205,  -1606,  -2006,  -2404,  -2801,  -3196,  -3590,  -3981,  -4370,
    -4756,  -5139,  -5520,  -5897,  -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,  -9102,  -9434,
    -9760,  -10080, -10394, -10702, -11003, -11297, -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978, -15137, -15286, -15426, -15557, -15679, -15791,
    -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379, -16384, -16379, -16364, -16340,
    -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286, -15137, -14978,
    -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080, -9760,  -9434,  -9102,  -8765,  -8423,  -8076,  -7723,  -7366,
    -7005,  -6639,  -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,  -3196,  -2801,  -2404,  -2006,
    -1606,  -1205,  -804,   -402,
    
};

Model3D * slot_model[VGK_SAVE_SLOT_COUNT] = {NULL};
static bool vgk_near_far_coloring = false;
static bool vgk_scene_mode_active = false;

void vgk_slot_model_set(const Model3D *model, uint8_t slot) {
    if(slot >= VGK_SAVE_SLOT_COUNT) {
        return;  // Invalid slot; do nothing
    }
    slot_model[slot] = (Model3D *)model;
}

void vgk_plugin_init(void) {
    vs1053_mem_write(VGK_STATUS, 0x0000);  // Clear status
    vs1053_sci_write(SCI_AIADDR, 0x0050);
}

// Setup object transformation parameters
void vgk_obj_params_set(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale, int16_t pos_x, int16_t pos_y,
                         int16_t pos_z) {
    vs1053_mem_write(VGK_OBJ_PITCH_YAW, (uint16_t)pitch << 8 | yaw);  // sets WRAMADDR, auto increments
    vs1053_sci_write(SCI_WRAM, ((uint16_t)roll << 8) | scale);
    vs1053_sci_write(SCI_WRAM, pos_x);
    vs1053_sci_write(SCI_WRAM, pos_y);
    vs1053_sci_write(SCI_WRAM, pos_z);
}

// Setup object angle and scale parameters only
void vgk_obj_angle_scale_set(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale) {
    vs1053_mem_write(VGK_OBJ_PITCH_YAW, (uint16_t)pitch << 8 | yaw);  // sets WRAMADDR, auto increments
    vs1053_sci_write(SCI_WRAM, ((uint16_t)roll << 8) | scale);
}

// Setup object position only parameters
void setup_object_pos(int16_t pos_x, int16_t pos_y, int16_t pos_z) {
    vs1053_mem_write(VGK_OBJ_POS_X, pos_x);
    vs1053_sci_write(SCI_WRAM, pos_y);
    vs1053_sci_write(SCI_WRAM, pos_z);
}

// Setup camera transformation parameters
void vgk_cam_params_set(uint8_t pitch, uint8_t yaw, uint8_t roll, int16_t pos_x, int16_t pos_y, int16_t pos_z) {
    vs1053_mem_write(VGK_CAM_PITCH_YAW, (uint16_t)pitch << 8 | yaw);
    vs1053_sci_write(SCI_WRAM, ((uint16_t)roll << 8) |
                                   0x80);  // scale not used for camera, but set scale to 1.0 (Q7) for consistency
    vs1053_sci_write(SCI_WRAM, pos_x);
    vs1053_sci_write(SCI_WRAM, pos_y);
    vs1053_sci_write(SCI_WRAM, pos_z);
}

// Setup camera transformation parameters
void vgk_projection_params_init(int16_t focal, int16_t half_w, int16_t half_h, int16_t near_z) {
    vs1053_mem_write(VGK_PROJ_FOCAL, focal);
    vs1053_sci_write(SCI_WRAM, half_w);
    vs1053_sci_write(SCI_WRAM, half_h);
    vs1053_sci_write(SCI_WRAM, near_z);
    vgk_projection_enable();
}

void vgk_projection_enable(void) {
    vs1053_mem_write(VGK_ENABLE_PROJECT, 0x0001);  // Enable projection
    vs1053_sci_write(SCI_WRAM, 0x0001);                // Enable clipping
}

void vgk_projection_disable(void) {
    vs1053_mem_write(VGK_ENABLE_PROJECT, 0x0000);  // Disable projection (passthrough)
    vs1053_sci_write(SCI_WRAM, 0x0000);                // Disable clipping
}

void vgk_model_vertices_init(const Model3D *model, uint8_t slot) {
    uint16_t base = 0;
    if (slot < 4) {
        base = VGK_SAVE_AREA_X + (uint16_t)slot * VGK_SAVE_SLOT_SIZE;
    } else if (slot < 6) {
        base = VGK_SAVE_AREA_B + (uint16_t)(slot - 4) * VGK_SAVE_SLOT_SIZE;
    } else {
        return;  // Invalid slot; do nothing
    }
    vs1053_mem_write(base + VGK_SLOT_N_VERTICES, model->vertex_count);
    vs1053_sci_write(SCI_WRAMADDR, base + VGK_SLOT_INPUT_VERT);
    for (uint8_t i = 0; i < model->vertex_count; ++i) {
        vs1053_sci_write(SCI_WRAM, model->vx[i]);
        vs1053_sci_write(SCI_WRAM, model->vy[i]);
        vs1053_sci_write(SCI_WRAM, model->vz[i]);
    }
}

static void vgk_descriptor_enable(bool enabled) {
    vs1053_mem_write(VGK_ENABLE_DESCRIPTOR, enabled ? 0x0001 : 0x0000);
}

void vgk_near_far_coloring_enable(bool enabled) {
    vgk_near_far_coloring = enabled;
    if (enabled) {
        vgk_descriptor_enable(true);
    } else {
        // don't disable descriptor if scene mode is active.
        if (!vgk_scene_mode_active) {
            vgk_descriptor_enable(false);
        }
    }
}

void vgk_hidden_line_disable(void) {
    vs1053_mem_write(VGK_ENABLE_HIDDEN_LINE, 0x0000);
}

void vgk_hidden_line_enable(void) {
    vs1053_mem_write(VGK_ENABLE_HIDDEN_LINE, 0x0001);
}

void vgk_model_hidden_line_init(const Model3D *model, uint8_t slot) {
    uint16_t base = 0;
    if (slot < 4) {
        base = VGK_SAVE_AREA_X + (uint16_t) slot * VGK_SAVE_SLOT_SIZE;
    } else if (slot < 6) {
        base = VGK_SAVE_AREA_B + (uint16_t)(slot - 4) * VGK_SAVE_SLOT_SIZE;
    } else {
        return;  // Invalid slot; do nothing
    }
    
    uint8_t face_count = model->face_count;

    if (face_count > VGK_MAX_FACES) {
        face_count = VGK_MAX_FACES;
    }

    bool has_hidden_data =
        (face_count > 0) &&
        (model->face_nx != 0) && (model->face_ny != 0) && (model->face_nz != 0) &&
        (model->edge_face0 != 0) && (model->edge_face1 != 0);

    if (!has_hidden_data) {
        // clear face count.
        vs1053_mem_write(base + VGK_SLOT_N_FACES, 0x0000);
        return;
    }

    uint8_t edge_count = model->edge_count;
    if (edge_count > VGK_MAX_INPUT_EDGES) {
        edge_count = VGK_MAX_INPUT_EDGES;
    }

    vs1053_sci_write(SCI_WRAMADDR, base + VGK_SLOT_EDGE_FACE_MAP);
    for (uint8_t i = 0; i < edge_count; ++i) {
        uint8_t face0 = model->edge_face0[i];
        uint8_t face1 = model->edge_face1[i];
        vs1053_sci_write(SCI_WRAM, ((uint16_t)face1 << 8) | (uint16_t)face0);
    }

    vs1053_sci_write(SCI_WRAMADDR, base + VGK_SLOT_FACE_NORMALS);
    for (uint8_t i = 0; i < face_count; ++i) {
        vs1053_sci_write(SCI_WRAM, (uint16_t)model->face_nx[i]);
        vs1053_sci_write(SCI_WRAM, (uint16_t)model->face_ny[i]);
        vs1053_sci_write(SCI_WRAM, (uint16_t)model->face_nz[i]);
    }

    // Store face count 
    vs1053_mem_write(base + VGK_SLOT_N_FACES, face_count);   
}

void vgk_model_slot_init(const Model3D *model, uint8_t slot) {
    // Write geometry directly to the save slot
    // The kernel will copy from this slot to the
    // active working area when the object is first used in a scene.
    vgk_slot_model_set(model, slot);  
    vgk_model_vertices_init(model, slot);
    vgk_model_edges_init(model, slot);
    vgk_model_hidden_line_init(model, slot);
}

void vgk_model_edges_init(const Model3D *model, uint8_t slot) {
    uint16_t base = 0;
    if (slot < 4) {
        base = VGK_SAVE_AREA_X + (uint16_t)slot * VGK_SAVE_SLOT_SIZE;
    } else if (slot < 6) {
        base = VGK_SAVE_AREA_B + (uint16_t)(slot - 4) * VGK_SAVE_SLOT_SIZE;
    } else {
        return;  // Invalid slot; do nothing
    }
    vs1053_mem_write(base + VGK_SLOT_N_EDGES, model->edge_count);
    vs1053_sci_write(SCI_WRAMADDR, base + VGK_SLOT_EDGE_LIST);
    for (uint8_t i = 0; i < model->edge_count; ++i) {
        vs1053_sci_write(SCI_WRAM, (uint16_t)model->edge_b[i] << 8 | (uint16_t)model->edge_a[i]);
    }
}

/* Yield callback called on each wait-loop iteration.  NULL = nop-delay only. */
static void (*g_yield_cb)(void) = NULL;

void vgk_yield_cb_set(void (*cb)(void)) {
    g_yield_cb = cb;
}

/* Call the registered yield callback once.  */
void vgk_yield(void) {
    if (g_yield_cb) { g_yield_cb(); }
}

void vgk_reset(void) {
    vs1053_mem_write(VGK_STATUS, 0x0000);
}

// Call the plugin entry point
void vgk_trigger(void) {
    vs1053_sci_write(SCI_AICTRL0, VGK_TRIGGER_MAGIC);  // Set trigger to start processing
}

// load Object from internal slot
__attribute__((noinline))
bool vgk_model_load(uint16_t slot) {
    vs1053_sci_write(SCI_AICTRL2, slot);            // Set trigger to start processing
    if (vgk_wait_complete(1000) == 1) {  // wait for completion (or error)
        return true;
    }
    return false;
}

uint8_t vgk_status(void) {
    volatile uint16_t status = vs1053_mem_read(VGK_STATUS);

    if (status == VGK_STATUS_DONE) {
        return 2;  // Complete
    } else if (status == VGK_STATUS_BUSY) {
        return 1;  // Busy
    } else if (status == VGK_STATUS_SAVE_ERROR || status == VGK_STATUS_LOAD_ERROR) {
        return 3;  // Error
    } else {
        return 0;  // Idle
    }
}

uint8_t vgk_wait_complete(uint16_t timeout_ms) {
    uint16_t elapsed = 0;
    volatile uint16_t raw_status = 0;
    while (elapsed < timeout_ms) {
        raw_status = vs1053_mem_read(VGK_STATUS);
        // textPrint("Status: ");
        // textPrintUInt(raw_status);
        // textPrint("\n");

        if (raw_status == VGK_STATUS_DONE) {
            return 1;  // Complete
        } else if (raw_status == VGK_STATUS_SAVE_ERROR || raw_status == VGK_STATUS_LOAD_ERROR) {
            return 2;  // Error
        }

        /* Yield to registered callback (e.g. audio tick) or fall back to
         * a short nop delay to avoid hammering the SCI interface. */
        if (g_yield_cb) {
            g_yield_cb();
        } else {
            for ( uint8_t delay = 0; delay < 25; delay++) {
                __asm__("nop");
            }
        }
        elapsed++;
    }
    return 0;  // Timeout
}


// -----------------------------------------------------------------------------
// State capture helper
// -----------------------------------------------------------------------------
void vgk_plugin_capture_state(PluginCapture *cap) {
    // camera inputs
    cap->cam_pitch_yaw = vs1053_mem_read(VGK_CAM_PITCH_YAW);
    cap->cam_roll_scale = vs1053_mem_read(VGK_CAM_ROLL_SCALE);
    cap->cam_pos_x = (int16_t)vs1053_mem_read(VGK_CAM_POS_X);
    cap->cam_pos_y = (int16_t)vs1053_mem_read(VGK_CAM_POS_Y);
    cap->cam_pos_z = (int16_t)vs1053_mem_read(VGK_CAM_POS_Z);
    cap->cam_sx = (int16_t)vs1053_mem_read(VGK_CAM_SX);
    cap->cam_cx = (int16_t)vs1053_mem_read(VGK_CAM_CX);
    cap->cam_sy = (int16_t)vs1053_mem_read(VGK_CAM_SY);
    cap->cam_cy = (int16_t)vs1053_mem_read(VGK_CAM_CY);
    cap->cam_sz = (int16_t)vs1053_mem_read(VGK_CAM_SZ);
    cap->cam_cz = (int16_t)vs1053_mem_read(VGK_CAM_CZ);

    // matrix (Y-RAM contiguous)
    uint16_t addr = VGK_MATRIX_BASE;
    for (uint8_t i = 0; i < 12; ++i) {
        cap->matrix[i] = (int16_t)vs1053_mem_read(addr++);
    }

    // projection params
    cap->proj_focal = vs1053_mem_read(VGK_PROJ_FOCAL);
    cap->proj_half_w = vs1053_mem_read(VGK_PROJ_HALF_W);
    cap->proj_half_h = vs1053_mem_read(VGK_PROJ_HALF_H);
    cap->proj_near_z = (int16_t)vs1053_mem_read(VGK_PROJ_NEAR_Z);

    // counts / status
    cap->n_vertices = vs1053_mem_read(VGK_N_VERTICES);
    cap->n_edges = vs1053_mem_read(VGK_N_EDGES);
    cap->n_output_edges = vs1053_mem_read(VGK_N_STREAM_EDGES);
    cap->status = vs1053_mem_read(VGK_STATUS);

}

void vgk_line_draw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color, uint8_t layer) {
    POKE(DL_COLOR, color);
    POKE(DL_MODE, 0x01); 
    POKE(DL_CONTROL, (layer << 2) | 0x01); // enable + no clock        
    POKEW(DL_X0, x0);
    POKEW(DL_X1, x1);
    POKEW(DL_Y,  y1<<8 | y0);
    POKE(DL_CONTROL, (layer << 2) | 0x03); // enable + clock
    while (PEEK(DL_FIFO_LO) | PEEK(DL_FIFO_HI)) {
        // wait for FIFO to empty before writing next edge
    }
    POKE(DL_CONTROL, 0x00);
    POKE(DL_MODE, 0x00);
}

/* vgk_scrn_edges_get: Uses new stream edge reads
 * 
 */
__attribute__((noinline))
uint8_t vgk_scrn_edges_get(uint8_t layer, uint8_t default_color) {

    bool has_descriptors = (vs1053_mem_read(VGK_ENABLE_DESCRIPTOR) != 0);
    
    POKE(DL_COLOR, default_color);
    POKE(DL_MODE, 0x01); 
    // Read number of output edges
    uint16_t edge_count = vs1053_mem_read(VGK_N_STREAM_EDGES);
    
    // textPrint("Edges: ");
    // textPrintUInt(edge_count);
    // textPrint("\n");
    for (uint16_t e = 0; e < edge_count; ++e) {
        POKE(DL_CONTROL, (layer << 2) | 0x01); // enable + no clock        
        // unused for now.
        if(has_descriptors) {
            uint16_t desc = vs1053_sci_read(SCI_WRAM); //(bit15=near, bit14=scene temporary, bits8-13=slot, bits0-7=edge_idx)
            uint8_t slot= (desc & 0x3F00) >> 8;
            uint8_t edge_idx = desc & 0x00FF;
            if(slot_model[slot]->edge_color_count > 0) {
                uint16_t edge_color = slot_model[slot]->edge_color[edge_idx];             
                POKE(DL_COLOR, desc & 0x8000 ? edge_color&0xFF : edge_color>>8); // near = low byte, far = high byte
            } else {
                if(vgk_near_far_coloring) {
                    uint16_t object_color = slot_model[slot]->object_color;
                    POKE(DL_COLOR, desc & 0x8000 ? object_color&0xFF : object_color>>8); // near = low byte, far = high byte
                }
            }
        }
        uint16_t x0 = vs1053_sci_read(SCI_WRAM);
        uint16_t x1 = vs1053_sci_read(SCI_WRAM);
        uint16_t y = vs1053_sci_read(SCI_WRAM);
        POKEW(DL_X0, x0);
        POKEW(DL_X1, x1);
        POKEW(DL_Y,  y);
        POKE(DL_CONTROL, (layer << 2) | 0x03); // enable + clock
        while (PEEK(DL_FIFO_LO) | PEEK(DL_FIFO_HI)) {
            // wait for FIFO to empty before writing next edge
        }

    }
    POKE(DL_CONTROL, 0x00);
    POKE(DL_MODE, 0x00);
 
    return edge_count;
}

#if 1  // enabled

// =============================================================================
// Scene API implementation
// =============================================================================
// These functions program the VGK_SCENE_* memory region so the DSP scene
// loop can iterate over multiple saved objects in a single trigger.  The host
// writes the descriptor, triggers the kernel, then reads combined results.
//
// Work in progress.  Not optimized.

void vgk_scene_enable(bool enabled) {
    vgk_scene_mode_active = enabled;
    vs1053_mem_write(VGK_SCENE_ENABLE, enabled ? 0x0001 : 0x0000);
    if (enabled) {
        vgk_descriptor_enable(true);
    } else {
        // don't disable descriptor if near_far_coloring mode is active.
        if (!vgk_near_far_coloring) {
            vgk_descriptor_enable(false);
        }
    }    
}

void vgk_scene_no_occlusion_enable(void) {
    vs1053_mem_write(VGK_SCENE_FLAGS, VGK_SCENE_FLAG_NO_OCCLUSION);
}

void vgk_scene_no_occlusion_disable(void) {
    vs1053_mem_write(VGK_SCENE_FLAGS, 0x0000);
}

void vgk_scene_set_object_count(uint8_t n_objects) {
    if (n_objects > VGK_SCENE_MAX_OBJECTS) {
        n_objects = VGK_SCENE_MAX_OBJECTS;
    }
    vs1053_mem_write(VGK_SCENE_N_OBJECTS, (uint16_t)n_objects);
}

void vgk_scene_set_object(uint8_t index, const SceneObjectParams *obj) {
    if (index >= VGK_SCENE_MAX_OBJECTS) {
        return;
    }
    uint16_t addr = VGK_SCENE_OBJ_PARAMS +
                    (uint16_t)index * VGK_SCENE_OBJ_STRIDE;
    // Write the 6-word per-object record:
    //   [slot_idx, pitch_yaw, roll_scale, pos_x, pos_y, pos_z]
    vs1053_mem_write(addr, obj->slot);
    vs1053_sci_write(SCI_WRAM, (uint16_t)obj->pitch << 8 | obj->yaw);
    vs1053_sci_write(SCI_WRAM, (uint16_t)obj->roll << 8 | obj->scale);
    vs1053_sci_write(SCI_WRAM, (uint16_t)obj->pos_x);
    vs1053_sci_write(SCI_WRAM, (uint16_t)obj->pos_y);
    vs1053_sci_write(SCI_WRAM, (uint16_t)obj->pos_z);
}

void vgk_scene_set_descriptor(uint8_t n_objects, const SceneObjectParams *objects) {
    if (n_objects > VGK_SCENE_MAX_OBJECTS) {
        n_objects = VGK_SCENE_MAX_OBJECTS;
    }
    vgk_scene_set_object_count(n_objects);
    for (uint8_t i = 0; i < n_objects; ++i) {
        vgk_scene_set_object(i, &objects[i]);
    }
}

// ---------------------------------------------------------------------------
// Scene results readback
// ---------------------------------------------------------------------------

void vgk_scene_get_result(SceneResult *result) {
    result->total_verts = vs1053_mem_read(VGK_SCENE_TOTAL_VERTS);
    result->total_edges = vs1053_mem_read(VGK_SCENE_TOTAL_EDGES);
    result->total_clips = vs1053_mem_read(VGK_SCENE_TOTAL_CLIPS);
    result->n_objects   = (uint8_t)vs1053_mem_read(VGK_SCENE_N_OBJECTS);
}

void vgk_scene_object_meta_get(uint8_t index, SceneObjectMeta *meta) {
    if (index >= VGK_SCENE_MAX_OBJECTS) {
        return;
    }
    meta->vert_offset = vs1053_mem_read(VGK_SCENE_VERT_OFFSET + index);
    meta->edge_offset = vs1053_mem_read(VGK_SCENE_EDGE_OFFSET + index);
    meta->clip_offset = vs1053_mem_read(VGK_SCENE_CLIP_OFFSET + index);
    meta->vert_count  = vs1053_mem_read(VGK_SCENE_VERT_COUNT  + index);
    meta->edge_count  = vs1053_mem_read(VGK_SCENE_EDGE_COUNT  + index);
    meta->centroid_z  = (int16_t)vs1053_mem_read(VGK_SCENE_DEPTH + index);

    uint16_t aabb_addr = VGK_SCENE_AABB +
                         (uint16_t)index * VGK_SCENE_AABB_STRIDE;
    meta->aabb_min_x  = (int16_t)vs1053_mem_read(aabb_addr);
    meta->aabb_max_x  = (int16_t)vs1053_mem_read(aabb_addr + 1);
    meta->aabb_min_y  = (int16_t)vs1053_mem_read(aabb_addr + 2);
    meta->aabb_max_y  = (int16_t)vs1053_mem_read(aabb_addr + 3);
}

uint16_t vgk_scene_obj_edge_offset_get(uint8_t index) {
    if (index >= VGK_SCENE_MAX_OBJECTS) {
        return 0;
    }
    return vs1053_mem_read(VGK_SCENE_EDGE_OFFSET + index);
}

uint16_t vgk_scene_obj_edge_count_get(uint8_t index) {
    if (index >= VGK_SCENE_MAX_OBJECTS) {
        return 0;
    }
    return vs1053_mem_read(VGK_SCENE_EDGE_COUNT + index);
}


uint8_t vgk_scene_scrn_edges_get(uint8_t n_objects,
                                const SceneObjectParams *objects,
                                uint8_t near_color, uint8_t far_color,
                                uint8_t draw_layer) {

    // incomplete implementation. one color

    uint16_t edges_written = 0;
    // Write the scene descriptor and trigger the kernel in scene mode.
    vgk_scene_enable(true);
    if (objects != NULL) {
        vgk_scene_set_descriptor(n_objects, objects);
    }
    vgk_trigger();

    if (vgk_wait_complete(10000) != 1) {
        vgk_scene_enable(false);
        textPrint("Error: Geometry kernel timeout or error.\n");
        return 0;  // timeout or error
    }

    // Read combined scene results.
    SceneResult res;
    vgk_scene_get_result(&res);

    edges_written = vgk_scrn_edges_get(draw_layer, near_color);

    vgk_scene_enable(false);
    return edges_written;
}

#endif
