#ifndef OBJECT_3D_H
#define OBJECT_3D_H

#include <stdbool.h>
#include "../include/3d_math.h"
#include <stdint.h>

#define GEOM3D_INVALID_FACE 0xFF

// Geom3D type
// struct of arrays providing vertex and edge data for 3D model geometry
// with optional face normal and edge-face adjacency for hidden-line removal support.
// edge_a contains the index of the first vertex of each edge,
// edge_b contains the index of the second vertex of each edge.
//
// faces are defined by edge adjacency
// edge_face0 and edge_face1 contains the indices of the faces adjacent to each edge.
// for a closed mesh every edge will have 2 adjacent faces.
// the face normal is used for backface culling and hidden-line removal.
// when defining a model, the normals should point outward from the face surface
// modelers, such as blender, can export vertex, edge, face and normal data which can be
// converted to the Geometry3D format with a script.
//
// Note that edges do not have to have an adjacent face.  e.g. a planar surface
// Or a line segment.  In these cases, the edge_face value should be 
// set to GEOM3D_INVALID_FACE (0xFF) to indicate no adjacent face.
//
// While examples uses in-memory defined models, models could be dynamically loaded from disk
// into an allocated Geom3D structure and then uploaded to the plugin.  After a model is uploaded
// the Geom3D structure can be freed from CPU memory.

typedef struct {
    uint8_t vertex_count;
    uint8_t edge_count;

    // Bounding sphere for culling and collision detection.
    int16_t center_x;  // object space center of collision sphere
    int16_t center_y;  // typically all zero.
    int16_t center_z;  // but allows for non centered bounding sphere for asymmetric objecs.
    int16_t radius;    
 
    // Vertex storage (length = vertex_count)
    const int16_t* vx;
    const int16_t* vy;
    const int16_t* vz;

    // Edge storage (length = edge_count)
    const uint8_t* edge_a;
    const uint8_t* edge_b;

    // Optional hidden-line metadata.
    // Face normals are Q14 object-space tuples (length = face_count).
    // Edge-face arrays are per-edge adjacency (length = edge_count), where
    // GEOM3D_INVALID_FACE marks "no adjacent face" (boundary edge).
    uint8_t face_count;
    const int16_t* face_nx;
    const int16_t* face_ny;
    const int16_t* face_nz;
    const uint8_t* edge_face0;
    const uint8_t* edge_face1;

} __attribute__((packed)) Geom3D;

// Object3D includes optional object coloring including optional per edge coloring
// and includes a pointer to geometry data.
// after geometry is loaded into the plugin, geometry can be freed and geometry pointer can be set to NULL if desired.

typedef struct {
    const Geom3D* geometry;
    uint16_t object_color; // 16-bit packed palette index high byte is far color, low byte is near color
    // overrides object-level color
    // 16-bit packed palette index high byte is far color, low byte is near color
    // array with geometry->edge_count entries
    // kernel will optionally return a descriptor word before edge edge
    // with bit 15 set if edge is near, bits 8-14 = slot, bits 0-7 = edge index
    // slot and edge index can be used to look up edge color in this array for per-edge near/far coloring.
    uint8_t edge_color_count;
    const uint16_t* edge_color;
} __attribute__((packed)) Object3D;

extern const Object3D g_model_projectile;
extern const Object3D g_model_starfield;
extern const Object3D g_model_cube;
extern const Object3D g_model_anaconda;
extern const Object3D g_model_truncated_octahedron;
extern const Object3D g_model_truncated_icosahedron;

typedef struct {
    vec3_t position;
    uint8_t yaw, pitch, roll; // orientation
    bool moved; // flag to indicate if the camera has moved since last frame, for optimization purposes
} __attribute__((packed)) Camera;

void camera_init(Camera *cam, vec3_t pos);

#endif // OBJECT_3D_H
