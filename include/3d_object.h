#ifndef OBJECT_3D_H
#define OBJECT_3D_H

#include <stdbool.h>
#include "../include/3d_math.h"
#include <stdint.h>


// -----------------------------------------------------------------------------
// New generalized 3D model/instance API (SoA friendly, asm-replaceable)
// -----------------------------------------------------------------------------

#define MODEL3D_MAX_VERTICES 8
#define MODEL3D_MAX_FACES    16
#define MODEL3D_INVALID_FACE 0xFF


#if defined(GENERATE_OFFSETS)
#define POINTER(type) uint16_t
#else
#define POINTER(type) type*
#endif

// Model and transform math all int16_t Q14, 
// but transformed outbatch uses float for compatibility with rasterizer.
typedef struct {
    uint8_t vertex_count;
    uint8_t edge_count;
    
    // Bounding sphere for quick culling (model local space)
    int16_t center_x;
    int16_t center_y;
    int16_t center_z;
    int16_t radius;
    uint16_t object_color; // 16-bit packed palette index high byte is far color, low byte is near color

    // Structure-of-Arrays vertex storage (length = vertex_count)
    POINTER(const int16_t) vx;
    POINTER(const int16_t) vy;
    POINTER(const int16_t) vz;

    // Structure-of-Arrays edges (length = edge_count)
    POINTER(const uint8_t) edge_a;
    POINTER(const uint8_t) edge_b;

    // Optional hidden-line metadata.
    // Face normals are Q14 object-space tuples (length = face_count).
    // Edge-face arrays are per-edge adjacency (length = edge_count), where
    // MODEL3D_INVALID_FACE marks "no adjacent face" (boundary edge).
    uint8_t face_count;
    POINTER(const int16_t) face_nx;
    POINTER(const int16_t) face_ny;
    POINTER(const int16_t) face_nz;
    POINTER(const uint8_t) edge_face0;
    POINTER(const uint8_t) edge_face1;

    // Optional per-edge color
    // overrides object-level color
    // 16-bit packed palette index high byte is far color, low byte is near color
    uint8_t edge_color_count;
    POINTER(const uint16_t) edge_color;
} __attribute__((packed)) Model3D;

typedef struct {
    vec3_t position;
    uint8_t yaw;
    uint8_t pitch;
    uint8_t roll; 
    uint8_t yaw_rate;
    uint8_t pitch_rate;
    uint8_t roll_rate;
    uint8_t scale;  // Q7 format 128 = 1.0
    POINTER(const Model3D) model;
} __attribute__((packed)) Instance3D;

typedef struct {
    uint8_t count;     // Must be first element in structure for assembly offset calculation
    uint8_t _pad[3];   // Ensure x/y/z arrays start on a 4-byte boundary
    float x[MODEL3D_MAX_VERTICES];
    float y[MODEL3D_MAX_VERTICES];
    float z[MODEL3D_MAX_VERTICES];
} VertexBatchSoA;

extern const Model3D g_model_empty;
extern const Model3D g_model_projectile;
extern const Model3D g_model_starfield;
extern const Model3D g_model_cube;
extern const Model3D g_model_anaconda;
extern const Model3D g_model_truncated_octahedron;

typedef struct {
    vec3_t position;
    uint8_t yaw, pitch, roll; // orientation
    bool moved; // flag to indicate if the camera has moved since last frame, for optimization purposes
} __attribute__((packed)) Camera;

void camera_init(Camera *cam, vec3_t pos);
void camera_look_at(Camera *cam, vec3_t target);

#endif // OBJECT_3D_H
