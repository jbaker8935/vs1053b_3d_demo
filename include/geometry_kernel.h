/**
 * @file geometry_kernel.h
 * @brief VS1053b Geometry Kernel (VGK) host-side API.
 *
 * The VS1053b Geometry Kernel is a DSP plugin that offloads 3D wireframe
 * geometry processing from the host CPU.  It performs:
 *  - Euler-angle object and camera transformation (8-bit angle LUT, Q14 trig)
 *  - Perspective projection (Q0 integer focal / half-width / half-height)
 *  - Near-plane clipping
 *  - Optional face-normal hidden-line removal (back-face culling)
 *  - Optional near/far edge marking
 *  - Scene mode: up to @ref VGK_SCENE_MAX_OBJECTS objects per trigger with
 *    AABB occlusion and painter's-order depth sort
 *  - Output: edge list with screen-space coordinates and optional per-edge
 *   near/far flags for edge coloring
 * The Host API supports configuring the kernel, uploading geometry, and rendering results
 * 
 *
 * ### Typical single-object render loop
 * @code
 *   vgk_plugin_init();                          // once at startup
 *   vgk_projection_params(240, 160, 120, -128); // once; adjust to taste
 *   vgk_model_save(&g_model_cube, 0);           // load model into slot 0
 *
 *   while (running) {
 *       vgk_cam_params(0, angle, 0, 0, 0, -512);
 *       vgk_model_select(0);                    // activate slot 0
 *       vgk_obj_params(0, angle, 0, 128, 0, 0, 0);
 *       vgk_trigger();
 *       vgk_wait_complete(1000);
 *       vgk_scrn_edges_render(layer, color);
 *   }
 * @endcode
 *
 * @note All angle parameters are **8-bit unsigned indices** into the 256-entry
 *       @ref sin_table — one full rotation = 256 units.  Scale is Q7
 *       (128 = 1.0).  Coordinates are signed 16-bit integers.
 */
#ifndef GEOMETRY_KERNEL_H
#define GEOMETRY_KERNEL_H
#include <stdint.h>
#include "../include/3d_math.h"
#include "../include/3d_object.h"

/* VS1053b register definitions and host helpers */
#include "../include/vs1053b.h"

// ==============================================================================
/** @defgroup vgk_memmap VGK Memory Map
 *  @brief DSP X/Y-RAM addresses used by the geometry kernel.
 *
 *  These constants are passed to `vs1053_mem_write()` / `vs1053_mem_read()`
 *  and are not normally needed by application code.  They are exposed here
 *  for advanced users and diagnostics.
 * @{
 */
// ==============================================================================

// I-RAM Plugin Entry (WRAMADDR = DSP address + 0x8000)
#define HOST_GEOM_PLUGIN_ENTRY  0x8050  ///< DSP plugin entry point (I-RAM, DSP addr 0x0050).

/** @name Geometry limits
 *  Hard limits enforced by the kernel and save-slot layout.
 * @{ */
#define VGK_MAX_VERTICES  60  ///< Maximum vertices per model (per slot).
#define VGK_MAX_EDGES     90  ///< Maximum edges per model (per slot).
#define VGK_MAX_FACES     32  ///< Maximum faces per model (for hidden-line data).
#define VGK_INVALID_FACE  0xFF ///< Sentinel: edge has no adjacent face (same as @ref GEOM3D_INVALID_FACE).
/** @} */
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

// Stable SCI-readable plugin probe words and direct-slot control words.
/** @name Plugin probe / identification words
 * @{ */
#define VGK_PLUGIN_SIGNATURE_VALUE  0x4750  ///< Expected value at VGK_PLUGIN_SIGNATURE ('GP').
/** Plugin version packed as (major << 8 | minor).  Read with @ref vgk_plugin_version(). */
#define VGK_PLUGIN_VERSION_VALUE    0x0100  ///< Current plugin version: v1.0.
#define VGK_PLUGIN_SIGNATURE        0x1818  ///< X-RAM: plugin presence signature word.
#define VGK_PLUGIN_VERSION          0x1819  ///< X-RAM: plugin version word (high=major, low=minor).
/** @} */
#define VGK_ACTIVE_SLOT_BASE        0x181A
#define VGK_ACTIVE_INPUT_VERT_BASE  0x181B
#define VGK_ACTIVE_EDGE_LIST_BASE   0x181C
#define VGK_ACTIVE_EDGE_FACE_MAP_BASE 0x181D
#define VGK_ACTIVE_FACE_NORMALS_BASE  0x181E

// Input/Output Buffers
#define VGK_INPUT_VERT    0x0F00  // Input vertices (X-RAM)
#define VGK_OUTPUT_VERT   0x500C  // Output vertices (Y-RAM)

// Matrix storage (Y-RAM) - 3x4 = 12 words
#define VGK_MATRIX_BASE   0x5000  // Final composite matrix
#define VGK_TEMP_OBJ      0x1EDA  // Object matrix temp (12 words, X-RAM)
#define VGK_TEMP_CAM      0x5860  // Camera matrix temp (12 words, Y-RAM)

/** @name Trigger / Status constants
 * @{ */
/** @brief Magic value written to SCI_AICTRL0 to trigger a kernel render pass.
 *  @see vgk_trigger() */
#define VGK_TRIGGER_MAGIC 0xCAFE
/** @} */

