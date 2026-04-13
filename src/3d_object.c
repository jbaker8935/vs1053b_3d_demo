#include "../include/3d_object.h"
#include "../include/3d_math.h"
#include <stdbool.h>
#include <string.h>

#define SCALE 150

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
        0,     0,  5236, -5236,  5236, -5236,  8000, -8000,  8000, -8000,     0,     0,
     3000, -3000,  6000, -6000,  1000, -1000,  7000, -7000,
     4500, -4500,  2000, -2000,  6500, -6500,  3500, -3500,
     5500, -5500,  7500,  -500
};

static const int16_t g_stars_vy[32] = {
     5236,  5236,  8000,  8000, -8000, -8000,     0,     0,     0,     0,  5236, -5236,
     7000,  7000,  2000,  2000,  8000,  8000,  1000,  1000,
    -7000, -7000, -2000, -2000, -5000, -5000,  5000,  5000,
     3000,  3000, -6000, -6000
};

static const int16_t g_stars_vz[32] = {
     5236, -5236,     0,     0,     0,     0,  5236,  5236, -5236, -5236,  8000,  8000,
     1000, -1000,  5000, -5000,  2000, -2000,  3000, -3000,
     2500, -2500,  6000, -6000,  1500, -1500,  4500, -4500,
     6000, -6000,  3000, -3000
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
    .radius   = 1,             // not used
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
    .vertex_count = 8,  
    .edge_count = 12,   
    .vx = g_cube_vx,
    .vy = g_cube_vy,
    .vz = g_cube_vz,
    .edge_a = g_cube_edge_a,
    .edge_b = g_cube_edge_b,
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


/* truncated octahedron - max edges supported by current kernel
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

/* 36 edges */
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

/* 14 face normals in Q14 object space. */
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

/* Per-edge adjacent face indices */
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
    .radius   = 108 * TO_SCALE, // Not used

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

/* truncated icosahedron (soccer ball)
 * 60 vertices, 90 edges, 32 faces (12 pentagons + 20 hexagons).
 * Faces 0-11 are pentagons, faces 12-31 are hexagons.
 */

#define TI_SCALE 5

/* vertex coordinates: (0, ±1, ±3φ) and cyclic perms × TI_SCALE,
 * (±1, ±(2+φ), ±2φ) and cyclic perms × TI_SCALE,
 * (±2, ±(1+2φ), ±φ) and cyclic perms × TI_SCALE,
 * with φ = (1+√5)/2 ≈ 1.618, values rounded to nearest integer.
 */
static const int16_t ti_vx[60] = {
       0 * TI_SCALE,   20 * TI_SCALE,   97 * TI_SCALE,    0 * TI_SCALE,   20 * TI_SCALE,  -97 * TI_SCALE,    0 * TI_SCALE,  -20 * TI_SCALE,
      97 * TI_SCALE,    0 * TI_SCALE,  -20 * TI_SCALE,  -97 * TI_SCALE,   20 * TI_SCALE,   72 * TI_SCALE,   65 * TI_SCALE,   20 * TI_SCALE,
      72 * TI_SCALE,  -65 * TI_SCALE,   20 * TI_SCALE,  -72 * TI_SCALE,   65 * TI_SCALE,   20 * TI_SCALE,  -72 * TI_SCALE,  -65 * TI_SCALE,
     -20 * TI_SCALE,   72 * TI_SCALE,   65 * TI_SCALE,  -20 * TI_SCALE,   72 * TI_SCALE,  -65 * TI_SCALE,  -20 * TI_SCALE,  -72 * TI_SCALE,
      65 * TI_SCALE,  -20 * TI_SCALE,  -72 * TI_SCALE,  -65 * TI_SCALE,   40 * TI_SCALE,   85 * TI_SCALE,   32 * TI_SCALE,   40 * TI_SCALE,
      85 * TI_SCALE,  -32 * TI_SCALE,   40 * TI_SCALE,  -85 * TI_SCALE,   32 * TI_SCALE,   40 * TI_SCALE,  -85 * TI_SCALE,  -32 * TI_SCALE,
     -40 * TI_SCALE,   85 * TI_SCALE,   32 * TI_SCALE,  -40 * TI_SCALE,   85 * TI_SCALE,  -32 * TI_SCALE,  -40 * TI_SCALE,  -85 * TI_SCALE,
      32 * TI_SCALE,  -40 * TI_SCALE,  -85 * TI_SCALE,  -32 * TI_SCALE
};
static const int16_t ti_vy[60] = {
      20 * TI_SCALE,   97 * TI_SCALE,    0 * TI_SCALE,   20 * TI_SCALE,  -97 * TI_SCALE,    0 * TI_SCALE,  -20 * TI_SCALE,   97 * TI_SCALE,
       0 * TI_SCALE,  -20 * TI_SCALE,  -97 * TI_SCALE,    0 * TI_SCALE,   72 * TI_SCALE,   65 * TI_SCALE,   20 * TI_SCALE,   72 * TI_SCALE,
     -65 * TI_SCALE,   20 * TI_SCALE,  -72 * TI_SCALE,   65 * TI_SCALE,   20 * TI_SCALE,  -72 * TI_SCALE,  -65 * TI_SCALE,   20 * TI_SCALE,
      72 * TI_SCALE,   65 * TI_SCALE,  -20 * TI_SCALE,   72 * TI_SCALE,  -65 * TI_SCALE,  -20 * TI_SCALE,  -72 * TI_SCALE,   65 * TI_SCALE,
     -20 * TI_SCALE,  -72 * TI_SCALE,  -65 * TI_SCALE,  -20 * TI_SCALE,   85 * TI_SCALE,   32 * TI_SCALE,   40 * TI_SCALE,   85 * TI_SCALE,
     -32 * TI_SCALE,   40 * TI_SCALE,  -85 * TI_SCALE,   32 * TI_SCALE,   40 * TI_SCALE,  -85 * TI_SCALE,  -32 * TI_SCALE,   40 * TI_SCALE,
      85 * TI_SCALE,   32 * TI_SCALE,  -40 * TI_SCALE,   85 * TI_SCALE,  -32 * TI_SCALE,  -40 * TI_SCALE,  -85 * TI_SCALE,   32 * TI_SCALE,
     -40 * TI_SCALE,  -85 * TI_SCALE,  -32 * TI_SCALE,  -40 * TI_SCALE
};
static const int16_t ti_vz[60] = {
      97 * TI_SCALE,    0 * TI_SCALE,   20 * TI_SCALE,  -97 * TI_SCALE,    0 * TI_SCALE,   20 * TI_SCALE,   97 * TI_SCALE,    0 * TI_SCALE,
     -20 * TI_SCALE,  -97 * TI_SCALE,    0 * TI_SCALE,  -20 * TI_SCALE,   65 * TI_SCALE,   20 * TI_SCALE,   72 * TI_SCALE,  -65 * TI_SCALE,
      20 * TI_SCALE,   72 * TI_SCALE,   65 * TI_SCALE,   20 * TI_SCALE,  -72 * TI_SCALE,  -65 * TI_SCALE,   20 * TI_SCALE,  -72 * TI_SCALE,
      65 * TI_SCALE,  -20 * TI_SCALE,   72 * TI_SCALE,  -65 * TI_SCALE,  -20 * TI_SCALE,   72 * TI_SCALE,   65 * TI_SCALE,  -20 * TI_SCALE,
     -72 * TI_SCALE,  -65 * TI_SCALE,  -20 * TI_SCALE,  -72 * TI_SCALE,   32 * TI_SCALE,   40 * TI_SCALE,   85 * TI_SCALE,  -32 * TI_SCALE,
      40 * TI_SCALE,   85 * TI_SCALE,   32 * TI_SCALE,   40 * TI_SCALE,  -85 * TI_SCALE,  -32 * TI_SCALE,   40 * TI_SCALE,  -85 * TI_SCALE,
      32 * TI_SCALE,  -40 * TI_SCALE,   85 * TI_SCALE,  -32 * TI_SCALE,  -40 * TI_SCALE,   85 * TI_SCALE,   32 * TI_SCALE,  -40 * TI_SCALE,
     -85 * TI_SCALE,  -32 * TI_SCALE,  -40 * TI_SCALE,  -85 * TI_SCALE
};

/* 90 edges */
static const uint8_t ti_edge_a[90] = {
      0,   0,   0,   1,   1,   1,   2,   2,   2,   3,
      3,   3,   4,   4,   4,   5,   5,   5,   6,   6,
      7,   7,   8,   8,   9,   9,  10,  10,  11,  11,
     12,  12,  12,  13,  13,  13,  14,  14,  14,  15,
     15,  15,  16,  16,  16,  17,  17,  17,  18,  18,
     18,  19,  19,  19,  20,  20,  20,  21,  21,  21,
     22,  22,  22,  23,  23,  23,  24,  24,  25,  25,
     26,  26,  27,  27,  28,  28,  29,  29,  30,  30,
     31,  31,  32,  32,  33,  33,  34,  34,  35,  35
};
static const uint8_t ti_edge_b[90] = {
      6,  38,  41,   7,  36,  39,   8,  37,  40,   9,
     44,  47,  10,  42,  45,  11,  43,  46,  50,  53,
     48,  51,  49,  52,  56,  59,  54,  57,  55,  58,
     24,  36,  38,  25,  36,  37,  26,  37,  38,  27,
     39,  44,  28,  40,  42,  29,  41,  43,  30,  42,
     50,  31,  43,  48,  32,  44,  49,  33,  45,  56,
     34,  46,  54,  35,  47,  55,  41,  48,  39,  49,
     40,  50,  47,  51,  45,  52,  46,  53,  53,  54,
     51,  55,  52,  56,  57,  59,  57,  58,  58,  59
};

/* 32 face normals in Q14 object space.
 * Faces 0-11:  pentagon normals — (±φ/√(1+φ²), 0, ±1/√(1+φ²)) and axis permutations.
 * Faces 12-31: hexagon normals  — ±9459 (≈1/√3) triples, and ±15305/±5846 pairs.
 */
static const int16_t ti_face_nx[32] = {
    -13937,  8614,     0,     0,     0, -8614, 13937, 13937,
         0, -8614,  8614, -13937, 15305,  9459, -9459, -15305,
     -5846, -9459,  9459,     0,  5846, 15305,  5846,  9459,
    -15305, -9459,     0, -9459, -5846,  9459,     0,     0
};
static const int16_t ti_face_ny[32] = {
         0, 13937,  8614,  8614, -8614, -13937,     0,     0,
     -8614, 13937, -13937,     0,  5846, -9459, -9459, -5846,
         0,  9459, -9459, -15305,     0, -5846,     0,  9459,
      5846, -9459, 15305,  9459,     0,  9459, -15305, 15305
};
static const int16_t ti_face_nz[32] = {
      8614,     0, -13937, 13937, 13937,     0,  8614, -8614,
    -13937,     0,     0, -8614,     0,  9459,  9459,     0,
     15305, -9459, -9459, -5846, -15305,     0, 15305,  9459,
         0, -9459,  5846,  9459, -15305, -9459,  5846, -5846
};

/* Per-edge adjacent face indices */
static const uint8_t ti_edge_face0[90] = {
     16,   3,   3,  26,   1,   1,  12,   6,   6,  20,
      2,   2,  19,  10,  10,  15,   0,   0,   4,   4,
      9,   9,   7,   7,   8,   8,   5,   5,  11,  11,
      3,  23,   3,   1,   1,  12,   6,   6,  22,   2,
     29,   2,  10,  13,  10,   0,  16,   0,   4,  13,
      4,   9,  24,   9,   7,  20,   7,   8,  18,   8,
      5,  14,   5,  11,  17,  11,   3,  26,   1,  12,
      6,  13,   2,  17,  10,  18,   0,  14,   4,  14,
      9,  17,   7,  18,  19,   8,   5,  15,  11,  25
};
static const uint8_t ti_edge_face1[90] = {
     22,  22,  16,  31,  26,  31,  21,  12,  21,  28,
     20,  28,  30,  30,  19,  24,  24,  15,  22,  16,
     26,  31,  12,  21,  20,  28,  30,  19,  24,  15,
     26,  26,  23,  12,  23,  23,  22,  23,  23,  31,
     31,  29,  21,  21,  13,  16,  27,  27,  30,  30,
     13,  24,  27,  27,  20,  29,  29,  19,  19,  18,
     15,  15,  14,  28,  28,  17,  27,  27,  29,  29,
     13,  22,  17,  31,  18,  21,  14,  16,  14,  30,
     17,  24,  18,  20,  25,  25,  25,  25,  25,  28
};

static const uint16_t ti_edge_color[90] = {
    /*  0- 9 */ 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A,
    /* 10-19 */ 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0707, 0x0707,
    /* 20-29 */ 0x0707, 0x0707, 0x0707, 0x0707, 0x0707, 0x0707, 0x0707, 0x0707, 0x0707, 0x0707,
    /* 30-39 */ 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707,
    /* 40-49 */ 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A,
    /* 50-59 */ 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707,
    /* 60-69 */ 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0A0A,
    /* 70-79 */ 0x0707, 0x0A0A, 0x0707, 0x0A0A, 0x0707, 0x0A0A, 0x0707, 0x0A0A, 0x0707, 0x0A0A,
    /* 80-89 */ 0x0707, 0x0A0A, 0x0707, 0x0A0A, 0x0A0A, 0x0707, 0x0707, 0x0A0A, 0x0707, 0x0A0A,
};

const Model3D g_model_truncated_icosahedron = {
    .vertex_count = 60,
    .edge_count   = 90,
    .center_x = 0,
    .center_y = 0,
    .center_z = 0,
    .radius   = 99,

    .object_color = 0x0D0B,

    .vx = ti_vx,
    .vy = ti_vy,
    .vz = ti_vz,

    .edge_a = ti_edge_a,
    .edge_b = ti_edge_b,

    .face_count = 32,
    .face_nx = ti_face_nx,
    .face_ny = ti_face_ny,
    .face_nz = ti_face_nz,

    .edge_face0 = ti_edge_face0,
    .edge_face1 = ti_edge_face1,
    .edge_color_count = 90,
    .edge_color = ti_edge_color
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
