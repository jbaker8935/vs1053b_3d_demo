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

int16_t kernelWriteC(uint8_t fd, void *buf, uint16_t nbytes) {
    kernelArgs->file.write.stream = fd;
    kernelArgs->common.buf = buf;
    kernelArgs->common.buflen = nbytes;
    kernelCall(File.Write);
    if (kernelError) return -1;

    for (;;) {
        kernelNextEvent();
        if (kernelEventData.type == kernelEvent(file.WROTE)) return kernelEventData.file.data.delivered;
        if (kernelEventData.type == kernelEvent(file.ERROR)) return -1;
    }
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
    uint16_t base = VGK_SAVE_AREA_X + (uint16_t)slot * VGK_SAVE_SLOT_SIZE;
    vs1053_mem_write(base + VGK_SLOT_N_VERTICES, model->vertex_count);
    vs1053_sci_write(SCI_WRAMADDR, base + VGK_SLOT_INPUT_VERT);
    for (uint8_t i = 0; i < model->vertex_count; ++i) {
        vs1053_sci_write(SCI_WRAM, model->vx[i]);
        vs1053_sci_write(SCI_WRAM, model->vy[i]);
        vs1053_sci_write(SCI_WRAM, model->vz[i]);
    }
}

// Tracks the active hidden-line state so get_screen_edges_full_asm can
// choose the fast path (no OUTPUT_EDGES reads) when removal is disabled.
// Non-static so emit_edges_asm.s can access it via .extern.
bool vgk_no_near_far_coloring = false;
bool vgk_hidden_line_active = false;

void vgk_hidden_line_disable(void) {
    vgk_hidden_line_active = false;
    vs1053_mem_write(VGK_ENABLE_HIDDEN_LINE, 0x0000);
}

void vgk_hidden_line_enable(void) {
    vgk_hidden_line_active = true;
    vs1053_mem_write(VGK_ENABLE_HIDDEN_LINE, 0x0001);
}

void vgk_model_hidden_line_init(const Model3D *model, uint8_t slot) {
    uint16_t base = VGK_SAVE_AREA_X + (uint16_t)slot * VGK_SAVE_SLOT_SIZE;
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
    // Write geometry directly to the save slot: no DSP round-trip needed.
    // The DSP _load_object (AICTRL2 trigger) will copy from this slot to the
    // active working area when the object is first used in a scene.
    vgk_model_vertices_init(model, slot);
    vgk_model_edges_init(model, slot);
    vgk_model_hidden_line_init(model, slot);
}

void vgk_model_edges_init(const Model3D *model, uint8_t slot) {
    uint16_t base = VGK_SAVE_AREA_X + (uint16_t)slot * VGK_SAVE_SLOT_SIZE;
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

/* Call the registered yield callback once.  Use this at coarse-grained idle
 * points (after a full object readback, inside a vsync spin-wait) to keep the
 * audio tick running without adding a poll inside every 15 µs SCI call. */
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
            for (volatile uint16_t delay = 0; delay < 25; delay++) {
                __asm__("nop");
            }
        }
        elapsed++;
    }
    return 0;  // Timeout
}

uint16_t screen_x[VGK_MAX_VERTICES];
uint16_t clip_verts_x[VGK_MAX_VERTICES];
uint16_t screen_y[VGK_MAX_VERTICES];
uint8_t screen_y8[VGK_MAX_VERTICES];
uint16_t clip_verts_y[VGK_MAX_VERTICES];

// SoA coordinate arrays for the direct-emit path (get_screen_edges_with_depth).
// Split into separate lo/hi/y byte arrays so assembly can index with vertex
// index directly (LDA arr,X) without a ×2 multiply needed for uint16_t arrays.
uint8_t scr_x_lo[VGK_MAX_VERTICES];  // screen X low  bytes — for original vertices
uint8_t scr_x_hi[VGK_MAX_VERTICES];  // screen X high bytes
uint8_t scr_y[VGK_MAX_VERTICES];     // screen Y        (only low byte used)
uint8_t clip_x_lo[16];                   // clip   X low  bytes — for clipped vertices
uint8_t clip_x_hi[16];                   // clip   X high bytes
uint8_t clip_y[16];                      // clip   Y
int16_t zbuffer[VGK_MAX_VERTICES];
int16_t depth_metric[VGK_MAX_VERTICES];
uint16_t scene_screen_x[VGK_SCENE_MAX_VERTS];
uint16_t scene_screen_y[VGK_SCENE_MAX_VERTS];
int16_t scene_clip_x[VGK_SCENE_MAX_CLIPS];
int16_t scene_clip_y[VGK_SCENE_MAX_CLIPS];