// Internal kernal state memory (X-RAM)
#define VGK_LR0_SAVE_PROJECT           0x1ED7  // For _project_all_vertices
#define VGK_LR0_SAVE_CLIP              0x1ED8  // For _clip_all_edges
#define VGK_LR0_SAVE_CLIP_AND_PROJECT  0x1ED9  // For _clip_and_project

// Edge List 
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

/** @name Kernel status codes
 *  Returned by @ref vgk_status() and polled internally by @ref vgk_wait_complete().
 * @{ */
#define VGK_STATUS_IDLE         0       ///< Kernel idle (not yet triggered).
#define VGK_STATUS_BUSY         1       ///< Kernel currently processing.
#define VGK_STATUS_DONE         0xABCD  ///< Kernel finished successfully.
#define VGK_STATUS_SAVE_ERROR   0xE201  ///< Slot save operation failed. (deprecated)
#define VGK_STATUS_LOAD_ERROR   0xE202  ///< Slot load / model-select failed.
/** @} */

#define VGK_LR0_SAVE        (VGK_X_FREE_BASE + 0xB5)  // Save/restore LR0 (X-RAM)
#define VGK_STATUS          (VGK_X_FREE_BASE + 0xB6)  ///< Kernel status register (X-RAM): 0=idle, 1=busy, 0xABCD=done.
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
#define VGK_CURRENT_SLOT       (VGK_X_FREE_BASE + 0xC4) // Current slot index: written by load handler; overwritten per-object in scene loop

// Optional hidden-line buffers in free X-RAM.
#define VGK_FACE_VISIBILITY   (VGK_X_FREE_BASE + 0x146) // 32 words: per-face visibility (0/1/2); ends +0x165
#define VGK_FACE_NORMALS      (VGK_X_FREE_BASE + 0x166) // 96 words: up to 32 Q14 normals (3 words each); ends +0x1C5
#define VGK_FACE_REP_VERT     (VGK_X_FREE_BASE + 0x1C6) // 32 words: per-face representative vertex index; ends +0x1E5

// Screen Coordinates Output (X-RAM)
#define VGK_SCREEN_COORDS_X  (VGK_X_FREE_BASE + 0xC6) // X-RAM: 128 words for [sx,sy] × up to 60 verts
#define VGK_SCREEN_COORDS    VGK_SCREEN_COORDS_X      // Alias

// Save slot layout: single contiguous block in AAC decoder X-RAM (freed with DAC disabled).
//   0x2000..0x2FFF: slots 0..7 (8 × 512 = 4096 words)
// Region 0x2000..0x2FFF is within AAC decoder X-RAM (0x1F00..0x3565),
// fully accessible via SCI, and clear of stack/parametric/sysvar areas.
/** @name Save slot constants
 * @{ */
/** @brief Base X-RAM address of the 8-slot model save area (0x2000..0x2FFF). */
#define VGK_SAVE_AREA             0x2000
#define VGK_SAVE_SLOT_SIZE        0x200   ///< Words per save slot (512).
#define VGK_SAVE_SLOT_COUNT       8       ///< Total save slots (indices 0..7).
#define VGK_SAVE_AREA_END         (VGK_SAVE_AREA + (VGK_SAVE_SLOT_SIZE * VGK_SAVE_SLOT_COUNT) - 1)  ///< Last word address of save area (0x2FFF).
/** @} */

#define VGK_INPUT_VERT_END        (VGK_INPUT_VERT + (VGK_MAX_VERTICES * 3) - 1)

// Offsets within a slot (add to slot base = VGK_SAVE_AREA + slot * VGK_SAVE_SLOT_SIZE)
// Note: when saving geometry to vs1053 memory the host application writes directly to the 
// slot and offsets within the slot.
#define VGK_SLOT_N_VERTICES    0x00
#define VGK_SLOT_INPUT_VERT    0x01
#define VGK_SLOT_N_EDGES       (VGK_SLOT_INPUT_VERT + (VGK_MAX_VERTICES * 3))
#define VGK_SLOT_EDGE_LIST     (VGK_SLOT_N_EDGES + 1)
#define VGK_SLOT_N_FACES       (VGK_SLOT_EDGE_LIST + VGK_MAX_INPUT_EDGES)
#define VGK_SLOT_EDGE_FACE_MAP (VGK_SLOT_N_FACES + 1)
#define VGK_SLOT_FACE_NORMALS  (VGK_SLOT_EDGE_FACE_MAP + VGK_MAX_INPUT_EDGES)

/** @name Output edge flags
 *  Bit flags associated with each edge after processing.  Edges saved to the output stream and their optional
 *  descriptor values are based on these bit flags.
 * @{ */
#define VGK_EDGE_VISIBLE    0x0001  ///< Edge survived clipping and is visible.
#define VGK_EDGE_CLIP_V0    0x0002  ///< Vertex V0 was replaced by a clip-plane intersection.
#define VGK_EDGE_CLIP_V1    0x0004  ///< Vertex V1 was replaced by a clip-plane intersection.
#define VGK_EDGE_NEAR       0x0008  ///< Edge midpoint is classified as *near* (closer to camera).
#define VGK_EDGE_CULLED     0x0000  ///< Edge was culled (not visible); all flag bits clear.
/** @} */

// ==============================================================================
// SCENE MEMORY MAP (Decoder X-RAM: 0x0400..0x0E49)
// ==============================================================================
// The scene feature uses decoder X-RAM regions that are normally occupied by
// audio decoders. Since this is a graphics-only application, the memory is
// available for the expanded scene descriptor, metadata, relocated edge stream,
// and scene scratch region.

