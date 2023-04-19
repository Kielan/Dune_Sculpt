#pragma once

/** NOTE: do NOT modify topology while walking a mesh! **/

typedef enum {
  MESH_W_DEPTH_FIRST,
  MESH_W_BREADTH_FIRST,
} MeshWalkOrder;

typedef enum {
  MESH_WALKERS_FLAG_NOP = 0,
  MESH_WALKERS_FLAG_TEST_HIDDEN = (1 << 0),
} MeshWalkersFlag;

/*Walkers*/
typedef struct MeshWalker {
  char begin_htype; /* only for validating input */
  void (*begin)(struct MeshWalker *walker, void *start);
  void *(*step)(struct MeshMWalker *walker);
  void *(*yield)(struct MeshWalker *walker);
  int structsize;
  MeshWalkOrder order;
  int valid_mask;

  /* runtime */
  int layer;

  Mesh *mesh;
  lib_mempool *worklist;
  ListBase states;

  /* these masks are to be tested against elements BMO_elem_flag_test(),
   * should never be accessed directly only through BMW_init() and bmw_mask_check_*() functions */
  short mask_vert;
  short mask_edge;
  short mask_face;

  MeshWalkerFlag flag;

  struct GSet *visit_set;
  struct GSet *visit_set_alt;
  int depth;
} MeshWalker;

/* define to make mesh_walker_init more clear */
#define MESH_WALKER_MASK_NOP 0

/**
 * Init Walker
 *
 * Allocates and returns a new mesh walker of a given type.
 * The elements visited are filtered by the bitmask 'searchmask'.
 */
void mesh_walker_init(struct MeshWalker *walker,
                      Mesh *mesh,
                      int type,
                      short mask_vert,
                      short mask_edge,
                      short mask_face,
                      MeshWalkerFlag flag,
              int layer);
void *mesh_walker_begin(MeshWalker *walker, void *start);
/** Step Walker **/
void *mesh_walker_step(struct MeshWalker *walker);
/** End Walker Frees a walker's worklist. */
void mesh_walker_end(struct MeshWalker *walker);
/** Walker Current Depth Returns the current depth of the walker. */
int mesh_walker_current_depth(MeshWalker *walker);

/* These are used by custom walkers. */
/**
 * Current Walker State
 *
 * Returns the first state from the walker state
 * worklist. This state is the next in the
 * worklist for processing.
 */
void *mesh_waker_current_state(MeshWalker *walker);
/**
 * brief Add a new Walker State
 *
 * Allocate a new empty state and put it on the worklist.
 * A pointer to the new state is returned so that the caller
 * can fill in the state data. The new state will be inserted
 * at the front for depth-first walks, and at the end for
 * breadth-first walks.
 */
void *mesh_walker_state_add(MeshWalker *walker);
/**
 * Remove Current Walker State
 *
 * Remove and free an item from the end of the walker state
 * worklist.
 */
void mesh_walker_state_remove(MeshWalker *walker);
/**
 * Main Walking Function
 *
 * Steps a mesh walker forward by one element
 */
void *mesh_walk(MeshWalker *walker);
/**
 * Reset Walker
 *
 * Frees all states from the worklist, resetting the walker
 * for reuse in a new walk.
 */
void mesh_walker_reset(MeshWalker *walker);

#define MESH_W_ITER(ele, walker, data) \
  for (MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = BMW_begin(walker, (BM_CHECK_TYPE_ELEM(data), data)); ele; \
       MESH_CHECK_TYPE_ELEM_ASSIGN(ele) = BMW_step(walker))

/*
 * example of usage, walking over an island of tool flagged faces:
 *
 * MeshWalker walker;
 * MeshFace *f;
 *
 * mesh_walker_init(&walker, mesh, MESH_W_ISLAND, SOME_OP_FLAG);
 *
 * for (f = mesh_walker_begin(&walker, some_start_face); f; f = mesh_walker_step(&walker)) {
 *     // do something with f
 * }
 * mesh_walker_end(&walker);
 */

enum {
  BMW_VERT_SHELL,
  BMW_LOOP_SHELL,
  BMW_LOOP_SHELL_WIRE,
  BMW_FACE_SHELL,
  BMW_EDGELOOP,
  BMW_FACELOOP,
  BMW_EDGERING,
  BMW_EDGEBOUNDARY,
  BMW_EDGELOOP_NONMANIFOLD,
  /* BMW_RING, */
  BMW_LOOPDATA_ISLAND,
  BMW_ISLANDBOUND,
  BMW_ISLAND,
  BMW_ISLAND_MANIFOLD,
  BMW_CONNECTED_VERTEX,
  /* end of array index enum vals */

  /* Do not initialize function pointers and struct size in #BMW_init. */
  BMW_CUSTOM,
  BMW_MAXWALKERS,
};

/* use with BMW_init, so as not to confuse with restrict flags */
#define BMW_NIL_LAY 0
