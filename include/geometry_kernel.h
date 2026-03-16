#ifndef GEOMETRY_KERNEL_H
#define GEOMETRY_KERNEL_H
#include <stdint.h>
#include "../include/muVS1053b.h"
#include "../include/3d_math.h"
#include "../include/3d_object.h"

// VS1053b SCI register addresses
#define SCI_MODE        0x00
#define SCI_STATUS      0x01
#define SCI_VOL         0x0B  // Volume: 0x0000=max, 0xFEFE=muted (~63.5dB atten), 0xFFFF=DAC off
#define SCI_WRAMADDR    0x07
#define SCI_WRAM        0x06
#define SCI_AIADDR      0x0A
#define SCI_AICTRL0     0x0C
#define SCI_AICTRL1     0x0D
#define SCI_AICTRL2     0x0E
#define SCI_AICTRL3     0x0F
// Memory space offsets for WRAMADDR
#define IRAM_OFFSET     0x8000
#define XRAM_OFFSET     0x0000
#define YRAM_OFFSET     0x4000
// ==============================================================================
// MEMORY MAP
// ==============================================================================

// I-RAM Plugin Entry (WRAMADDR = DSP address + 0x8000)
#define HOST_GEOM_PLUGIN_ENTRY  0x8050  // DSP: 0x0050

#define VS_GEOM_MAX_VERTICES  32
#define VS_GEOM_MAX_EDGES     36
#define VS_GEOM_MAX_FACES     16
#define VS_GEOM_INVALID_FACE  0xFF
// Input Parameters (X-RAM)
#define VS_GEOM_N_VERTICES    0x1800  // Number of vertices (16-bit)

// Packed Euler inputs (8-bit indices) and Q7 scale (packed)
// Ordered consecutively so SCI autoincrement can be used to write the values.
#define VS_GEOM_OBJ_PITCH_YAW  0x1801  // packed: high byte pitch, low byte yaw
#define VS_GEOM_OBJ_ROLL_SCALE 0x1802  // packed: high byte roll, low byte scale (Q7)
#define VS_GEOM_OBJ_POS_X      0x1803  // World position X
#define VS_GEOM_OBJ_POS_Y      0x1804  // World position Y
#define VS_GEOM_OBJ_POS_Z      0x1805  // World position Z

#define VS_GEOM_CAM_PITCH_YAW  0x1806  // packed: high byte pitch, low byte yaw
#define VS_GEOM_CAM_ROLL_SCALE 0x1807  // packed: high byte roll, low byte scale (Q7)
#define VS_GEOM_CAM_POS_X      0x1808  // Camera position X
#define VS_GEOM_CAM_POS_Y      0x1809  // Camera position Y
#define VS_GEOM_CAM_POS_Z      0x180A  // Camera position Z

// Unpacked trig and scale (kernel writes these based on the packed inputs for convenience)
#define VS_GEOM_OBJ_SX        0x180B  // sin(rx) Q14
#define VS_GEOM_OBJ_CX        0x180C  // cos(rx) Q14
#define VS_GEOM_OBJ_SY        0x180D  // sin(ry) Q14
#define VS_GEOM_OBJ_CY        0x180E  // cos(ry) Q14
#define VS_GEOM_OBJ_SZ        0x180F  // sin(rz) Q14
#define VS_GEOM_OBJ_CZ        0x1810  // cos(rz) Q14
#define VS_GEOM_OBJ_SCALE     0x1811  // Global scale Q14 (converted from Q7)

#define VS_GEOM_CAM_SX        0x1812  // sin(rx) Q14
#define VS_GEOM_CAM_CX        0x1813  // cos(rx) Q14
#define VS_GEOM_CAM_SY        0x1814  // sin(ry) Q14
#define VS_GEOM_CAM_CY        0x1815  // cos(ry) Q14
#define VS_GEOM_CAM_SZ        0x1816  // sin(rz) Q14
#define VS_GEOM_CAM_CZ        0x1817  // cos(rz) Q14

// Convenience aliases
#define VS_GEOM_OBJ_TRIG      VS_GEOM_OBJ_SX
#define VS_GEOM_CAM_TRIG      VS_GEOM_CAM_SX

