#ifndef GEOMETRY_KERNEL_H
#define GEOMETRY_KERNEL_H
#include <stdint.h>
#include "../include/3d_math.h"
#include "../include/3d_object.h"

/* VS1053b register definitions and host helpers */
#include "../include/vs1053b.h"
// ==============================================================================
// MEMORY MAP
// ==============================================================================

// I-RAM Plugin Entry (WRAMADDR = DSP address + 0x8000)
#define HOST_GEOM_PLUGIN_ENTRY  0x8050  // DSP: 0x0050

#define VGK_MAX_VERTICES  60
#define VGK_MAX_EDGES     90
#define VGK_MAX_FACES     32
#define VGK_INVALID_FACE  0xFF
// Input Parameters (X-RAM)
#define VGK_N_VERTICES    0x1800  // Number of vertices (16-bit)

// Packed Euler inputs (8-bit indices) and Q7 scale (packed)
// Ordered consecutively so SCI autoincrement can be used to write the values.
#define VGK_OBJ_PITCH_YAW  0x1801  // packed: high byte pitch, low byte yaw
#define VGK_OBJ_ROLL_SCALE 0x1802  // packed: high byte roll, low byte scale (Q7)
#define VGK_OBJ_POS_X      0x1803  // World position X
#define VGK_OBJ_POS_Y      0x1804  // World position Y
#define VGK_OBJ_POS_Z      0x1805  // World position Z

#define VGK_CAM_PITCH_YAW  0x1806  // packed: high byte pitch, low byte yaw
#define VGK_CAM_ROLL_SCALE 0x1807  // packed: high byte roll, low byte scale (Q7)
#define VGK_CAM_POS_X      0x1808  // Camera position X
#define VGK_CAM_POS_Y      0x1809  // Camera position Y
#define VGK_CAM_POS_Z      0x180A  // Camera position Z

// Unpacked trig and scale (kernel writes these based on the packed inputs for convenience)
#define VGK_OBJ_SX        0x180B  // sin(rx) Q14
#define VGK_OBJ_CX        0x180C  // cos(rx) Q14
#define VGK_OBJ_SY        0x180D  // sin(ry) Q14
#define VGK_OBJ_CY        0x180E  // cos(ry) Q14
#define VGK_OBJ_SZ        0x180F  // sin(rz) Q14
#define VGK_OBJ_CZ        0x1810  // cos(rz) Q14
#define VGK_OBJ_SCALE     0x1811  // Global scale Q14 (converted from Q7)

#define VGK_CAM_SX        0x1812  // sin(rx) Q14
#define VGK_CAM_CX        0x1813  // cos(rx) Q14
#define VGK_CAM_SY        0x1814  // sin(ry) Q14
#define VGK_CAM_CY        0x1815  // cos(ry) Q14
#define VGK_CAM_SZ        0x1816  // sin(rz) Q14
#define VGK_CAM_CZ        0x1817  // cos(rz) Q14

// Convenience aliases
#define VGK_OBJ_TRIG      VGK_OBJ_SX
#define VGK_CAM_TRIG      VGK_CAM_SX

// Input/Output Buffers
#define VGK_INPUT_VERT    0x0F00  // Input vertices (X-RAM)
#define VGK_OUTPUT_VERT   0x500C  // Output vertices (Y-RAM)

// Matrix storage (Y-RAM) - 3x4 = 12 words
#define VGK_MATRIX_BASE   0x5000  // Final composite matrix
#define VGK_TEMP_OBJ      0x1EDA  // Object matrix temp (12 words, X-RAM)
#define VGK_TEMP_CAM      0x5860  // Camera matrix temp (12 words, Y-RAM)

// Magic trigger value for SCI_AICTRL0 (PLUGIN_MODE)
#define VGK_TRIGGER_MAGIC 0xCAFE  // Write this to SCI_AICTRL0 to trigger

// Internal kernal state memory (X-RAM)
#define VGK_LR0_SAVE_PROJECT           0x1ED7  // For _project_all_vertices
#define VGK_LR0_SAVE_CLIP              0x1ED8  // For _clip_all_edges
#define VGK_LR0_SAVE_CLIP_AND_PROJECT  0x1ED9  // For _clip_and_project

