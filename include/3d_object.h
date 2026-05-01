/**
 * @file 3d_object.h
 * @brief 3D geometry, object, and camera data types used by the VS1053b
 *        geometry kernel and host application.
 *
 * Three primary types are defined here:
 *  - @ref Geom3D   – raw geometry (vertices, edges, optional face normals)
 *  - @ref Object3D – a renderable object that pairs geometry with color data
 *  - @ref Camera   – world-space camera position and orientation
 *
 * All coordinate values are signed 16-bit integers in object/world space
 * (range ±16383).  Face normals are Q14 fixed-point unit vectors.
 * Angle fields are 8-bit indices into the 256-entry @ref sin_table.
 */
#ifndef OBJECT_3D_H
#define OBJECT_3D_H

#include <stdbool.h>
#include "../include/3d_math.h"
#include <stdint.h>
#include "oscar64_compat.h"

/**
 * @brief Sentinel value indicating an edge has no adjacent face.
 *
 * Use this value in @ref Geom3D::edge_face0 or @ref Geom3D::edge_face1 for
 * boundary edges (e.g. planar surfaces, open meshes, or line segments) that
 * do not bound a polygon.
 */
#define GEOM3D_INVALID_FACE 0xFF

/**
 * @brief Geometry descriptor for a 3D wireframe model.
 *
 * Contains vertex positions, edge index pairs, and optional face-normal and
 * edge-adjacency data needed for hidden-line removal.
 *
 * ### Face adjacency and hidden-line removal
 * Faces are implicitly defined by edge adjacency: each edge record stores the
 * indices of the (up to two) faces that share that edge.  For a closed mesh
 * every edge has exactly two adjacent faces.  Boundary edges (open meshes,
 * planar surfaces, or lone line segments) should store @ref GEOM3D_INVALID_FACE
 * in the unused adjacency slot.
 *
 * Face normals must point **outward** from the surface.  Most 3-D modelling
 * tools (e.g. Blender) export consistent outward normals by default;
 * a conversion script can transform them into the Q14 integer format
 * required here.
 *
 * ### Dynamic loading
 * Although the built-in models used here are statically allocated, a @ref Geom3D can be
 * dynamically allocated, filled from storage (e.g. disk/flash), and uploaded
 * to the plugin with @ref vgk_model_save.  After the upload the CPU-side
 * structure (and the arrays it points to) may be freed — the plugin retains
 * its own copy in DSP X-RAM save slots.
 */
typedef struct {
    uint8_t vertex_count; /**< Number of vertices (max @ref VGK_MAX_VERTICES). */
    uint8_t edge_count;   /**< Number of edges    (max @ref VGK_MAX_EDGES).    */

    /** @name Bounding sphere
     *  Object-space bounding sphere used for culling and collision detection.
     *  The center is typically (0,0,0) for centered models but can be offset
     *  for asymmetric objects.
     * @{
     */
    int16_t center_x; /**< Bounding-sphere center X in object space. */
    int16_t center_y; /**< Bounding-sphere center Y in object space. */
    int16_t center_z; /**< Bounding-sphere center Z in object space. */
    int16_t radius;   /**< Bounding-sphere radius. */
    /** @} */

    /** @name Vertex arrays  (length = vertex_count)
     * @{ */
    const int16_t* vx; /**< Vertex X coordinates. */
    const int16_t* vy; /**< Vertex Y coordinates. */
    const int16_t* vz; /**< Vertex Z coordinates. */
    /** @} */

    /** @name Edge arrays  (length = edge_count)
     *  Each edge is defined by two vertex indices into the vx/vy/vz arrays.
     * @{ */
    const uint8_t* edge_a; /**< First  vertex index of each edge. */
    const uint8_t* edge_b; /**< Second vertex index of each edge. */
    /** @} */

    /** @name Hidden-line metadata  (optional; set face_count to 0 and pointers to NULL to omit)
     *
     *  Face normals are Q14 unit vectors in object space (one triple per face).
     *  Edge-face arrays are per-edge adjacency records: each entry holds the
     *  index of one adjacent face, or @ref GEOM3D_INVALID_FACE for boundary
     *  edges.  The kernel uses back-face culling against these normals to
     *  determine which edges are hidden.
     *
     *  All three face-normal arrays and both edge-face arrays must be non-NULL
     *  (and face_count > 0) for hidden-line removal to be activated.
     * @{ */
    uint8_t face_count;       /**< Number of faces (max @ref VGK_MAX_FACES). */
    const int16_t* face_nx;   /**< Face normal X components (Q14, length = face_count). */
    const int16_t* face_ny;   /**< Face normal Y components (Q14, length = face_count). */
    const int16_t* face_nz;   /**< Face normal Z components (Q14, length = face_count). */
    const uint8_t* edge_face0; /**< Index of first  adjacent face per edge, or GEOM3D_INVALID_FACE. */
    const uint8_t* edge_face1; /**< Index of second adjacent face per edge, or GEOM3D_INVALID_FACE. */
    /** @} */

} __attribute__((packed)) Geom3D;