/** @name Scene mode constants
 * @{ */
#define VGK_SCENE_BASE          0x0400   ///< X-RAM base of scene descriptor block.
#define VGK_SCENE_ENABLE        (VGK_SCENE_BASE + 0x00)   ///< Non-zero = scene mode active.
#define VGK_SCENE_N_OBJECTS     (VGK_SCENE_BASE + 0x01)   ///< Number of objects in current scene (1..32).
/** @brief Maximum number of objects in a single scene. */
#define VGK_SCENE_MAX_OBJECTS   32
#define VGK_SCENE_OBJ_PARAMS    (VGK_SCENE_BASE + 0x02)   ///< Start of per-object parameter records.
#define VGK_SCENE_OBJ_STRIDE    6                           ///< Words per object parameter record.

// Per-object record layout (6 words each, base + obj_idx * stride):
//   +0: slot_idx      Save slot index (0..7)
//   +1: pitch_yaw     Packed: hi=pitch, lo=yaw
//   +2: roll_scale    Packed: hi=roll, lo=scale_q7
//   +3: pos_x         World position X
//   +4: pos_y         World position Y
//   +5: pos_z         World position Z
// Total scene descriptor: 2 + 32*6 = 194 words (0x0400..0x04C1)

// Scene Per-Object Metadata (X-RAM 0x04C2 for 32-object layout)
#define VGK_SCENE_META_BASE     (VGK_SCENE_OBJ_PARAMS + (VGK_SCENE_MAX_OBJECTS * VGK_SCENE_OBJ_STRIDE))
#define VGK_SCENE_VERT_OFFSET   (VGK_SCENE_META_BASE + 0x00)  // 32 words: cumulative vert offset
#define VGK_SCENE_EDGE_OFFSET   (VGK_SCENE_VERT_OFFSET + VGK_SCENE_MAX_OBJECTS)  // 32 words: cumulative edge offset
#define VGK_SCENE_DEPTH         (VGK_SCENE_EDGE_OFFSET + VGK_SCENE_MAX_OBJECTS)  // 32 words: nearest-face depth (max view Z, least negative = closest to camera)
#define VGK_SCENE_AABB          (VGK_SCENE_DEPTH + VGK_SCENE_MAX_OBJECTS)  // 128 words: 32 x [min_x,max_x,min_y,max_y]
#define VGK_SCENE_AABB_STRIDE   4                           // Words per AABB record
#define VGK_SCENE_VERT_COUNT    (VGK_SCENE_AABB + (VGK_SCENE_MAX_OBJECTS * VGK_SCENE_AABB_STRIDE))  // 32 words: per-object vert count
#define VGK_SCENE_EDGE_COUNT    (VGK_SCENE_VERT_COUNT + VGK_SCENE_MAX_OBJECTS)  // 32 words: per-object output edge count
#define VGK_SCENE_TOTAL_VERTS   (VGK_SCENE_EDGE_COUNT + VGK_SCENE_MAX_OBJECTS)  // 1 word: total combined verts
#define VGK_SCENE_TOTAL_EDGES   (VGK_SCENE_TOTAL_VERTS + 1)  // 1 word: total combined edges
#define VGK_SCENE_CLIP_OFFSET   (VGK_SCENE_TOTAL_EDGES + 1)  // 32 words: cumulative clip vert offset
#define VGK_SCENE_TOTAL_CLIPS   (VGK_SCENE_CLIP_OFFSET + VGK_SCENE_MAX_OBJECTS)  // 1 word: total combined clip verts
#define VGK_SCENE_SORT_ORDER    (VGK_SCENE_TOTAL_CLIPS + 1)  // 32 words: depth-sorted object indices
#define VGK_SCENE_FLAGS         (VGK_SCENE_SORT_ORDER + VGK_SCENE_MAX_OBJECTS)  // Scene mode flags (bit0=NO_OCCLUSION, bit1=NO_SORT)
#define VGK_SCENE_FLAG_NO_OCCLUSION  0x0001  ///< Disable AABB occlusion cull/sort; per-object hidden-line still active.
#define VGK_SCENE_FLAG_NO_SORT       0x0002  ///< Disable depth sort; SORT_ORDER array is left unchanged.
/** @} */  // end scene mode constants

/** @name Edge stream / descriptor constants
 * @{ */
/** @brief Maximum edges in the output edge stream (512 × 4 words = 2048 words). */
#define VGK_EDGE_STREAM_MAX    512
#define VGK_EDGE_STREAM_BASE   0x0640   ///< X-RAM: start of edge stream (after 32-object scene metadata).
#define VGK_N_STREAM_EDGES     0x063F   ///< X-RAM: total visible edge count; read before streaming.

/** @name Edge descriptor bit fields (when VGK_ENABLE_DESCRIPTOR != 0)
 * @{ */
#define VGK_EDESC_NEAR_BIT     0x8000   ///< Bit 15: edge midpoint is on the near (camera) side.
#define VGK_EDESC_SLOT_SHIFT   8        ///< Shift to extract the 7-bit save-slot field from a descriptor word.
#define VGK_EDESC_SLOT_MASK    0x7F     ///< Mask for the 7-bit save-slot field (after shifting).
#define VGK_EDESC_IDX_MASK     0xFF     ///< Mask for the 8-bit per-slot edge index (bits 0..7).
/** @} */
/** @} */  // end edge stream / descriptor
/** @} */  // end vgk_memmap

