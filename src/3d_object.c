#include "../include/3d_object.h"
#include "../include/3d_math.h"
#include <stdbool.h>
#include <string.h>

#define SCALE 150

static const int16_t g_empty_vx[1] = {
    0
};
static const int16_t g_empty_vy[1] = {
    0
};
static const int16_t g_empty_vz[1] = {
    0
};
static const uint8_t g_empty_edge_a[0] = {};
static const uint8_t g_empty_edge_b[0] = {};
const Model3D g_model_empty = {
    .vertex_count = 1,  
    .edge_count = 0,    
    .vx = g_empty_vx,
    .vy = g_empty_vy,
    .vz = g_empty_vz,
    .edge_a = g_empty_edge_a,
    .edge_b = g_empty_edge_b,
    .center_x = 0,
    .center_y = 0,
    .center_z = 0,
    .radius = 1, 
};

// Projectile
#define P_SCALE 8
static const int16_t g_projectile_vx[4] = { P_SCALE,  P_SCALE, -P_SCALE, -P_SCALE };
static const int16_t g_projectile_vy[4] = { P_SCALE, -P_SCALE,  P_SCALE, -P_SCALE };
static const int16_t g_projectile_vz[4] = { P_SCALE, -P_SCALE, -P_SCALE,  P_SCALE };
static const uint8_t g_projectile_edge_a[6] = { 0, 0, 0, 1, 1, 2 };
static const uint8_t g_projectile_edge_b[6] = { 1, 2, 3, 2, 3, 3 };
const Model3D g_model_projectile = {
    .vertex_count = 4,
    .edge_count = 6,
    .vx = g_projectile_vx,
    .vy = g_projectile_vy,
    .vz = g_projectile_vz,
    .edge_a = g_projectile_edge_a,
    .edge_b = g_projectile_edge_b,
    .center_x = 0,
    .center_y = 0,
    .center_z = 0,
    .radius = P_SCALE * 1.732, // SCALE * sqrt(3)
    .object_color = 0x0A0A,    // red projectile
    .face_count   = 0,    
};


// Starfield

static const int16_t g_stars_vx[32] = {
    //-- 12 icosahedron vertices (r≈8000) --
        0,     0,  5236, -5236,  5236, -5236,  8000, -8000,  8000, -8000,     0,     0,
    //-- 20 scatter fill points (hand-distributed across sky) --
     3000, -3000,  6000, -6000,  1000, -1000,  7000, -7000,
     4500, -4500,  2000, -2000,  6500, -6500,  3500, -3500,
     5500, -5500,  7500,  -500,  4000,  1500, -4000, -1500
};

static const int16_t g_stars_vy[32] = {
    //-- 12 icosahedron --
     5236,  5236,  8000,  8000, -8000, -8000,     0,     0,     0,     0,  5236, -5236,
    //-- 20 fill --
     7000,  7000,  2000,  2000,  8000,  8000,  1000,  1000,
    -7000, -7000, -2000, -2000, -5000, -5000,  5000,  5000,
     3000,  3000, -6000, -6000, -4000,  6500,  4000, -6500
};

static const int16_t g_stars_vz[32] = {
    //-- 12 icosahedron --
     5236, -5236,     0,     0,     0,     0,  5236,  5236, -5236, -5236,  8000,  8000,
    //-- 20 fill --
     1000, -1000,  5000, -5000,  2000, -2000,  3000, -3000,
     2500, -2500,  6000, -6000,  1500, -1500,  4500, -4500,
     6000, -6000,  3000, -3000,  5500,  2000, -5500, -2000
};

// Every edge is degenerate (a == b) → rendered as a point
static const uint8_t g_stars_edge_a[32] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31
};
static const uint8_t g_stars_edge_b[32] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31
};

const Model3D g_model_starfield = {
    .vertex_count = 32,
    .edge_count   = 32,        
    .vx = g_stars_vx,
    .vy = g_stars_vy,
    .vz = g_stars_vz,
    .edge_a = g_stars_edge_a,
    .edge_b = g_stars_edge_b,
    .center_x = 0,
    .center_y = 0,
    .center_z = 0,
    .radius   = 1,             // kernel scale: start at 1.0 (neutral)
    .object_color = 0x0F0F,    // default white.  could vary brightness.
    .face_count   = 0,
};

static const int16_t g_cube_vx[8] = {
    -1 * SCALE, -1 * SCALE, -1 * SCALE, -1 * SCALE,
     1 * SCALE,  1 * SCALE,  1 * SCALE,  1 * SCALE,
};
static const int16_t g_cube_vy[8] = {
    -1 * SCALE, -1 * SCALE,  1 * SCALE,  1 * SCALE,
    -1 * SCALE, -1 * SCALE,  1 * SCALE,  1 * SCALE,
};
static const int16_t g_cube_vz[8] = {
    -1 * SCALE,  1 * SCALE, -1 * SCALE,  1 * SCALE,
    -1 * SCALE,  1 * SCALE, -1 * SCALE,  1 * SCALE,
};
static const uint8_t g_cube_edge_a[12] = {
    0, 0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 6,
};
static const uint8_t g_cube_edge_b[12] = {
    1, 2, 4, 3, 5, 3, 6, 7, 5, 6, 7, 7,
};