// Input/Output Buffers
#define VS_GEOM_INPUT_VERT    0x1818  // Input vertices (X-RAM)
#define VS_GEOM_OUTPUT_VERT   0x580C  // Output vertices (Y-RAM)

// Matrix storage (Y-RAM) - 3x4 = 12 words
#define VS_GEOM_MATRIX_BASE   0x5800  // Final composite matrix
#define VS_GEOM_TEMP_OBJ      0x1EDA  // Object matrix temp (12 words, X-RAM)
#define VS_GEOM_TEMP_CAM      0x5860  // Camera matrix temp (12 words, Y-RAM)

// Magic trigger value for SCI_AICTRL0 (PLUGIN_MODE)
#define VS_GEOM_TRIGGER_MAGIC 0xCAFE  // Write this to SCI_AICTRL0 to trigger

// Internal kernal state memory (X-RAM)
#define VS_GEOM_LR0_SAVE_PROJECT           0x1ED7  // For _project_all_vertices
#define VS_GEOM_LR0_SAVE_CLIP              0x1ED8  // For _clip_all_edges
#define VS_GEOM_LR0_SAVE_CLIP_AND_PROJECT  0x1ED9  // For _clip_and_project

// Edge List (X-RAM: placed inside free X-RAM region to avoid audio decoder memory)
// Base free X-RAM region per memory map: 0x3565..0x3FFF

#define VS_GEOM_X_FREE_BASE     0x3600  // Aligned free X-RAM base (within free_x memory define provided by VLSI)
#define VS_GEOM_N_EDGES         VS_GEOM_X_FREE_BASE         // Number of input edges (X-RAM)
#define VS_GEOM_EDGE_LIST       (VS_GEOM_X_FREE_BASE + 1)   // Edge list (packed): [v0_low | v1_high] × n (1 word per edge)
// Edge list ends at (VS_GEOM_X_FREE_BASE + 0x24) inclusive (max 36 edges) 
#define VS_GEOM_MAX_INPUT_EDGES 36  // Maximum allowed input edges (match output edge capacity)

// Per-edge adjacent face map (packed): low byte = face0, high byte = face1.
// Use 0xFF for "no adjacent face" (boundary edge).
#define VS_GEOM_EDGE_FACE_MAP   (VS_GEOM_X_FREE_BASE + 0x25) // 36 words, one per input edge

// Status and Projection Parameters 

#define VS_GEOM_STATUS_IDLE         0
#define VS_GEOM_STATUS_BUSY         1
#define VS_GEOM_STATUS_DONE         0xABCD
#define VS_GEOM_STATUS_SAVE_ERROR   0xE201
#define VS_GEOM_STATUS_LOAD_ERROR   0xE202

#define VS_GEOM_LR0_SAVE        (VS_GEOM_X_FREE_BASE + 0xA2)  // Save/restore LR0 (X-RAM)
#define VS_GEOM_STATUS          (VS_GEOM_X_FREE_BASE + 0xA3)  // 0=idle, 1=busy, 0xABCD=done (X-RAM)
#define VS_GEOM_ENABLE_PROJECT  (VS_GEOM_X_FREE_BASE + 0xA4)  // Non-zero to enable projection
#define VS_GEOM_ENABLE_CLIP     (VS_GEOM_X_FREE_BASE + 0xA5)  // Non-zero to enable clipping
#define VS_GEOM_PROJ_FOCAL      (VS_GEOM_X_FREE_BASE + 0xA6)  // Focal length (Q0, = half_w for 90 deg FOV)
#define VS_GEOM_PROJ_HALF_W     (VS_GEOM_X_FREE_BASE + 0xA7)  // Half screen width (Q0, e.g., 160)
#define VS_GEOM_PROJ_HALF_H     (VS_GEOM_X_FREE_BASE + 0xA8)  // Half screen height (Q0, e.g., 120)
#define VS_GEOM_PROJ_NEAR_Z     (VS_GEOM_X_FREE_BASE + 0xA9)  // Near plane Z (Q0, negative, e.g., -256)
#define VS_GEOM_SCRATCH0        (VS_GEOM_X_FREE_BASE + 0xAA)  // General scratch (X-RAM)
#define VS_GEOM_SCRATCH1        (VS_GEOM_X_FREE_BASE + 0xAB)  // General scratch (X-RAM)
#define VS_GEOM_LR0_SAVE2       (VS_GEOM_X_FREE_BASE + 0xAC)  // Save/restore LR0 (X-RAM)
#define VS_GEOM_LR0_SAVE3       (VS_GEOM_X_FREE_BASE + 0xAD)  // Save/restore LR0 (X-RAM)
#define VS_GEOM_ENABLE_HIDDEN_LINE (VS_GEOM_X_FREE_BASE + 0xAE) // Non-zero enables face-normal hidden-line culling
#define VS_GEOM_N_FACES            (VS_GEOM_X_FREE_BASE + 0xAF) // Number of face normals provided