// =============================================================================
/** @defgroup vgk_init Initialization
 *  @brief Plugin load and global configuration.
 * @{ */
// =============================================================================

/**
 * @brief Load the geometry kernel plugin into the VS1053b and initialise it.
 *
 * Must be called once at startup before any other VGK function.  The function:
 *  1. Mutes the DAC and disables DAC interrupts (geometry mode reuses decoder
 *     RAM, so audio must be silenced first).
 *  2. Uploads the embedded plugin binary to the VS1053b via SCI.
 *  3. Enables perspective projection and near-plane clipping.
 *  4. Boosts the VS1053b clock to ×4.5 (max CLKI ≈ 55.3 MHz).
 *
 * @note For demonstration code, the plugin binary is embedded at link-time just below bitmap page 2
 *       (0x3F000).  After loading, that CPU-side memory region is available
 *       for other use.  An application could also load the plugin from an external file source.
 */
void vgk_plugin_init(void);

/**
 * @brief Reset the kernel status word to idle.
 *
 * Clears VGK_STATUS to 0 so the next @ref vgk_wait_complete() call starts
 * polling from a known clean state.  Called automatically by
 * @ref vgk_model_select().
 */
void vgk_reset(void);

/**
 * @brief Query the version of the loaded plugin.
 *
 * Reads the signature word from DSP memory to confirm the plugin is present,
 * then returns the packed version number.
 *
 * @return 0 if the plugin signature is absent (plugin not loaded).
 * @return Packed version word: high byte = major version, low byte = minor
 *         version (e.g. 0x0100 = v1.0).
 */
uint16_t vgk_plugin_version(void);

/**
 * @brief Set perspective projection parameters.
 *
 * Only needs to be called once (or when the viewport geometry changes).
 *
 * Recommended starting values for a 320×240 display:
 * @code
 *   vgk_projection_params(240, 160, 120, -128);
 * @endcode
 * Reducing @p focal or increasing @p near_z magnitude increases perspective
 * distortion but also raises the risk of near-plane clipping artifacts.
 *
 * @param focal   Focal length in pixels.  Set equal to @p half_w for a 90°
 *                horizontal field of view.
 * @param half_w  Half the screen width in pixels (e.g. 160 for 320-wide).
 * @param half_h  Half the screen height in pixels (e.g. 120 for 240-tall).
 * @param near_z  Near clipping plane Z in world units (must be negative,
 *                e.g. –128).  Vertices closer to the camera than this plane
 *                are clipped.
 */
void vgk_projection_params(int16_t focal, int16_t half_w, int16_t half_h,
                         int16_t near_z);

/** @} */ // end vgk_init

// =============================================================================
/** @defgroup vgk_slots Model Slot Management
 *  @brief Upload 3D model data to DSP save slots.
 *
 *  The kernel stores up to @ref VGK_SAVE_SLOT_COUNT (8) models in on-chip DSP
 *  X-RAM save slots (0x2000–0x2FFF).  A model must be uploaded before it can
 *  be selected and rendered.  After uploading, the CPU-side @ref Geom3D data
 *  may be freed.
 * @{ */
// =============================================================================

/**
 * @brief Upload a complete object (vertices, edges, hidden-line data) to a
 *        save slot.
 *
 * Convenience wrapper that calls @ref vgk_model_vertices(),
 * @ref vgk_model_edges(), and @ref vgk_model_hidden_line() in sequence, then
 * registers the @ref Object3D color pointer via @ref vgk_slot_object().
 * This is the primary function for loading a model — use the individual
 * sub-functions only when partial updates are required.
 *
 * @param obj   Pointer to the object descriptor (geometry + color data).
 * @param slot  Destination save slot (0..@ref VGK_SAVE_SLOT_COUNT – 1).
 */
void vgk_model_save(const Object3D *obj, uint8_t slot);

/**
 * @brief Upload vertex array data for a model to a save slot.
 *
 * Writes vertex_count and the X/Y/Z interleaved vertex triples into the slot's
 * reserved region in DSP X-RAM.  Called internally by @ref vgk_model_save().
 *
 * @param model  Source geometry descriptor.
 * @param slot   Destination save slot (0..@ref VGK_SAVE_SLOT_COUNT – 1).
 */
void vgk_model_vertices(const Geom3D *model, uint8_t slot);

/**
 * @brief Upload edge list data for a model to a save slot.
 *
 * Writes edge_count and the packed edge pairs (v1<<8 | v0) into the slot's
 * edge-list region in DSP X-RAM.  Called internally by @ref vgk_model_save().
 *
 * @param model  Source geometry descriptor.
 * @param slot   Destination save slot (0..@ref VGK_SAVE_SLOT_COUNT – 1).
 */
void vgk_model_edges(const Geom3D *model, uint8_t slot);

/**
 * @brief Upload face-normal and edge-adjacency data for hidden-line removal.
 *
 * Writes the edge-face adjacency map and face normal triples (Q14) into the
 * slot's hidden-line region in DSP X-RAM.  If the model's face data is absent
 * (NULL pointers or face_count == 0), the slot's face count is cleared to
 * disable hidden-line removal for that slot.  Called internally by
 * @ref vgk_model_save().
 *
 * @param model  Source geometry descriptor (face normals may be NULL).
 * @param slot   Destination save slot (0..@ref VGK_SAVE_SLOT_COUNT – 1).
 */