// Zero-page parameter block for get_screen_edges_full_asm.
// Defined here (not in asm) so the LTO optimizer sees their ZP cost when deciding
// whether other variables can be autopromoted — preventing ZP overflow.
// g_emit_layer_ctrl is computed entirely in asm but defined here for the same reason.
uint8_t __zp g_emit_edge_count;
uint8_t __zp g_emit_visible_count;
uint8_t __zp g_emit_n_input;
uint8_t __zp g_emit_n_clip;
uint8_t __zp g_emit_near_color;
uint8_t __zp g_emit_far_color;
uint8_t __zp g_emit_layer_ctrl;

// ZP pointers used by get_screen_edges_full_asm fast path.
// Assembly uses (g_emit_edge_a),Y and (g_emit_edge_b),Y for edge vertex index reads,
// avoiding the pre-resolved g_emit_x0/y0/x1/y1 tables that existed previously.
const uint8_t * __zp g_emit_edge_a;
const uint8_t * __zp g_emit_edge_b;

// Per-frame edge buffer shared between get_screen_edges_full_asm and scene_get_screen_edges.
// Non-static so emit_edges_asm.s can declare them via .extern.
uint16_t g_edge_buf_packed[VGK_MAX_EDGES];
uint8_t  g_edge_buf_flags[VGK_MAX_EDGES];

// Forward declaration of the monolithic asm emitter.
extern uint8_t vgk_scrn_edges_get_asm(uint8_t layer);

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
    for (int i = 0; i < 12; ++i) {
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
    cap->n_output_edges = vs1053_mem_read(VGK_N_OUTPUT_EDGES);
    cap->n_clip_verts = vs1053_mem_read(VGK_N_CLIP_VERTS);
    cap->status = vs1053_mem_read(VGK_STATUS);

    // sample output edge list — Phase 1 format: flags packed 2/word at OUTPUT_EDGE_FLAGS,
    // packed v0v1 at OUTPUT_EDGE_PACKED.
    {
        uint16_t n = cap->n_output_edges < CAPTURE_MAX_EDGES ? cap->n_output_edges : CAPTURE_MAX_EDGES;
        for (int i = 0; i < (int)n; ++i) {
            cap->edge_list[i]  = vs1053_mem_read(VGK_OUTPUT_EDGE_PACKED + (uint16_t)i);
            uint16_t fw = vs1053_mem_read(VGK_OUTPUT_EDGE_FLAGS + (uint16_t)(i >> 1));
            cap->edge_flags[i] = (uint8_t)((i & 1) ? (fw >> 8) : (fw & 0xFF));
        }
    }

    // sample clipped screen coords
    {
        uint16_t base = VGK_CLIP_SCREEN;
        for (int i = 0; i < cap->n_clip_verts && i < CAPTURE_MAX_CLIP_V; ++i) {
            cap->clip_screen[i * 2 + 0] = vs1053_mem_read(base + (uint16_t)(i * 2));
            cap->clip_screen[i * 2 + 1] = vs1053_mem_read(base + (uint16_t)(i * 2 + 1));
        }
    }
}

__attribute__((noinline))
uint8_t vgk_scrn_edges_with_depth_get(Model3D *model, uint8_t layer) {
    uint8_t ec = model->edge_count;
    if (ec > VGK_MAX_EDGES) ec = VGK_MAX_EDGES;
    uint8_t near_color = (uint8_t)(model->object_color & 0x00FF);
    uint8_t far_color  = (uint8_t)(model->object_color >> 8);
    if (vgk_no_near_far_coloring) far_color = near_color;
    g_emit_n_input       = model->vertex_count;
    g_emit_edge_count    = ec;
    g_emit_near_color    = near_color;
    g_emit_far_color     = far_color;
    g_emit_visible_count = 0;
    g_emit_edge_a        = model->edge_a;
    g_emit_edge_b        = model->edge_b;
    return vgk_scrn_edges_get_asm(layer);
}