/**
 * @brief A renderable 3D object: geometry reference plus color data.
 *
 * Pairs a pointer to a @ref Geom3D geometry descriptor with color information
 * for the rendering pass.  The geometry pointer may be set to NULL after
 * @ref vgk_model_save has uploaded it to the plugin — the kernel stores its
 * own copy in DSP save-slot memory.
 *
 * ### Color encoding
 * @ref object_color is a 16-bit packed palette index:
 *  - **High byte** – palette index used for edges classified as *far* (behind
 *    the depth midpoint).
 *  - **Low byte**  – palette index used for edges classified as *near* (in
 *    front of the depth midpoint).
 *
 * Per-edge color overrides follow the same packed encoding and are stored in
 * the @ref edge_color array.  The kernel emits a one-word descriptor before
 * each edge in the stream (when descriptors are enabled) with bit 15 = near
 * flag, bits 8–14 = save-slot index, and bits 0–7 = edge index within the
 * slot.  @ref vgk_scrn_edges_render uses this descriptor to look up the
 * correct color for each edge.
 */
typedef struct {
    const Geom3D* geometry;    /**< Pointer to geometry data; may be NULL after upload. */

    /**
     * @brief Object-level near/far color: high byte = far palette index,
     *        low byte = near palette index.
     *
     * Used when @ref vgk_edge_coloring mode is @ref VGK_EC_NEAR_FAR and no
     * per-edge override is present.
     */
    uint16_t object_color;

    uint8_t edge_color_count;   /**< Number of entries in @ref edge_color (= geometry->edge_count, or 0). */

    /**
     * @brief Optional per-edge near/far color overrides (length = edge_color_count).
     *
     * Each entry uses the same packed encoding as @ref object_color
     * (high byte = far, low byte = near).  Set to NULL and edge_color_count
     * to 0 to use @ref object_color for all edges.  Used when
     * @ref vgk_edge_coloring mode is @ref VGK_EC_EDGE_COLORING.
     */
    const uint16_t* edge_color;
} __attribute__((packed)) Object3D;

/** @name Built-in model instances
 *  Pre-built @ref Object3D descriptors defined in the vgm_assets module.
 * @{ */
extern const Object3D g_model_projectile;           /**< Small projectile / dot shape.         */
extern const Object3D g_model_starfield;             /**< Starfield point-cloud.                */
extern const Object3D g_model_cube;                  /**< Unit cube (12 edges, 6 faces).        */
extern const Object3D g_model_anaconda;              /**< Anaconda ship wireframe.              */
extern const Object3D g_model_truncated_octahedron;  /**< Truncated octahedron solid.           */
extern const Object3D g_model_truncated_icosahedron; /**< Truncated icosahedron (soccer ball).  */
/** @} */

/**
 * @brief World-space camera state.
 *
 * Stores the camera's world position, Euler orientation angles, and a dirty
 * flag that allows the application to skip redundant camera-parameter uploads
 * when the camera has not moved between frames.
 */
typedef struct {
    vec3_t position;  /**< World-space camera position. */
    uint8_t yaw;      /**< Yaw   angle (sin_table index, 0..255). */
    uint8_t pitch;    /**< Pitch angle (sin_table index, 0..255). */
    uint8_t roll;     /**< Roll  angle (sin_table index, 0..255). */
    bool moved;       /**< True when position or orientation changed since last upload. */
} __attribute__((packed)) Camera;

/**
 * @brief Initialise a @ref Camera to a given world position with all angles
 *        set to zero and @c moved = true.
 *
 * @param cam  Pointer to the Camera to initialise.
 * @param pos  Initial world-space position.
 */
void camera_init(Camera *cam, vec3_t pos);

#endif // OBJECT_3D_H