void vgk_model_hidden_line(const Geom3D *model, uint8_t slot);

/**
 * @brief Register an @ref Object3D color pointer for a slot.
 *
 * Stores @p obj in an internal table indexed by @p slot so that
 * @ref vgk_scrn_edges_render() can look up per-object and per-edge colors
 * from the descriptor words emitted by the kernel.  Called automatically by
 * @ref vgk_model_save().  Call explicitly only if the color arrays change
 * after the initial upload.
 *
 * @param obj   Object whose color data to associate with the slot.
 * @param slot  Save slot index (0..@ref VGK_SAVE_SLOT_COUNT – 1).
 */
void vgk_slot_object(const Object3D *obj, uint8_t slot);

/**
 * @brief Activate a save slot as the current object for the next render pass.
 *
 * Writes the slot index to the kernel's active-slot register and waits for
 * the DSP to acknowledge.  The selected slot's geometry
 * remains active until the next call to this function.  The model does not
 * need to be re-selected between frames if only transformation parameters are
 * changing.
 *
 * @param slot  Save slot to activate (0..@ref VGK_SAVE_SLOT_COUNT – 1).
 * @return true  on success (slot loaded and acknowledged).
 * @return false on timeout or error.
 */
bool vgk_model_select(uint16_t slot);

/** @} */ // end vgk_slots

// =============================================================================
/** @defgroup vgk_transform Object and Camera Transform
 *  @brief Set per-frame object and camera pose parameters.
 *
 *  All angle parameters are **8-bit unsigned indices** into the 256-entry
 *  @ref sin_table — one full rotation = 256 units (≈ 1.4° per step).
 *  Scale is Q7 fixed-point: 128 = 1.0×, 64 = 0.5×, 255 ≈ 2.0×.
 *  Positions are signed 16-bit world-space coordinates (range ±16383).
 *
 *  The object parameters set a single shared "current object" register block
 *  in the DSP.  In scene mode the per-object pose is instead supplied through
 *  the @ref SceneObjectParams descriptor.
 * @{ */
// =============================================================================

/**
 * @brief Set all object transformation parameters (angles, scale, position).
 *
 * Writes pitch, yaw, roll, scale, and position in a single burst to the
 * kernel's object-parameter X-RAM region using SCI auto-increment.
 *
 * @param pitch  Pitch angle (sin_table index, 0..255).
 * @param yaw    Yaw   angle (sin_table index, 0..255).
 * @param roll   Roll  angle (sin_table index, 0..255).
 * @param scale  Object scale in Q7 (128 = 1.0×).
 * @param pos_x  World X position.
 * @param pos_y  World Y position.
 * @param pos_z  World Z position.
 */
void vgk_obj_params(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale,
    int16_t pos_x, int16_t pos_y, int16_t pos_z);

/**
 * @brief Update object angles and scale only (position unchanged).
 *
 * Use this instead of @ref vgk_obj_params() when only the orientation or
 * scale changes between frames, to avoid re-writing the position words.
 *
 * @param pitch  Pitch angle (sin_table index, 0..255).
 * @param yaw    Yaw   angle (sin_table index, 0..255).
 * @param roll   Roll  angle (sin_table index, 0..255).
 * @param scale  Object scale in Q7 (128 = 1.0×).
 */
void vgk_obj_angle_scale(uint8_t pitch, uint8_t yaw, uint8_t roll, uint8_t scale);

/**
 * @brief Update object world position only (angles and scale unchanged).
 *
 * @param pos_x  World X position.
 * @param pos_y  World Y position.
 * @param pos_z  World Z position.
 */
void vgk_obj_pos(int16_t pos_x, int16_t pos_y, int16_t pos_z);

/**
 * @brief Set all camera parameters (angles and position).
 *
 * The camera is global and shared across all objects in both single-object
 * and scene mode.  The scale field in the packed camera word is fixed at 0x80
 * (unused); do not pass a scale to the camera.
 *
 * @param pitch  Camera pitch (sin_table index, 0..255).
 * @param yaw    Camera yaw   (sin_table index, 0..255).
 * @param roll   Camera roll  (sin_table index, 0..255).
 * @param pos_x  Camera world X position.
 * @param pos_y  Camera world Y position.
 * @param pos_z  Camera world Z position.
 */
void vgk_cam_params(uint8_t pitch, uint8_t yaw, uint8_t roll,
    int16_t pos_x, int16_t pos_y, int16_t pos_z);

/**
 * @brief Update camera angles only (position unchanged).
 *
 * @param pitch  Camera pitch (sin_table index, 0..255).
 * @param yaw    Camera yaw   (sin_table index, 0..255).
 * @param roll   Camera roll  (sin_table index, 0..255).
 */
void vgk_cam_angle(uint8_t pitch, uint8_t yaw, uint8_t roll);

/**
 * @brief Update camera world position only (angles unchanged).
 *
 * @param pos_x  Camera world X position.
 * @param pos_y  Camera world Y position.
 * @param pos_z  Camera world Z position.
 */
void vgk_cam_pos(int16_t pos_x, int16_t pos_y, int16_t pos_z);

/** @} */ // end vgk_transform

// =============================================================================
/** @defgroup vgk_render Rendering Configuration and Execution
 *  @brief Control hidden-line removal, depth coloring, triggering, and waiting.
 * @{ */
// =============================================================================