// Optional hidden-line buffers in free X-RAM.
#define VS_GEOM_FACE_VISIBILITY   (VS_GEOM_X_FREE_BASE + 0x1A2) // 16 words: per-face visibility (0/1)
#define VS_GEOM_FACE_NORMALS      (VS_GEOM_X_FREE_BASE + 0x1B2) // 48 words: up to 16 Q14 normals (3 words each)

// Screen Coordinates Output (X-RAM)
#define VS_GEOM_SCREEN_COORDS_X  (VS_GEOM_X_FREE_BASE + 0xC0) // X-RAM: 64 words for [sx,sy] × up to 32 verts
#define VS_GEOM_SCREEN_COORDS    VS_GEOM_SCREEN_COORDS_X      // Alias

// Clipping Output (X-RAM)
#define VS_GEOM_N_CLIP_VERTS_X  (VS_GEOM_X_FREE_BASE + 0x170) // X-RAM: count (1 word)
#define VS_GEOM_CLIP_VERTS_X    (VS_GEOM_X_FREE_BASE + 0x171) // X-RAM: clip verts buffer (48 words)
#define VS_GEOM_N_CLIP_VERTS    VS_GEOM_N_CLIP_VERTS_X
#define VS_GEOM_CLIP_VERTS      VS_GEOM_CLIP_VERTS_X
#define VS_GEOM_CLIP_SCREEN_X   (VS_GEOM_X_FREE_BASE + 0x100) // X-RAM: 32 words for [sx,sy] × up to 16 verts
#define VS_GEOM_CLIP_SCREEN     VS_GEOM_CLIP_SCREEN_X
#define VS_GEOM_N_OUTPUT_EDGES_X       (VS_GEOM_X_FREE_BASE + 0x120) // X-RAM: count (1 word)
#define VS_GEOM_OUTPUT_EDGE_FLAGS_X    (VS_GEOM_X_FREE_BASE + 0x121) // X-RAM: flags packed 2/word (18 words for 36 edges)
#define VS_GEOM_OUTPUT_EDGE_PACKED_X   (VS_GEOM_X_FREE_BASE + 0x133) // X-RAM: packed_v0v1 per edge (36 words)
#define VS_GEOM_N_OUTPUT_EDGES         VS_GEOM_N_OUTPUT_EDGES_X
#define VS_GEOM_OUTPUT_EDGE_FLAGS      VS_GEOM_OUTPUT_EDGE_FLAGS_X
#define VS_GEOM_OUTPUT_EDGE_PACKED     VS_GEOM_OUTPUT_EDGE_PACKED_X
// Flag word[i] = (flags[2i+1] << 8) | flags[2i]  (low byte = even edge, high byte = odd edge)