// Edge List (X-RAM: placed inside free X-RAM region to avoid audio decoder memory)
// Base free X-RAM region per memory map: 0x3565..0x3FFF

#define VGK_X_FREE_BASE     0x3600  // Aligned free X-RAM base (within free_x memory define provided by VLSI)
#define VGK_N_EDGES         VGK_X_FREE_BASE         // Number of input edges (X-RAM)
#define VGK_EDGE_LIST       (VGK_X_FREE_BASE + 1)   // Edge list (packed): [v0_low | v1_high] × n (1 word per edge)
// Edge list ends at (VGK_X_FREE_BASE + 0x24) inclusive (max 36 edges) 
#define VGK_MAX_INPUT_EDGES 90  // Maximum allowed input edges (match output edge capacity)

// Per-edge adjacent face map (packed): low byte = face0, high byte = face1.
// Use 0xFF for "no adjacent face" (boundary edge).
#define VGK_EDGE_FACE_MAP   (VGK_X_FREE_BASE + 0x5B) // 90 words, one per input edge; ends +0xB4

// Status and Projection Parameters 

#define VGK_STATUS_IDLE         0
#define VGK_STATUS_BUSY         1
#define VGK_STATUS_DONE         0xABCD
#define VGK_STATUS_SAVE_ERROR   0xE201
#define VGK_STATUS_LOAD_ERROR   0xE202

#define VGK_LR0_SAVE        (VGK_X_FREE_BASE + 0xB5)  // Save/restore LR0 (X-RAM)
#define VGK_STATUS          (VGK_X_FREE_BASE + 0xB6)  // 0=idle, 1=busy, 0xABCD=done (X-RAM)
#define VGK_ENABLE_PROJECT  (VGK_X_FREE_BASE + 0xB7)  // Non-zero to enable projection
#define VGK_ENABLE_CLIP     (VGK_X_FREE_BASE + 0xB8)  // Non-zero to enable clipping
#define VGK_PROJ_FOCAL      (VGK_X_FREE_BASE + 0xB9)  // Focal length (Q0, = half_w for 90 deg FOV)
#define VGK_PROJ_HALF_W     (VGK_X_FREE_BASE + 0xBA)  // Half screen width (Q0, e.g., 160)
#define VGK_PROJ_HALF_H     (VGK_X_FREE_BASE + 0xBB)  // Half screen height (Q0, e.g., 120)
#define VGK_PROJ_NEAR_Z     (VGK_X_FREE_BASE + 0xBC)  // Near plane Z (Q0, negative, e.g., -256)
#define VGK_SCRATCH0        (VGK_X_FREE_BASE + 0xBD)  // General scratch (X-RAM)
#define VGK_SCRATCH1        (VGK_X_FREE_BASE + 0xBE)  // General scratch (X-RAM)
#define VGK_LR0_SAVE2       (VGK_X_FREE_BASE + 0xBF)  // Save/restore LR0 (X-RAM)
#define VGK_LR0_SAVE3       (VGK_X_FREE_BASE + 0xC0)  // Save/restore LR0 (X-RAM)
#define VGK_ENABLE_HIDDEN_LINE (VGK_X_FREE_BASE + 0xC1) // Non-zero enables face-normal hidden-line culling
#define VGK_N_FACES            (VGK_X_FREE_BASE + 0xC2) // Number of face normals provided
#define VGK_ENABLE_DESCRIPTOR  (VGK_X_FREE_BASE + 0xC3) // Non-zero = include 1-word descriptor before each stream edge (bit15=near, bits8-14=slot, bits0-7=edge_idx)
#define VGK_CURRENT_SLOT       (VGK_X_FREE_BASE + 0xC4) // Current slot index: written by mode-4 load handler; overwritten per-object in scene loop

// Optional hidden-line buffers in free X-RAM.
#define VGK_FACE_VISIBILITY   (VGK_X_FREE_BASE + 0x146) // 32 words: per-face visibility (0/1/2); ends +0x165
#define VGK_FACE_NORMALS      (VGK_X_FREE_BASE + 0x166) // 96 words: up to 32 Q14 normals (3 words each); ends +0x1C5
#define VGK_FACE_REP_VERT     (VGK_X_FREE_BASE + 0x1C6) // 32 words: per-face representative vertex index; ends +0x1E5