/**
 * @brief Enable or disable global face-normal hidden-line removal.
 *
 * When enabled, the kernel back-face culls each edge against the face normals
 * stored in the active slot.  Edges whose both adjacent faces are back-facing
 * are omitted from the output stream.  Requires valid hidden-line data
 * (uploaded via @ref vgk_model_hidden_line() or @ref vgk_model_save()).
 *
 * @param enabled  true to enable hidden-line removal; false to disable.
 */
void vgk_hidden_line(bool enabled);

/**
 * @brief Edge depth-coloring mode.
 *
 * Controls whether the kernel emits per-edge descriptor words and how the
 * host-side @ref vgk_scrn_edges_render() interprets them.
 */
typedef enum {
    VGK_EC_DEFAULT,      ///< No depth coloring; descriptor words disabled; all edges rendered in @c default_color.
    VGK_EC_NEAR_FAR,     ///< Object-level near/far coloring using @ref Object3D::object_color.
    VGK_EC_EDGE_COLORING ///< Per-edge near/far coloring using @ref Object3D::edge_color array.
} edge_coloring_t;

/**
 * @brief Set the edge depth-coloring mode.
 *
 * Configures whether the kernel prefixes each output edge with a one-word
 * descriptor containing the near flag, slot index, and edge index.  This
 * descriptor is consumed by @ref vgk_scrn_edges_render() to select colors.
 *
 * Passing @ref VGK_EC_DEFAULT disables descriptor emission (fastest path).
 * Any other mode automatically enables descriptors.
 *
 * @param mode  The desired coloring mode (@ref edge_coloring_t).
 */
void vgk_edge_coloring(edge_coloring_t mode);

/**
 * @brief Trigger a kernel render pass.
 *
 * Writes @ref VGK_TRIGGER_MAGIC to SCI_AICTRL0, causing the DSP to begin
 * processing.  The kernel processes asynchronously; call @ref vgk_wait_complete()
 * to block until the result is ready.
 *
 * @note In scene mode (@ref vgk_scene_mode()), the single trigger processes
 *       all objects in the scene descriptor.
 */
void vgk_trigger(void);

/**
 * @brief Poll the kernel status until complete, error, or timeout.
 *
 * On each polling iteration, calls the registered yield callback
 * (@ref vgk_yield_cb()) if set, otherwise inserts a short NOP delay to
 * avoid saturating the SCI bus.
 *
 * @param timeout_ms  Maximum time to wait in milliseconds. (note: implemented as a loop count)
 * @return 1  Kernel completed successfully (@ref VGK_STATUS_DONE).
 * @return 2  Kernel reported an error (save/load fault).
 * @return 0  Timeout expired before completion.
 */
uint8_t vgk_wait_complete(uint16_t timeout_ms);

/**
 * @brief Read the current raw kernel status.
 *
 * Translates the raw @ref VGK_STATUS word into a simplified code.
 *
 * @return 0  Idle (not yet triggered or reset).
 * @return 1  Busy (processing in progress).
 * @return 2  Complete (@ref VGK_STATUS_DONE received).
 * @return 3  Error (@ref VGK_STATUS_SAVE_ERROR or @ref VGK_STATUS_LOAD_ERROR).
 */
uint8_t vgk_status(void);

/**
 * @brief Register a yield callback invoked on each polling iteration.
 *
 * The callback is called by @ref vgk_wait_complete() (and @ref vgk_yield())
 * on every loop iteration instead of the default NOP delay.  Intended to
 * service audio ticks or other periodic tasks while waiting for the DSP.
 * Pass NULL to restore the default NOP-delay behavior.
 *
 * @param cb  Function pointer to the yield callback, or NULL.
 */
void vgk_yield_cb(void (*cb)(void));

/**
 * @brief Invoke the registered yield callback once.
 *
 * If no callback is registered this function returns immediately.  Useful
 * for manually yielding inside application render loops that poll the kernel.
 */
void vgk_yield(void);

/** @} */ // end vgk_render

// =============================================================================
/** @defgroup vgk_draw Screen Drawing
 *  @brief Read the output edge stream and drive the hardware line engine.
 * @{ */
// =============================================================================

/**
 * @brief Draw a single line segment using the F256 hardware line engine.
 *
 * Pushes one line draw command directly to the DL (display-list / line draw)
 * hardware, waits for the FIFO to drain, then disables the engine.
 *
 * @param x0     Start X coordinate (screen pixels).
 * @param y0     Start Y coordinate (screen pixels).
 * @param x1     End   X coordinate (screen pixels).
 * @param y1     End   Y coordinate (screen pixels).
 * @param color  Palette index to draw with.
 * @param layer  Graphics layer to draw on.
 */
void vgk_line_draw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color, uint8_t layer);

/**
 * @brief Read the output edge stream and render all visible edges to screen.
 *
 * Reads @ref VGK_N_STREAM_EDGES from DSP memory, then iterates over the
 * stream consuming 3 or 4 words per edge (x0, x1, y packed, and optionally
 * a descriptor word when depth coloring is active).  For each edge:
 *  - If descriptors are enabled and coloring mode is @ref VGK_EC_NEAR_FAR,
 *    the object-level near/far color is selected from @ref Object3D::object_color.
 *  - If mode is @ref VGK_EC_EDGE_COLORING and a per-edge entry exists, the
 *    per-edge color is used instead.
 *  - Otherwise @p default_color is used.
 *
 * @param layer          Graphics layer to draw on.
 * @param default_color  Fallback palette index when no coloring override applies.
 * @return Number of edges rendered.
 */
