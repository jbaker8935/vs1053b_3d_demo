/**
 * @file 3d_math.h
 * @brief Basic 3D vector types and trigonometric look-up table used
 *        throughout the geometry kernel and application code.
 *
 * All coordinate values are signed 16-bit integers.  World-space coordinates
 * use the range –16384..+16383.  Screen-space coordinates are in pixels.
 * Angles are expressed as 8-bit unsigned indices into @ref sin_table (one
 * full turn = 256 units).
 */
#ifndef MATH_3D_H
#define MATH_3D_H

#include <stdint.h>

/**
 * @brief 3-component integer vector used for world-space positions, object
 *        centers, and bounding-sphere centers.
 */
typedef struct {
    int16_t x; /**< X component. */
    int16_t y; /**< Y component. */
    int16_t z; /**< Z component. */
} vec3_t;

/**
 * @brief 256-entry sine look-up table in Q14 fixed-point format.
 *
 * Index 0..255 maps uniformly to angle 0..359.86° (one full turn = 256
 * steps).  Each entry is a signed 16-bit Q14 value, so the representable
 * range is –1.0 (0xC000) to +1.0 (0x4000).
 *
 * To obtain cos(θ), read sin_table[(θ + 64) & 0xFF].
 */
extern const int16_t sin_table[256];

#endif // MATH_3D_H