// Screen Coordinates Output (X-RAM)
#define VGK_SCREEN_COORDS_X  (VGK_X_FREE_BASE + 0xC6) // X-RAM: 128 words for [sx,sy] × up to 60 verts
#define VGK_SCREEN_COORDS    VGK_SCREEN_COORDS_X      // Alias

// Save slot layout: single contiguous block in AAC decoder X-RAM (freed with DAC disabled).
//   0x2000..0x2BFF: slots 0..5 (6 × 512 = 3072 words)
// Region 0x2000..0x2BFF is within AAC decoder X-RAM (0x1F00..0x3565),
// fully accessible via SCI, and clear of stack/parametric/sysvar areas.
#define VGK_SAVE_AREA             0x2000                         // X-RAM: contiguous save slots 0..5
#define VGK_SAVE_SLOT_SIZE        0x200                          // words per object save slot (512)
#define VGK_SAVE_SLOT_COUNT       6                              // total save slots (0..5)
#define VGK_SAVE_AREA_END         (VGK_SAVE_AREA + (VGK_SAVE_SLOT_SIZE * VGK_SAVE_SLOT_COUNT) - 1)  // 0x2BFF

#define VGK_INPUT_VERT_END        (VGK_INPUT_VERT + (VGK_MAX_VERTICES * 3) - 1)

// Offsets within a slot (add to slot base = VGK_SAVE_AREA + slot * VGK_SAVE_SLOT_SIZE)
#define VGK_SLOT_N_VERTICES    0x00  // 1 word: vertex count
#define VGK_SLOT_INPUT_VERT    0x01  // 180 words: [vx,vy,vz] x up to 60 verts
#define VGK_SLOT_N_EDGES       0xB5  // 1 word: edge count (offset 181 = 1 + 180)
#define VGK_SLOT_EDGE_LIST     0xB6  // 90 words: packed [v1|v0] per edge
// Slot field: N_FACES 
#define VGK_SLOT_N_FACES       0x110  // 1 word: face count
#define VGK_SLOT_EDGE_FACE_MAP 0x111  // 90 words: packed [face1|face0] per edge
#define VGK_SLOT_FACE_NORMALS  0x16B  // 96 words: [nx,ny,nz] x up to 32 faces (offset 171)

// Edge flags for output
#define VGK_EDGE_VISIBLE    0x0001  // Edge is visible
#define VGK_EDGE_CLIP_V0    0x0002  // V0 was clipped (new vertex created)
#define VGK_EDGE_CLIP_V1    0x0004  // V1 was clipped (new vertex created)
#define VGK_EDGE_NEAR       0x0008  // Edge midpoint depth classified as near
#define VGK_EDGE_CULLED     0x0000  // Edge is culled

// ==============================================================================
// SCENE MEMORY MAP (Decoder X-RAM: 0x0400..0x0A3F)
// ==============================================================================
// The scene feature uses decoder X-RAM regions that are normally occupied by
// audio decoders.  Since this is a graphics-only application the memory is
// available for scene descriptor and combined output accumulation.

// Scene Descriptor (X-RAM 0x0400)
#define VGK_SCENE_BASE          0x0400
#define VGK_SCENE_ENABLE        (VGK_SCENE_BASE + 0x00)   // Non-zero = scene mode
#define VGK_SCENE_N_OBJECTS     (VGK_SCENE_BASE + 0x01)   // Number of objects (1..8)
#define VGK_SCENE_MAX_OBJECTS   8                               // Maximum objects per scene
#define VGK_SCENE_OBJ_PARAMS    (VGK_SCENE_BASE + 0x02)   // Per-object records start
#define VGK_SCENE_OBJ_STRIDE    6                               // Words per object record

// Per-object record layout (6 words each, base + obj_idx * stride):
//   +0: slot_idx      Save slot index (0..7)
//   +1: pitch_yaw     Packed: hi=pitch, lo=yaw
//   +2: roll_scale    Packed: hi=roll, lo=scale_q7
//   +3: pos_x         World position X
//   +4: pos_y         World position Y
//   +5: pos_z         World position Z
// Total scene descriptor: 2 + 8*6 = 50 words (0x0400..0x0431)