uint8_t vgk_scrn_edges_render(uint8_t layer, uint8_t default_color);

/** @} */ // end vgk_draw

#define CAPTURE_MAX_EDGES  VGK_MAX_EDGES      ///< Edge array size in @ref PluginCapture.
#define CAPTURE_MAX_CLIP_V VGK_MAX_VERTICES   ///< Clip-vertex array size in @ref PluginCapture.

/**
 * @brief Snapshot of DSP-internal state for host-side inspection.
 *
 * Filled by @ref vgk_plugin_capture_state().  Useful for verifying that the
 * DSP received correct parameters and for diagnosing rendering artifacts.
 */
typedef struct {
    // Camera inputs (packed DSP registers)
    uint16_t cam_pitch_yaw;  ///< Packed camera pitch (high) and yaw (low).
    uint16_t cam_roll_scale; ///< Packed camera roll (high) and scale (low, fixed 0x80).
    int16_t  cam_pos_x;      ///< Camera world X position.
    int16_t  cam_pos_y;      ///< Camera world Y position.
    int16_t  cam_pos_z;      ///< Camera world Z position.

    // Unpacked camera trigonometry (Q14, written by DSP)
    int16_t cam_sx; ///< sin(camera pitch), Q14.
    int16_t cam_cx; ///< cos(camera pitch), Q14.
    int16_t cam_sy; ///< sin(camera yaw),   Q14.
    int16_t cam_cy; ///< cos(camera yaw),   Q14.
    int16_t cam_sz; ///< sin(camera roll),  Q14.
    int16_t cam_cz; ///< cos(camera roll),  Q14.

    /** @brief Combined object-to-view matrix (row-major 3×4, Q14). */
    int16_t matrix[12];

    // Projection parameters
    uint16_t proj_focal;  ///< Focal length (Q0).
    uint16_t proj_half_w; ///< Half screen width (Q0).
    uint16_t proj_half_h; ///< Half screen height (Q0).
    int16_t  proj_near_z; ///< Near plane Z (Q0, negative).

    // Runtime counts
    uint16_t n_vertices;     ///< Input vertex count active at capture time.
    uint16_t n_edges;        ///< Input edge count active at capture time.
    uint16_t n_output_edges; ///< Number of output edges in the stream.
    uint16_t n_clip_verts;   ///< Number of clip-plane intersection vertices generated.
    uint16_t status;         ///< Raw @ref VGK_STATUS value at capture time.

    // Output samples
    uint16_t edge_list[CAPTURE_MAX_EDGES];       ///< Packed input edge pairs [v1<<8 | v0].
    uint16_t edge_flags[CAPTURE_MAX_EDGES];      ///< Per-edge flag words (VISIBLE | CLIP_V0 | CLIP_V1 | NEAR).
    uint16_t clip_screen[CAPTURE_MAX_CLIP_V*2];  ///< [sx, sy] pairs for clip-generated vertices.
} PluginCapture;

// =============================================================================
/** @defgroup vgk_debug Debug / Diagnostics
 *  @brief Snapshot DSP internal state for host-side debugging.
 * @{ */
// =============================================================================

/**
 * @brief Capture a diagnostic snapshot of key DSP memory regions.
 *
 * Reads camera inputs, computed trig values, the composite matrix, projection
 * parameters, runtime edge/vertex counts, and a sample of the output edge list
 * into @p cap.  Safe to call immediately after @ref vgk_wait_complete()
 * returns.
 *
 * @param cap  Output structure to fill.  Must not be NULL.
 */
void vgk_plugin_capture_state(PluginCapture *cap);

/** @} */ // end vgk_debug

// =============================================================================
/** @defgroup vgk_scene Scene Mode
 *  @brief Multi-object rendering with AABB occlusion and depth sorting.
 *
 *  Scene mode allows up to @ref VGK_SCENE_MAX_OBJECTS objects to be
 *  transformed, projected, and composited in a single kernel trigger.  The
 *  DSP performs:
 *   1. Per-object transform and projection.
 *   2. Optional AABB occlusion test (back-to-front order; objects fully
 *      occluded by a nearer object's AABB are skipped).
 *   3. Optional depth sort (painter's order; nearest objects rendered last).
 *   4. Combined edge stream output consumed by @ref vgk_scrn_edges_render().
 *
 *  Occlusion uses a coarse AABB test based on the screen bounding box of an object's
 *  projected and clipped edges.  The bounding box is axis aligned (AABB) so it is conservative;
 *  Partially overlapped edges may be clipped to their midpoint if it is visible.
 *
 *  ### Typical scene loop
 *  @code
 *    vgk_scene_mode(true);
 *    vgk_scene_objects(n, objects);
 *    vgk_trigger();
 *    vgk_wait_complete(10000);
 *    vgk_scrn_edges_render(layer, color);
 *    vgk_scene_mode(false);
 *  @endcode
 *  Or call the convenience wrapper @ref vgk_scene_render() which performs all
 *  steps above in one call.
 * @{ */
// =============================================================================

/**
 * @brief Per-object scene descriptor record written by the host.
 *
 * One entry per object in the scene; passed as an array to
 * @ref vgk_scene_objects() or individually to @ref vgk_scene_object_params().
 */