// Cube face normals in Q14 object space.
// Face indices: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z
static const int16_t g_cube_face_nx[6] = {
    -16384,  16384,      0,      0,      0,      0,
};
static const int16_t g_cube_face_ny[6] = {
         0,      0, -16384,  16384,      0,      0,
};
static const int16_t g_cube_face_nz[6] = {
         0,      0,      0,      0, -16384,  16384,
};

// Per-edge adjacent face indices, parallel to g_cube_edge_a/b.
static const uint8_t g_cube_edge_face0[12] = {
    0, 0, 2, 0, 2, 0, 3, 3, 1, 1, 1, 1,
};
static const uint8_t g_cube_edge_face1[12] = {
    2, 4, 4, 5, 5, 3, 4, 5, 2, 4, 5, 3,
};

const Model3D g_model_cube = {
    .vertex_count = 8,  // debug 2 instead of 8
    .edge_count = 12,    // debug 1 instead of 12
    .vx = g_cube_vx,
    .vy = g_cube_vy,
    .vz = g_cube_vz,
    .edge_a = g_cube_edge_a,
    .edge_b = g_cube_edge_b,
    // Center and radius derived from the provided AABB.
    .center_x = 0,
    .center_y = 0,
    .center_z = 0,
    .radius = 173, // sqrt(3*100^2)
    .object_color = 0x0D0B,
    .face_count = 6,
    .face_nx = g_cube_face_nx,
    .face_ny = g_cube_face_ny,
    .face_nz = g_cube_face_nz,
    .edge_face0 = g_cube_edge_face0,
    .edge_face1 = g_cube_edge_face1,
};

// Elite 6502 shape: ANACONDA
static const int16_t anaconda_vx[15] = { 0, -86, -52, 52, 86, 0, -138, -86, 86, 138, -86, -138, 0, 138, 86 };
static const int16_t anaconda_vy[15] = { 14, -26, -94, -94, -26, 96, 30, -78, -78, 30, 106, -2, 0, -2, 106 };
static const int16_t anaconda_vz[15] = { -116, -74, -6, -6, -74, -98, -30, 80, 80, -30, -46, 64, 508, 64, -46 };
static const uint8_t anaconda_edge_a[25] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 12 };
static const uint8_t anaconda_edge_b[25] = { 1, 4, 5, 2, 6, 3, 7, 4, 8, 9, 10, 14, 10, 11, 11, 12, 12, 13, 13, 14, 12, 14, 12, 13, 14 };
static const int16_t anaconda_face_nx[12] = { 0, -8157, -12917, 0, 12917, 8157, 0, -12904, -13397, 13397, 12904, 0 };
static const int16_t anaconda_face_ny[12] = { -11815, 2879, -9562, -16131, -9562, 2879, 16124, 9578, -8435, -8435, 9578, 16092 };
static const int16_t anaconda_face_nz[12] = { -11351, -13915, -3187, 2868, -3187, -13915, -2905, 3193, 4218, 4218, 3193, 3081 };
static const uint8_t anaconda_edge_face0[25] = { 0, 0, 1, 0, 1, 0, 2, 0, 3, 4, 1, 5, 1, 2, 2, 3, 3, 4, 4, 5, 7, 6, 7, 9, 10 };
static const uint8_t anaconda_edge_face1[25] = { 1, 5, 5, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 8, 10, 11 };
const Model3D g_model_anaconda = {
    .vertex_count = 15,
    .edge_count = 25,
    .center_x = 0,
    .center_y = 3,
    .center_z = 98,
    .radius = 157,
    .object_color = 0x0D0B,    
    .vx = anaconda_vx,
    .vy = anaconda_vy,
    .vz = anaconda_vz,
    .edge_a = anaconda_edge_a,
    .edge_b = anaconda_edge_b,
    .face_count = 12,
    .face_nx = anaconda_face_nx,
    .face_ny = anaconda_face_ny,
    .face_nz = anaconda_face_nz,
    .edge_face0 = anaconda_edge_face0,
    .edge_face1 = anaconda_edge_face1
};


/* truncated octahedron — canonical (0, ±1, ±2) permutation set, scaled ×48.
 * 24 vertices, 36 edges, 14 faces (6 squares + 8 hexagons).
 */

#define TO_SCALE 5