// Scene Per-Object Metadata (X-RAM 0x0432)
#define VGK_SCENE_META_BASE     (VGK_SCENE_BASE + 0x32)
#define VGK_SCENE_VERT_OFFSET   (VGK_SCENE_META_BASE + 0x00)  // 8 words: cumulative vert offset
#define VGK_SCENE_EDGE_OFFSET   (VGK_SCENE_META_BASE + 0x08)  // 8 words: cumulative edge offset
#define VGK_SCENE_DEPTH         (VGK_SCENE_META_BASE + 0x10)  // 8 words: centroid depth (view Z)
#define VGK_SCENE_AABB          (VGK_SCENE_META_BASE + 0x18)  // 32 words: 8 x [min_x,max_x,min_y,max_y]
#define VGK_SCENE_AABB_STRIDE   4                                   // Words per AABB record
#define VGK_SCENE_VERT_COUNT    (VGK_SCENE_META_BASE + 0x38)  // 8 words: per-object vert count
#define VGK_SCENE_EDGE_COUNT    (VGK_SCENE_META_BASE + 0x40)  // 8 words: per-object output edge count
#define VGK_SCENE_TOTAL_VERTS   (VGK_SCENE_META_BASE + 0x48)  // 1 word: total combined verts
#define VGK_SCENE_TOTAL_EDGES   (VGK_SCENE_META_BASE + 0x49)  // 1 word: total combined edges
#define VGK_SCENE_CLIP_OFFSET   (VGK_SCENE_META_BASE + 0x4A)  // 8 words: cumulative clip vert offset
#define VGK_SCENE_TOTAL_CLIPS   (VGK_SCENE_META_BASE + 0x52)  // 1 word: total combined clip verts
#define VGK_SCENE_SORT_ORDER    (VGK_SCENE_META_BASE + 0x53)  // 8 words: depth-sorted object indices
#define VGK_SCENE_FLAGS         (VGK_SCENE_META_BASE + 0x5B)  // 0x048D: Scene mode flags
#define VGK_SCENE_FLAG_NO_OCCLUSION  0x0001                        // Skip AABB sort/cull pass; per-object hidden line still runs
// Metadata ends at 0x048D

// SCI readback: read WRAMADDR=0x04FF to get N_STREAM_EDGES, then auto-
// increment through 0x0500 for N*(3 or 4) words. Copy directly to the
// hardware line-draw engine with no further processing required.
#define VGK_N_STREAM_EDGES     0x04FF   // 1 word: total visible edges in stream (X-RAM, before base)
#define VGK_EDGE_STREAM_BASE   0x0500   // stream start (X-RAM, same address as old SCENE_OUTPUT_BASE)
#define VGK_EDGE_STREAM_MAX    512      // max edges; 512*4=2048 words => stream ends at 0x0CFF
// Descriptor field constants (used when VGK_ENABLE_DESCRIPTOR != 0)
#define VGK_EDESC_NEAR_BIT     0x8000   // bit 15: edge midpoint is on the near (camera) side
#define VGK_EDESC_CULL_BIT     0x4000   // bit 14: transient occlusion-pass cull flag; cleared during stream compaction
                                        //   Safe: slot field uses bits 8-14, but max slot=5 so bits 11-14 are always 0
#define VGK_EDESC_SLOT_SHIFT   8        // bits 8-14: save-slot index the object was loaded from
#define VGK_EDESC_SLOT_MASK    0x7F     // 7-bit slot field (slots 0..127)
#define VGK_EDESC_IDX_MASK     0xFF     // bits 0-7: index of this edge within the object's edge list

extern const int16_t sin_table[256];
// When true, objects us the near color for all edges. Improved rendering speed, but no depth coloring.
extern bool vgk_no_near_far_coloring;
// When true, hidden-line removal is active; fast path is suppressed. 'Fast path' is optimized


// geometry kernel functions

void vgk_plugin_init(void);   // clears plugin status and enables the plugin for triggers.
void vgk_reset(void);         // clears plugin status.

// initialize project parameters for rendering.  Only needs to be done once.
// Recommended values for 4:3 aspect 320x240 with vertical fov 90 degrees
// vgk_projection_params_init(120, 160, 120, -64);

void vgk_projection_params_init(int16_t focal, int16_t half_w, int16_t half_h,
                         int16_t near_z);  
                         