typedef struct {
    uint16_t slot;    ///< Save-slot index (0..@ref VGK_SAVE_SLOT_COUNT – 1).
    uint8_t  pitch;   ///< Object pitch angle (sin_table index, 0..255).
    uint8_t  yaw;     ///< Object yaw   angle (sin_table index, 0..255).
    uint8_t  roll;    ///< Object roll  angle (sin_table index, 0..255).
    uint8_t  scale;   ///< Object scale Q7 (128 = 1.0×).
    int16_t  pos_x;   ///< World X position.
    int16_t  pos_y;   ///< World Y position.
    int16_t  pos_z;   ///< World Z position.
} SceneObjectParams;

/**
 * @brief Per-object metadata available after a scene render completes.
 *
 * Retrieved with @ref vgk_scene_object_meta_get().  Offsets are relative to
 * the combined vertex/edge/clip arrays in DSP memory.
 */
typedef struct {
    uint16_t vert_offset;  ///< Cumulative vertex offset into the combined screen-coord array.
    uint16_t edge_offset;  ///< Cumulative edge offset into the combined edge stream.
    uint16_t clip_offset;  ///< Cumulative clip-vertex offset.
    uint16_t vert_count;   ///< Number of projected screen vertices this object produced.
    uint16_t edge_count;   ///< Number of output (visible) edges this object produced.
    int16_t  centroid_z;   ///< View-space Z of the object centroid (more negative = farther away).
    int16_t  aabb_min_x;   ///< Screen-space AABB minimum X.
    int16_t  aabb_max_x;   ///< Screen-space AABB maximum X.
    int16_t  aabb_min_y;   ///< Screen-space AABB minimum Y.
    int16_t  aabb_max_y;   ///< Screen-space AABB maximum Y.
} SceneObjectMeta;

/**
 * @brief Combined scene output totals available after processing completes.
 *
 * Retrieved with @ref vgk_scene_get_result().
 */
typedef struct {
    uint16_t total_verts;  ///< Total projected screen vertices across all objects.
    uint16_t total_edges;  ///< Total visible output edges across all objects.
    uint16_t total_clips;  ///< Total clip-plane intersection vertices generated.
    uint8_t  n_objects;    ///< Number of objects that were processed in this pass.
} SceneResult;

/**
 * @brief Enable or disable scene mode.
 *
 * When enabled, the next @ref vgk_trigger() call processes all objects in the
 * scene descriptor instead of the single selected object.  Enabling scene mode
 * automatically enables AABB occlusion; disabling it disables occlusion.
 *
 * @param enabled  true to enter scene mode; false to return to single-object mode.
 */
void vgk_scene_mode(bool enabled);

/**
 * @brief Enable or disable AABB occlusion and depth sorting in scene mode.
 *
 * When disabled, all objects are processed without occlusion testing or depth
 * re-ordering.  Per-object hidden-line removal is unaffected.
 *
 * @param enabled  true to enable occlusion + depth sort; false to disable both.
 */
void vgk_scene_occlusion(bool enabled);

/**
 * @brief Write the full scene descriptor: object count and all per-object params.
 *
 * Sets @ref VGK_SCENE_N_OBJECTS and writes @p n_objects parameter records into
 * the DSP scene-descriptor region.  @p n_objects is clamped to
 * @ref VGK_SCENE_MAX_OBJECTS.
 *
 * @param n_objects  Number of objects in the scene (1..@ref VGK_SCENE_MAX_OBJECTS).
 * @param objects    Array of per-object parameter records.
 */
void vgk_scene_objects(uint8_t n_objects, const SceneObjectParams *objects);

/**
 * @brief Write a single per-object record in the scene descriptor.
 *
 * Use when only one object's parameters change between frames while others
 * remain constant.
 *
 * @param index  Object index within the descriptor (0..@ref VGK_SCENE_MAX_OBJECTS – 1).
 * @param obj    Pointer to the parameter record to write.
 */
void vgk_scene_object_params(uint8_t index, const SceneObjectParams *obj);


/**
 * @brief Read the combined scene result totals after processing.
 *
 * @param result  Output structure to fill.  Must not be NULL.
 */
void vgk_scene_get_result(SceneResult *result);

/**
 * @brief Read per-object metadata for one object after scene processing.
 *
 * @param index  Object index (0..n_objects – 1).
 * @param meta   Output structure to fill.  Must not be NULL.
 */
void vgk_scene_object_meta_get(uint8_t index, SceneObjectMeta *meta);


/**
 * @brief High-level convenience: configure, trigger, wait, and render a scene.
 *
 * Performs the full scene render sequence in one call:
 *  1. Enables scene mode (with occlusion).
 *  2. Optionally writes the scene descriptor if @p objects is non-NULL.
 *  3. Triggers the kernel.
 *  4. Waits up to 10 seconds for completion.
 *  5. Calls @ref vgk_scrn_edges_render() to draw the edge stream.
 *  6. Disables scene mode.
 *
 * @param n_objects      Number of objects in the scene.
 * @param objects        Per-object parameter array, or NULL if already written.
 * @param default_color  Fallback palette index for edges without depth coloring.
 * @param draw_layer     Graphics layer to draw on.
 * @return Number of edges rendered, or 0 on timeout / error.
 */
uint8_t vgk_scene_render(uint8_t n_objects, const SceneObjectParams *objects,
                                uint8_t default_color,
                                uint8_t draw_layer);

/** @} */ // end vgk_scene

#endif // GEOMETRY_KERNEL_H
