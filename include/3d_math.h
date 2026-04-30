#ifndef MATH_3D_H
#define MATH_3D_H

#include <stdint.h>

// 3D vector
typedef struct {
    int16_t x, y, z;
} vec3_t;

// 2D vector for screen
typedef struct {
    int16_t x, y;
} vec2_t;

extern const int16_t sin_table[256];

#endif // MATH_3D_H