// vgk_project_params_init enables projection and clipping by default
// the following functions can be used to enable/disable projection if the application only
// needs the transformed view coordinates without perspective division and clipping.
void vgk_projection_enable(void);
void vgk_projection_disable(void);


/* model functions
* vgk_model_slot_init loads vertices, edges, and face normals into a reserved slot in on-chip memory
* up to 8 models can be loaded simultaneously.
* vgk_model_vertices_init, vgk_model_edges_init, and vgk_model_hidden_line_init initialize the respective
* components of a model in the specified slot. 
* For most use cases, vgk_model_slot_init is the only one used.
* */

void vgk_model_slot_init(const Model3D* model, uint8_t slot);
void vgk_model_vertices_init(const Model3D* model, uint8_t slot);
void vgk_model_edges_init(const Model3D* model, uint8_t slot);
void vgk_model_hidden_line_init(const Model3D* model, uint8_t slot);

// Loads a model as the current object for processing.
// The model remains as the current object between kernel calls
// so multiple instances can be processed without reloading the object.
bool vgk_model_load(uint16_t slot);  

// set the current object's transformation parameters.
void vgk_obj_params_set(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale,
    int16_t pos_x, int16_t pos_y, int16_t pos_z);
    
// Angle and scale update only.
void vgk_obj_angle_scale_set(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale);
// Position-only update.
void vgk_obj_pos_set(int16_t pos_x, int16_t pos_y, int16_t pos_z);

// set the camera parameters (angles, position).  The camera is shared across all objects.
void vgk_cam_params_set(uint8_t pitch, uint8_t yaw, uint8_t roll,
    int16_t pos_x, int16_t pos_y, int16_t pos_z);
    
    
void vgk_hidden_line_enable(void);
void vgk_hidden_line_disable(void);

void vgk_near_far_coloring_enable(bool enabled);

void vgk_trigger(void);
uint8_t vgk_wait_complete(uint16_t timeout_ms);


// 
// Retrieves edges, no near/far coloring. Save drawing instructions to a global list
// for processing by draw_lines_from_list.
// Not currently optimized. So vgk_scrn_edges_with_depth_get is recommended.
//
uint8_t vgk_scrn_edges_get(uint8_t layer, uint8_t color);

/* Register a callback that vgk_yield() calls on every
* polling iteration instead of the default nop-delay.  Pass NULL to restore
* the default behavior.  Intended for audio tick servicing during DSP waits
* so that the audio loop is not starved when multiple objects are rendered. */
void vgk_yield_cb_set(void (*cb)(void));
void vgk_yield(void);

void vgk_line_draw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color, uint8_t layer);

// -----------------------------------------------------------------------------
// Plugin state capture for host-side debugging
// -----------------------------------------------------------------------------
// The host can call `capture_plugin_state()` after each kernel invocation to
// snapshot internal memory

#define CAPTURE_MAX_EDGES VGK_MAX_EDGES
#define CAPTURE_MAX_CLIP_V   VGK_MAX_VERTICES

typedef struct {
    // camera inputs (packed)
    uint16_t cam_pitch_yaw;
    uint16_t cam_roll_scale;
    int16_t  cam_pos_x;
    int16_t  cam_pos_y;
    int16_t  cam_pos_z;

    // unpacked camera trig (optional, mostly for validation)
    int16_t cam_sx, cam_cx, cam_sy, cam_cy, cam_sz, cam_cz;

    // combined camera-to-world matrix (row-major 3×4)
    int16_t matrix[12];

    // projection parameters
    uint16_t proj_focal;
    uint16_t proj_half_w;
    uint16_t proj_half_h;
    int16_t  proj_near_z;

    // runtime counts/status
    uint16_t n_vertices;
    uint16_t n_edges;
    uint16_t n_output_edges;
    uint16_t n_clip_verts;
    uint16_t status;

    // a small sample of output edge records and clipped screen coordinates
    uint16_t edge_list[CAPTURE_MAX_EDGES];   // packed [v1<<8|v0]
    uint16_t edge_flags[CAPTURE_MAX_EDGES];  // flags word (VISIBLE|CLIP_V0|CLIP_V1|NEAR)
    uint16_t clip_screen[CAPTURE_MAX_CLIP_V*2]; // [sx,sy] pairs
} PluginCapture;