// Save slot layout — host writes geometry directly to VS_GEOM_SAVE_AREA_X + slot * VS_GEOM_SAVE_SLOT_SIZE.
// The DSP _load_object copies from the chosen slot to the active working area on each trigger.
// Slot field offsets match the order used by the DSP _load_object function in geometry5.s.
#define VS_GEOM_SAVE_AREA_X        (VS_GEOM_X_FREE_BASE + 0x200)  // Base of 8-slot save area (X-RAM)
#define VS_GEOM_SAVE_SLOT_SIZE     0x100                           // Words per slot
#define VS_GEOM_SAVE_SLOT_COUNT    8                               // Slots 0..7
// Offsets within a slot (add to slot base = VS_GEOM_SAVE_AREA_X + slot * VS_GEOM_SAVE_SLOT_SIZE)
#define VS_GEOM_SLOT_N_VERTICES    0x00  // 1 word: vertex count
#define VS_GEOM_SLOT_INPUT_VERT    0x01  // 96 words: [vx,vy,vz] x up to 32 verts
#define VS_GEOM_SLOT_N_EDGES       0x61  // 1 word: edge count (offset 97 = 1 + 96)
#define VS_GEOM_SLOT_EDGE_LIST     0x62  // 36 words: packed [v1|v0] per edge
// Slot field: N_FACES 
#define VS_GEOM_SLOT_N_FACES       0x86  // 1 word: face count
#define VS_GEOM_SLOT_EDGE_FACE_MAP 0x87  // 36 words: packed [face1|face0] per edge
#define VS_GEOM_SLOT_FACE_NORMALS  0xAB  // 48 words: [nx,ny,nz] x up to 16 faces (offset 171)

// Edge flags for output
#define VS_GEOM_EDGE_VISIBLE    0x0001  // Edge is visible
#define VS_GEOM_EDGE_CLIP_V0    0x0002  // V0 was clipped (new vertex created)
#define VS_GEOM_EDGE_CLIP_V1    0x0004  // V1 was clipped (new vertex created)
#define VS_GEOM_EDGE_NEAR       0x0008  // Edge midpoint depth classified as near
#define VS_GEOM_EDGE_CULLED     0x0000  // Edge is culled

// ==============================================================================
// SCENE MEMORY MAP (Decoder X-RAM: 0x0400..0x0A3F)
// ==============================================================================
// The scene feature uses decoder X-RAM regions that are normally occupied by
// audio decoders.  Since this is a graphics-only application the memory is
// available for scene descriptor and combined output accumulation.

// Scene Descriptor (X-RAM 0x0400)
#define VS_GEOM_SCENE_BASE          0x0400
#define VS_GEOM_SCENE_ENABLE        (VS_GEOM_SCENE_BASE + 0x00)   // Non-zero = scene mode
#define VS_GEOM_SCENE_N_OBJECTS     (VS_GEOM_SCENE_BASE + 0x01)   // Number of objects (1..8)
#define VS_GEOM_SCENE_MAX_OBJECTS   8                               // Maximum objects per scene
#define VS_GEOM_SCENE_OBJ_PARAMS    (VS_GEOM_SCENE_BASE + 0x02)   // Per-object records start
#define VS_GEOM_SCENE_OBJ_STRIDE    6                               // Words per object record

// Per-object record layout (6 words each, base + obj_idx * stride):
//   +0: slot_idx      Save slot index (0..7)
//   +1: pitch_yaw     Packed: hi=pitch, lo=yaw
//   +2: roll_scale    Packed: hi=roll, lo=scale_q7
//   +3: pos_x         World position X
//   +4: pos_y         World position Y
//   +5: pos_z         World position Z
// Total scene descriptor: 2 + 8*6 = 50 words (0x0400..0x0431)

