
#pragma once

/**
 * Private function prototypes for mesh public API.
 * This file is a grab-bag of functions from various
 * parts of the mesh internals.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* returns positive nonzero on error */

#ifdef NDEBUG
/* No error checking for release,
 * it can take most of the CPU time when running some tools. */
#  define MESH_CHECK_ELEMENT(el) (void)(el)
#else
/**
 * Check the element is valid.
 *
 * MESH_TODO, when this raises an error the output is incredibly confusing.
 * need to have some nice way to print/debug what the heck's going on.
 */
int mesh_elem_check(void *element, char htype);
#  define MESH_CHECK_ELEMENT(el) \
    { \
      if (mesh_elem_check(el, ((MeshHeader *)el)->htype)) { \
        printf( \
            "check_element failure, with code %i on line %i in file\n" \
            "    \"%s\"\n\n", \
            mesh_elem_check(el, ((MeshHeader *)el)->htype), \
            __LINE__, \
            __FILE__); \
      } \
    } \
    ((void)0)
#endif

int mesh_radial_length(const MeshLoop *l);
int mesh_disk_count_at_most(const MeshVert *v, int count_max);
int mesh_disk_count(const MeshVert *v);

/**
 * Internal MeshHeader.api_flag
 * Ensure different parts of the API do not conflict
 * on using these internal flags!
 */
enum {
  _FLAG_JF = (1 << 0),       /* Join faces. */
  _FLAG_MF = (1 << 1),       /* Make face. */
  _FLAG_MV = (1 << 1),       /* Make face, vertex. */
  _FLAG_OVERLAP = (1 << 2),  /* General overlap flag. */
  _FLAG_WALK = (1 << 3),     /* General walk flag (keep clean). */
  _FLAG_WALK_ALT = (1 << 4), /* Same as _FLAG_WALK, for when a second tag is needed. */

  _FLAG_ELEM_CHECK = (1 << 7), /* Reserved for mesh_elem_check. */
};

#define MESH_ELEM_API_FLAG_ENABLE(element, f) \
  { \
    ((element)->head.api_flag |= (f)); \
  } \
  (void)0
#define MESH_ELEM_API_FLAG_DISABLE(element, f) \
  { \
    ((element)->head.api_flag &= (uchar) ~(f)); \
  } \
  (void)0
#define MESH_ELEM_API_FLAG_TEST(element, f) ((element)->head.api_flag & (f))
#define MESH_ELEM_API_FLAG_CLEAR(element) \
  { \
    ((element)->head.api_flag = 0); \
  } \
  (void)0

/**
 * POLY ROTATE PLANE
 * Rotates a polygon so that its
 * normal is pointing towards the mesh Z axis
 */
void poly_rotate_plane(const float normal[3], float (*verts)[3], uint nverts);

/* include the rest of our private declarations */
#include "mesh_structure.h"

#ifdef __cplusplus
}
#endif