static const int16_t to_vx[24] = {
    -96 * TO_SCALE, -96 * TO_SCALE, -96 * TO_SCALE, -96 * TO_SCALE,  -48 * TO_SCALE, -48 * TO_SCALE, -48 * TO_SCALE, -48 * TO_SCALE,
      0,   0,   0,   0,    0,   0,   0,   0,
     48 * TO_SCALE,  48 * TO_SCALE,  48 * TO_SCALE,  48 * TO_SCALE,   96 * TO_SCALE,  96 * TO_SCALE,  96 * TO_SCALE,  96 * TO_SCALE
};
static const int16_t to_vy[24] = {
    -48 * TO_SCALE,   0,   0,  48 * TO_SCALE,  -96 * TO_SCALE,   0,   0,  96 * TO_SCALE,
    -96 * TO_SCALE, -96 * TO_SCALE, -48 * TO_SCALE, -48 * TO_SCALE,   48 * TO_SCALE,  48 * TO_SCALE,  96 * TO_SCALE,  96 * TO_SCALE,
    -96 * TO_SCALE,   0,   0,  96 * TO_SCALE,  -48 * TO_SCALE,   0,   0,  48 * TO_SCALE
};
static const int16_t to_vz[24] = {
      0, -48 * TO_SCALE,  48 * TO_SCALE,   0,    0, -96 * TO_SCALE,  96 * TO_SCALE,   0,
    -48 * TO_SCALE,  48 * TO_SCALE, -96 * TO_SCALE,  96 * TO_SCALE,  -96 * TO_SCALE,  96 * TO_SCALE, -48 * TO_SCALE,  48 * TO_SCALE,
      0, -96 * TO_SCALE,  96 * TO_SCALE,   0,    0, -48 * TO_SCALE,  48 * TO_SCALE,   0
};

/* 36 edges: every adjacent vertex pair at distance √2 in raw unit coords */
static const uint8_t to_edge_a[36] = {
     0,  0,  0,  1,  1,  2,  2,  3,  4,  4,
     5,  5,  6,  6,  7,  7,  8,  8,  9,  9,
    10, 11, 12, 12, 13, 13, 14, 15, 16, 17,
    18, 19, 20, 20, 21, 22
};
static const uint8_t to_edge_b[36] = {
     1,  2,  4,  3,  5,  3,  6,  7,  8,  9,
    10, 12, 11, 13, 14, 15, 10, 16, 11, 16,
    17, 18, 14, 17, 15, 18, 19, 19, 20, 21,
    22, 23, 21, 22, 23, 23
};

/* 14 face normals in Q14 object space.
 * Faces 0-5: square faces (axis-aligned), faces 6-13: hexagonal faces. */
static const int16_t to_face_nx[14] = {
    -16384,  16384,      0,      0,      0,      0,
     -9459,  -9459,  -9459,  -9459,   9459,   9459,   9459,   9459
};
static const int16_t to_face_ny[14] = {
         0,      0, -16384,  16384,      0,      0,
     -9459,  -9459,   9459,   9459,  -9459,  -9459,   9459,   9459
};
static const int16_t to_face_nz[14] = {
         0,      0,      0,      0, -16384,  16384,
     -9459,   9459,  -9459,   9459,  -9459,   9459,  -9459,   9459
};

/* Per-edge adjacent face indices (derived from face membership of each vertex) */
static const uint8_t to_edge_face0[36] = {
     0,  0,  6,  0,  6,  0,  7,  8,  2,  2,
     4,  4,  5,  5,  3,  3,  6,  2,  7,  2,
     4,  5,  8,  4,  9,  5,  3,  3, 10, 10,
    11, 12,  1,  1,  1,  1
};
static const uint8_t to_edge_face1[36] = {
     6,  7,  7,  8,  8,  9,  9,  9,  6,  7,
     6,  8,  7,  9,  8,  9, 10, 10, 11, 11,
    10, 11, 12, 12, 13, 13, 12, 13, 11, 12,
    13, 13, 10, 11, 12, 13
};

const Model3D g_model_truncated_octahedron = {
    .vertex_count = 24,
    .edge_count   = 36,
    .center_x = 0,
    .center_y = 0,
    .center_z = 0,
    .radius   = 108,  // ceil(sqrt(96²+48²)) = ceil(107.3)

    .object_color = 0x0D0B,

    .vx = to_vx,
    .vy = to_vy,
    .vz = to_vz,

    .edge_a = to_edge_a,
    .edge_b = to_edge_b,

    .face_count = 14,
    .face_nx = to_face_nx,
    .face_ny = to_face_ny,
    .face_nz = to_face_nz,

    .edge_face0 = to_edge_face0,
    .edge_face1 = to_edge_face1
};


void camera_init(Camera *cam, vec3_t pos) {
    cam->position = pos;
    cam->yaw = 0;
    cam->pitch = 0;
    cam->roll = 0;
    cam->moved = false;
}

void camera_look_at(Camera *cam, vec3_t target) {
    // Simplified: assume looking at origin from -Z
    cam->yaw = 0;
    cam->pitch = 0;
    cam->roll = 0;
}