void vgk_plugin_capture_state(PluginCapture *cap);


// ==============================================================================
// SCENE API
// ==============================================================================
// Host-side functions to configure and read results from multi-object scene
// rendering.  The DSP scene loop is triggered by the normal 0xCAFE write to
// SCI_AICTRL0 when VGK_SCENE_ENABLE is non-zero.

// Per-object record written into the scene descriptor by the host.
typedef struct {
    uint16_t slot;        // Save-slot index (0..7)
    uint8_t  pitch;       // Object rotation pitch (sin_table index)
    uint8_t  yaw;         // Object rotation yaw   (sin_table index)
    uint8_t  roll;        // Object rotation roll   (sin_table index)
    uint8_t  scale;       // Object scale Q7 (128 = 1.0)
    int16_t  pos_x;       // World position X
    int16_t  pos_y;       // World position Y
    int16_t  pos_z;       // World position Z
} SceneObjectParams;

// Per-object metadata readable after scene processing completes.
typedef struct {
    uint16_t vert_offset;   // Cumulative vertex offset into combined array
    uint16_t edge_offset;   // Cumulative edge offset into combined array
    uint16_t clip_offset;   // Cumulative clip vert offset
    uint16_t vert_count;    // Number of screen vertices this object produced
    uint16_t edge_count;    // Number of output edges this object produced
    int16_t  centroid_z;    // Centroid view-space Z (more negative = farther)
    int16_t  aabb_min_x;   // Screen-space bounding box
    int16_t  aabb_max_x;
    int16_t  aabb_min_y;
    int16_t  aabb_max_y;
} SceneObjectMeta;

// Combined scene output summary.
typedef struct {
    uint16_t total_verts;   // Total combined screen vertices
    uint16_t total_edges;   // Total combined output edges
    uint16_t total_clips;   // Total combined clip vertices
    uint8_t  n_objects;     // Number of objects that were processed
} SceneResult;

// ---------------------------------------------------------------------------
// Scene configuration
// ---------------------------------------------------------------------------

// Enable or disable scene mode.  When enabled the next trigger_geometry_kernel()
// call processes all objects in the scene descriptor instead of a single object.
void vgk_scene_enable(bool enabled);
void vgk_scene_no_occlusion_enable(void);
void vgk_scene_no_occlusion_disable(void);

// Write the full scene descriptor: object count and per-object params.
// `n_objects` is clamped to VGK_SCENE_MAX_OBJECTS.
void vgk_scene_set_descriptor(uint8_t n_objects, const SceneObjectParams *objects);

// Write a single object record within the scene descriptor at `index`.
void vgk_scene_set_object(uint8_t index, const SceneObjectParams *obj);

// Set only the object count (useful when params are pre-written).
void vgk_scene_set_object_count(uint8_t n_objects);

// ---------------------------------------------------------------------------
// Scene results readback
// ---------------------------------------------------------------------------

// Read the combined scene totals after processing completes.
void vgk_scene_get_result(SceneResult *result);

// Read per-object metadata for a specific object index.
void vgk_scene_object_meta_get(uint8_t index, SceneObjectMeta *meta);

// Bulk-read combined screen coordinates into caller-provided buffers.
// `max_verts` limits the read; returns number of vertices actually read.
uint16_t vgk_scene_read_screen_coords(uint16_t *sx_out, uint16_t *sy_out,
                                   uint16_t max_verts);

// Bulk-read combined output edges (packed vertex pair + flags).
// `max_edges` limits the read; returns number of edges actually read.
uint16_t vgk_scene_read_output_edges(uint16_t *packed_out, uint16_t *flags_out,
                                  uint16_t max_edges);

// Bulk-read combined clip screen coordinates.
uint16_t vgk_scene_read_clip_screen(int16_t *sx_out, int16_t *sy_out,
                                 uint16_t max_clips);

// Render scene edges object-by-object with immediate draw flushes.
// Returns number of visible edges written.
// TODO: optimzed assembly
uint8_t vgk_scene_scrn_edges_get(uint8_t n_objects, const SceneObjectParams *objects,
                                uint8_t near_color, uint8_t far_color,
                                uint8_t draw_layer);

#endif // GEOMETRY_KERNEL_H