uint8_t vgk_scrn_edges_get(Model3D *model, uint8_t color) {
    // Read number of input vertices (needed to distinguish original vs clipped)

    uint8_t edges_written = 0;
    uint8_t start_line_count = g_line_count;

    volatile uint8_t n_input = model->vertex_count;  // vs1053_mem_read(VGK_N_VERTICES);

    // Read number of output edges
    volatile uint8_t edge_count = vs1053_mem_read(VGK_N_OUTPUT_EDGES);

    vs1053_sci_write(SCI_WRAMADDR, VGK_SCREEN_COORDS);
    for (uint8_t i = 0; i < n_input; ++i) {
        screen_x[i] = vs1053_sci_read(SCI_WRAM);
        screen_y[i] = vs1053_sci_read(SCI_WRAM) & 0x00FF;
    }

    // Read all clip screen coords in one burst (if any)
    uint8_t n_clip = vs1053_mem_read(VGK_N_CLIP_VERTS);

    {
        // Read back edges from X-RAM so host always honors plugin cull flags
        // supports up to 16 clipped vertices, are going to replace edge vertices.
        if (n_clip > 16)
            n_clip = 16;  // safety clamp
        int16_t clip_sx[16] = {0};
        int16_t clip_sy[16] = {0};
        if (n_clip > 0) {
            vs1053_sci_write(SCI_WRAMADDR, VGK_CLIP_SCREEN);
            for (uint8_t i = 0; i < n_clip; ++i) {
                clip_sx[i] = (int16_t)vs1053_sci_read(SCI_WRAM);
                clip_sy[i] = (int16_t)vs1053_sci_read(SCI_WRAM);
            }
        }

        // Phase 1: burst-read all flags from VGK_OUTPUT_EDGE_FLAGS,
        // then use model edge data for unclipped edges and individual SCI reads for clipped.
        {
            uint8_t n_flag_words = (edge_count + 1) >> 1;
            vs1053_sci_write(SCI_WRAMADDR, VGK_OUTPUT_EDGE_FLAGS);
            for (uint8_t i = 0; i < n_flag_words; ++i) {
                uint16_t fw = vs1053_sci_read(SCI_WRAM);
                g_edge_buf_flags[i * 2]     = (uint8_t)(fw & 0xFF);
                if ((uint8_t)(i * 2 + 1) < edge_count)
                    g_edge_buf_flags[i * 2 + 1] = (uint8_t)(fw >> 8);
            }

            for (uint8_t e = 0; e < edge_count; ++e) {
                uint8_t flags = g_edge_buf_flags[e];
                if (!(flags & VGK_EDGE_VISIBLE))
                    continue;

                // Always read from VGK_OUTPUT_EDGE_PACKED: output edge index e does
                // NOT correspond to input edge e when culling reduces output count.
                uint16_t packed = vs1053_mem_read(VGK_OUTPUT_EDGE_PACKED + e);
                uint8_t v0 = (uint8_t)(packed & 0xFF);
                uint8_t v1 = (uint8_t)(packed >> 8);

                int16_t sx0, sy0, sx1, sy1;
                if (v0 < n_input) {
                    sx0 = (int16_t)screen_x[v0];
                    sy0 = (int16_t)screen_y[v0];
                } else {
                    uint8_t ci = v0 - n_input;
                    if (ci < n_clip) { sx0 = clip_sx[ci]; sy0 = clip_sy[ci]; }
                    else             { sx0 = 0; sy0 = 0; }
                }

                if (v1 < n_input) {
                    sx1 = (int16_t)screen_x[v1];
                    sy1 = (int16_t)screen_y[v1];
                } else {
                    uint8_t ci = v1 - n_input;
                    if (ci < n_clip) { sx1 = clip_sx[ci]; sy1 = clip_sy[ci]; }
                    else             { sx1 = 0; sy1 = 0; }
                }

                add_line_to_list((uint16_t)sx0, (uint8_t)sy0, (uint16_t)sx1, (uint8_t)sy1, color);
            }
        }
    }

    edges_written = g_line_count - start_line_count;
    // Return actual number of visible edges written
    return edges_written;
}

#if 1

// =============================================================================
// Scene API implementation
// =============================================================================
// These functions program the VGK_SCENE_* memory region so the DSP scene
// loop can iterate over multiple saved objects in a single trigger.  The host
// writes the descriptor, triggers the kernel, then reads combined results.

void vgk_scene_enable(void) {
    vs1053_mem_write(VGK_SCENE_ENABLE, 0x0001);
}