// Scene Per-Object Metadata (X-RAM 0x0432)
#define VS_GEOM_SCENE_META_BASE     (VS_GEOM_SCENE_BASE + 0x32)
#define VS_GEOM_SCENE_VERT_OFFSET   (VS_GEOM_SCENE_META_BASE + 0x00)  // 8 words: cumulative vert offset
#define VS_GEOM_SCENE_EDGE_OFFSET   (VS_GEOM_SCENE_META_BASE + 0x08)  // 8 words: cumulative edge offset
#define VS_GEOM_SCENE_DEPTH         (VS_GEOM_SCENE_META_BASE + 0x10)  // 8 words: centroid depth (view Z)
#define VS_GEOM_SCENE_AABB          (VS_GEOM_SCENE_META_BASE + 0x18)  // 32 words: 8 x [min_x,max_x,min_y,max_y]
#define VS_GEOM_SCENE_AABB_STRIDE   4                                   // Words per AABB record
#define VS_GEOM_SCENE_VERT_COUNT    (VS_GEOM_SCENE_META_BASE + 0x38)  // 8 words: per-object vert count
#define VS_GEOM_SCENE_EDGE_COUNT    (VS_GEOM_SCENE_META_BASE + 0x40)  // 8 words: per-object output edge count
#define VS_GEOM_SCENE_TOTAL_VERTS   (VS_GEOM_SCENE_META_BASE + 0x48)  // 1 word: total combined verts
#define VS_GEOM_SCENE_TOTAL_EDGES   (VS_GEOM_SCENE_META_BASE + 0x49)  // 1 word: total combined edges
#define VS_GEOM_SCENE_CLIP_OFFSET   (VS_GEOM_SCENE_META_BASE + 0x4A)  // 8 words: cumulative clip vert offset
#define VS_GEOM_SCENE_TOTAL_CLIPS   (VS_GEOM_SCENE_META_BASE + 0x52)  // 1 word: total combined clip verts
#define VS_GEOM_SCENE_SORT_ORDER    (VS_GEOM_SCENE_META_BASE + 0x53)  // 8 words: depth-sorted object indices
#define VS_GEOM_SCENE_FLAGS         (VS_GEOM_SCENE_META_BASE + 0x5B)  // 0x048D: Scene mode flags
#define VS_GEOM_SCENE_FLAG_NO_OCCLUSION  0x0001                        // Skip AABB sort/cull pass; per-object hidden line still runs
// Metadata ends at 0x048D

// Scene Output Accumulation (Decoder X-RAM: 0x0500)
#define VS_GEOM_SCENE_SCREEN_COORDS 0x0500  // 512 words: 256 verts x 2 [sx,sy]
#define VS_GEOM_SCENE_OUTPUT_EDGES  0x0700  // 576 words: 288 edges x 2 [packed,flags]
#define VS_GEOM_SCENE_CLIP_SCREEN   0x0940  // 128 words: 64 clip verts x 2 [sx,sy]

// Scene capacity limits
#define VS_GEOM_SCENE_MAX_VERTS     256     // Max combined screen vertices
#define VS_GEOM_SCENE_MAX_EDGES     288     // Max combined output edges
#define VS_GEOM_SCENE_MAX_CLIPS     64      // Max combined clip vertices

extern const int16_t sin_table[256];
// When true, get_screen_edges_full_asm uses a single flat color (object_color
// low byte) and skips reading the OUTPUT_EDGES buffer entirely when n_clip==0
// and hidden-line removal is also disabled.  Near/far depth coloring is lost.
extern bool no_near_far_coloring;
// When true, hidden-line removal is active; fast path is suppressed.
extern bool g_hidden_line_active;
uint8_t get_screen_edges(Model3D * model, uint8_t color);
uint8_t get_screen_edges_with_depth(Model3D *model, uint8_t layer);
// geometry kernel functions
void setup_object_params(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale,
                         int16_t pos_x, int16_t pos_y, int16_t pos_z);
void setup_camera_params(uint8_t pitch, uint8_t yaw, uint8_t roll,
                                 int16_t pos_x, int16_t pos_y, int16_t pos_z);
void setup_model_vertices(const Model3D* model, uint8_t slot);
void setup_model_edges(const Model3D* model, uint8_t slot);
void setup_model_hidden_line(const Model3D* model, uint8_t slot);
void geometry_kernel_apply_hidden_line(const Model3D* model, uint8_t slot);
void hidden_line_enable(void);
void hidden_line_disable(void);
void setup_projection_params(int16_t focal, int16_t half_w, int16_t half_h,
                         int16_t near_z);
void trigger_geometry_kernel(void);
void geometry_kernel_reset(void);
uint8_t geometry_kernel_wait_complete(uint16_t timeout_ms);
bool geometry_kernel_save(uint16_t slot);
bool geometry_kernel_load(uint16_t slot);
void load_model_to_plugin(const Model3D* model, uint8_t slot);
void setup_object_angle_scale(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale);
void setup_object_pos(int16_t pos_x, int16_t pos_y, int16_t pos_z);
void get_view_z(int16_t * out_zbuffer, uint8_t max_verts);
void setup_projection_disable(void);
void setup_projection_enable(void);

// -----------------------------------------------------------------------------
// Plugin state capture for host-side debugging
// -----------------------------------------------------------------------------
// The host can call `capture_plugin_state()` after each kernel invocation to
// snapshot internal memory (camera input, composed matrix, projection params,
// and edge lists) so rendered output can be correlated with plugin state.  The
// data is read in a fixed order; see capture_template.md for sample CSV layout.

#define CAPTURE_MAX_EDGES VS_GEOM_MAX_EDGES
#define CAPTURE_MAX_CLIP_V   VS_GEOM_MAX_VERTICES

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

void capture_plugin_state(PluginCapture *cap);
// Mute the VS1053b DAC. Since the plugin runs without preemption and this
// application produces no audio, draining the FIFO causes audible clicks.
// Writing 0xFEFE to SCI_VOL applies ~63.5 dB attenuation without powering
// down the DAC (0xFFFF would cause a click on transition).

// custom file write helper
int16_t kernelWriteC(uint8_t fd, void *buf, uint16_t nbytes);

void vs1053_mute_dac(void);
void vs1053_disable_dac_interrupt(void);
void vs1053_enable_dac_interrupt(void);
// ==============================================================================
// SCENE API
// ==============================================================================
// Host-side functions to configure and read results from multi-object scene
// rendering.  The DSP scene loop is triggered by the normal 0xCAFE write to
// SCI_AICTRL0 when VS_GEOM_SCENE_ENABLE is non-zero.

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
void scene_enable(void);
void scene_disable(void);
void scene_no_occlusion_enable(void);
void scene_no_occlusion_disable(void);

// Write the full scene descriptor: object count and per-object params.
// `n_objects` is clamped to VS_GEOM_SCENE_MAX_OBJECTS.
void scene_set_descriptor(uint8_t n_objects, const SceneObjectParams *objects);

// Write a single object record within the scene descriptor at `index`.
void scene_set_object(uint8_t index, const SceneObjectParams *obj);

// Set only the object count (useful when params are pre-written).
void scene_set_object_count(uint8_t n_objects);

// ---------------------------------------------------------------------------
// Scene results readback
// ---------------------------------------------------------------------------

// Read the combined scene totals after processing completes.
void scene_get_result(SceneResult *result);

// Read per-object metadata for a specific object index.
void scene_get_object_meta(uint8_t index, SceneObjectMeta *meta);

// Bulk-read combined screen coordinates into caller-provided buffers.
// `max_verts` limits the read; returns number of vertices actually read.
uint16_t scene_read_screen_coords(uint16_t *sx_out, uint16_t *sy_out,
                                   uint16_t max_verts);

// Bulk-read combined output edges (packed vertex pair + flags).
// `max_edges` limits the read; returns number of edges actually read.
uint16_t scene_read_output_edges(uint16_t *packed_out, uint16_t *flags_out,
                                  uint16_t max_edges);

// Bulk-read combined clip screen coordinates.
uint16_t scene_read_clip_screen(int16_t *sx_out, int16_t *sy_out,
                                 uint16_t max_clips);

// Convenience: render scene edges object-by-object with immediate draw flushes.
// This avoids accumulating the full scene edge set in the host line-list.
//
// When the caller supplies `near_color` and `far_color`, the kernel reads each
// object's centroid view-Z from the VS_GEOM_SCENE_DEPTH table and computes a
// midpoint between the nearest and farthest object.  All edges belonging to an
// object closer than this midpoint are drawn with `near_color`; others use
// `far_color`.  This allows a crude depth-based colouring of scene geometry.
//
// Returns number of visible edges written.
uint8_t scene_get_screen_edges(uint8_t n_objects, const SceneObjectParams *objects,
                                uint8_t near_color, uint8_t far_color,
                                uint8_t draw_layer);

void hidden_line_disable(void);

/* Register a callback that geometry_kernel_wait_complete() calls on every
 * polling iteration instead of the default nop-delay.  Pass NULL to restore
 * the default behaviour.  Intended for audio tick servicing during DSP waits
 * so that the audio loop is not starved when multiple objects are rendered. */
void geometry_kernel_set_yield_cb(void (*cb)(void));
void geometry_kernel_yield(void);

#endif // GEOMETRY_KERNEL_H