void vgk_scene_disable(void) {
    vs1053_mem_write(VGK_SCENE_ENABLE, 0x0000);
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


uint16_t vgk_scene_read_screen_coords(uint16_t *sx_out, uint16_t *sy_out,
                                   uint16_t max_verts) {
    uint16_t total = vs1053_mem_read(VGK_SCENE_TOTAL_VERTS);
    if (total > max_verts) {
        total = max_verts;
    }
    if (total > VGK_SCENE_MAX_VERTS) {
        total = VGK_SCENE_MAX_VERTS;
    }
    vs1053_sci_write(SCI_WRAMADDR, VGK_SCENE_SCREEN_COORDS);
    for (uint16_t i = 0; i < total; ++i) {
        sx_out[i] = vs1053_sci_read(SCI_WRAM);
        sy_out[i] = vs1053_sci_read(SCI_WRAM);
    }
    return total;
}

uint16_t vgk_scene_read_output_edges(uint16_t *packed_out, uint16_t *flags_out,
                                  uint16_t max_edges) {
    uint16_t total = vs1053_mem_read(VGK_SCENE_TOTAL_EDGES);
    if (total > max_edges) {
        total = max_edges;
    }
    if (total > VGK_SCENE_MAX_EDGES) {
        total = VGK_SCENE_MAX_EDGES;
    }
    vs1053_sci_write(SCI_WRAMADDR, VGK_SCENE_OUTPUT_EDGES);
    for (uint16_t i = 0; i < total; ++i) {
        packed_out[i] = vs1053_sci_read(SCI_WRAM);
        flags_out[i]  = vs1053_sci_read(SCI_WRAM);
    }
    return total;
}

uint16_t vgk_scene_read_clip_screen(int16_t *sx_out, int16_t *sy_out,
                                 uint16_t max_clips) {
    uint16_t total = vs1053_mem_read(VGK_SCENE_TOTAL_CLIPS);
    if (total > max_clips) {
        total = max_clips;
    }
    if (total > VGK_SCENE_MAX_CLIPS) {
        total = VGK_SCENE_MAX_CLIPS;
    }
    vs1053_sci_write(SCI_WRAMADDR, VGK_SCENE_CLIP_SCREEN);
    for (uint16_t i = 0; i < total; ++i) {
        sx_out[i] = (int16_t)vs1053_sci_read(SCI_WRAM);
        sy_out[i] = (int16_t)(vs1053_sci_read(SCI_WRAM) & 0x00FF);
    }
    return total;
}

uint8_t vgk_scene_scrn_edges_get(uint8_t n_objects,
                                const SceneObjectParams *objects,
                                uint8_t near_color, uint8_t far_color,
                                uint8_t draw_layer) {
    // Write the scene descriptor and trigger the kernel in scene mode.
    vgk_scene_enable();
    // Set scene if objects is non-null, otherwise use existing descriptor in DSP memory (caller can pre-set objects and just trigger here)
    if (objects != NULL) {
        vgk_scene_set_descriptor(n_objects, objects);
    }
    vgk_trigger();

    if (vgk_wait_complete(2000) != 1) {
        vgk_scene_disable();
        return 0;  // timeout or error
    }

    // Read combined scene results.
    SceneResult res;
    vgk_scene_get_result(&res);

    uint16_t n_verts = res.total_verts;
    uint16_t n_clips = res.total_clips;

    if (n_verts > VGK_SCENE_MAX_VERTS) n_verts = VGK_SCENE_MAX_VERTS;
    if (n_clips > VGK_SCENE_MAX_CLIPS) n_clips = VGK_SCENE_MAX_CLIPS;

    // Read combined scene screen and clip buffers once.
    // Per-edge near/far colour selection uses VGK_EDGE_NEAR which the DSP
    // sets per-edge relative to each object's own vertex depth range, giving
    // the same per-object near/far behaviour as single-object mode.
    if (n_verts > 0) {
        vs1053_sci_write(SCI_WRAMADDR, VGK_SCENE_SCREEN_COORDS);
        for (uint16_t i = 0; i < n_verts; ++i) {
            scene_screen_x[i] = vs1053_sci_read(SCI_WRAM);
            scene_screen_y[i] = vs1053_sci_read(SCI_WRAM);
        }
    }

    if (n_clips > 0) {
        vs1053_sci_write(SCI_WRAMADDR, VGK_SCENE_CLIP_SCREEN);
        for (uint16_t i = 0; i < n_clips; ++i) {
            scene_clip_x[i] = (int16_t)vs1053_sci_read(SCI_WRAM);
            scene_clip_y[i] = (int16_t)(vs1053_sci_read(SCI_WRAM) & 0x00FF);
        }
    }

    /* Yield after the bulk coord readback so audio isn't starved across
     * the potentially large screen + clip SCI burst before edge drawing. */
    vgk_yield();

    uint8_t edges_written = 0;

    // Process each object's edges from the combined scene output.
    // Packed vertex indices are scene-global; clip flags determine whether
    // each endpoint indexes scene clip coords or scene screen coords.
    for (uint8_t obj = 0; obj < res.n_objects; ++obj) {

        // Read this object's output edges from the combined edge buffer.
        uint16_t e_off = vgk_scene_obj_edge_offset_get(obj);
        uint16_t e_cnt = vgk_scene_obj_edge_count_get(obj);
        if (e_cnt > VGK_MAX_EDGES) e_cnt = VGK_MAX_EDGES;

        // Read this object's output edges into the local buffer (one SCI burst).
        vs1053_sci_write(SCI_WRAMADDR,
                         VGK_SCENE_OUTPUT_EDGES + e_off * 2);
        for (uint16_t e = 0; e < e_cnt; ++e) {
            g_edge_buf_packed[e] = vs1053_sci_read(SCI_WRAM);
            g_edge_buf_flags[e]  = (uint8_t)(vs1053_sci_read(SCI_WRAM) & 0x00FF);
        }

        // Far pass: draw edges with NEAR flag clear so they land underneath.
        for (uint16_t e = 0; e < e_cnt; ++e) {
            uint8_t flags = g_edge_buf_flags[e];
            if (!(flags & VGK_EDGE_VISIBLE)) continue;
            if (flags & VGK_EDGE_NEAR) continue;

            uint16_t packed = g_edge_buf_packed[e];
            uint8_t v0 = packed & 0xFF;
            uint8_t v1 = (packed >> 8) & 0xFF;

            int16_t sx0, sy0, sx1, sy1;

            if (flags & VGK_EDGE_CLIP_V0) {
                if (v0 < n_clips) { sx0 = scene_clip_x[v0]; sy0 = scene_clip_y[v0]; }
                else              { sx0 = 0; sy0 = 0; }
            } else {
                if (v0 < n_verts) { sx0 = (int16_t)scene_screen_x[v0]; sy0 = (int16_t)(scene_screen_y[v0] & 0x00FF); }
                else              { sx0 = 0; sy0 = 0; }
            }
            if (flags & VGK_EDGE_CLIP_V1) {
                if (v1 < n_clips) { sx1 = scene_clip_x[v1]; sy1 = scene_clip_y[v1]; }
                else              { sx1 = 0; sy1 = 0; }
            } else {
                if (v1 < n_verts) { sx1 = (int16_t)scene_screen_x[v1]; sy1 = (int16_t)(scene_screen_y[v1] & 0x00FF); }
                else              { sx1 = 0; sy1 = 0; }
            }

            add_line_to_list((uint16_t)sx0, (uint8_t)sy0,
                             (uint16_t)sx1, (uint8_t)sy1, far_color);
            ++edges_written;
        }
        if (g_line_count > 0) { draw_lines_asm(draw_layer); reset_line_list(); }

        // Near pass: draw edges with NEAR flag set on top of the far edges.
        for (uint16_t e = 0; e < e_cnt; ++e) {
            uint8_t flags = g_edge_buf_flags[e];
            if (!(flags & VGK_EDGE_VISIBLE)) continue;
            if (!(flags & VGK_EDGE_NEAR)) continue;

            uint16_t packed = g_edge_buf_packed[e];
            uint8_t v0 = packed & 0xFF;
            uint8_t v1 = (packed >> 8) & 0xFF;

            int16_t sx0, sy0, sx1, sy1;

            if (flags & VGK_EDGE_CLIP_V0) {
                if (v0 < n_clips) { sx0 = scene_clip_x[v0]; sy0 = scene_clip_y[v0]; }
                else              { sx0 = 0; sy0 = 0; }
            } else {
                if (v0 < n_verts) { sx0 = (int16_t)scene_screen_x[v0]; sy0 = (int16_t)(scene_screen_y[v0] & 0x00FF); }
                else              { sx0 = 0; sy0 = 0; }
            }
            if (flags & VGK_EDGE_CLIP_V1) {
                if (v1 < n_clips) { sx1 = scene_clip_x[v1]; sy1 = scene_clip_y[v1]; }
                else              { sx1 = 0; sy1 = 0; }
            } else {
                if (v1 < n_verts) { sx1 = (int16_t)scene_screen_x[v1]; sy1 = (int16_t)(scene_screen_y[v1] & 0x00FF); }
                else              { sx1 = 0; sy1 = 0; }
            }

            add_line_to_list((uint16_t)sx0, (uint8_t)sy0,
                             (uint16_t)sx1, (uint8_t)sy1, near_color);
            ++edges_written;
        }
        if (g_line_count > 0) {
            draw_lines_asm(draw_layer);
            reset_line_list();
        }

        /* Yield between objects: covers edge readback + two draw passes
         * per object, keeping audio ticks from being starved on scenes
         * with multiple objects. */
        vgk_yield();
    }

    vgk_scene_disable();
    return edges_written;
}

#endif
