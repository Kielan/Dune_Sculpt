#include <cmath>
#include <cstdio>
#include <cstring>

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_pointcloud_types.h"

#include "BLI_linklist.h"
#include "BLI_math.h"
#include "BLI_task.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BKE_bvhutils.h"
#include "BKE_editmesh.h"
#include "BKE_mesh.h"
#include "BKE_mesh_runtime.h"

#include "MEM_guardedalloc.h"

/* -------------------------------------------------------------------- */
/** \name BVHCache
 * \{ */

struct BVHCacheItem {
  bool is_filled;
  BVHTree *tree;
};

struct BVHCache {
  BVHCacheItem items[BVHTREE_MAX_ITEM];
  ThreadMutex mutex;
};

/**
 * Queries a bvhcache for the cache bvhtree of the request type
 *
 * When the `r_locked` is filled and the tree could not be found the caches mutex will be
 * locked. This mutex can be unlocked by calling `bvhcache_unlock`.
 *
 * When `r_locked` is used the `mesh_eval_mutex` must contain the `Mesh_Runtime.eval_mutex`.
 */
static bool bvhcache_find(BVHCache **bvh_cache_p,
                          BVHCacheType type,
                          BVHTree **r_tree,
                          bool *r_locked,
                          ThreadMutex *mesh_eval_mutex)
{
  bool do_lock = r_locked;
  if (r_locked) {
    *r_locked = false;
  }
  if (*bvh_cache_p == nullptr) {
    if (!do_lock) {
      /* Cache does not exist and no lock is requested. */
      return false;
    }
    /* Lazy initialization of the bvh_cache using the `mesh_eval_mutex`. */
    BLI_mutex_lock(mesh_eval_mutex);
    if (*bvh_cache_p == nullptr) {
      *bvh_cache_p = bvhcache_init();
    }
    BLI_mutex_unlock(mesh_eval_mutex);
  }
  BVHCache *bvh_cache = *bvh_cache_p;

  if (bvh_cache->items[type].is_filled) {
    *r_tree = bvh_cache->items[type].tree;
    return true;
  }
  if (do_lock) {
    BLI_mutex_lock(&bvh_cache->mutex);
    bool in_cache = bvhcache_find(bvh_cache_p, type, r_tree, nullptr, nullptr);
    if (in_cache) {
      BLI_mutex_unlock(&bvh_cache->mutex);
      return in_cache;
    }
    *r_locked = true;
  }
  return false;
}

static void bvhcache_unlock(BVHCache *bvh_cache, bool lock_started)
{
  if (lock_started) {
    BLI_mutex_unlock(&bvh_cache->mutex);
  }
}

bool bvhcache_has_tree(const BVHCache *bvh_cache, const BVHTree *tree)
{
  if (bvh_cache == nullptr) {
    return false;
  }

  for (int i = 0; i < BVHTREE_MAX_ITEM; i++) {
    if (bvh_cache->items[i].tree == tree) {
      return true;
    }
  }
  return false;
}

BVHCache *bvhcache_init()
{
  BVHCache *cache = MEM_cnew<BVHCache>(__func__);
  BLI_mutex_init(&cache->mutex);
  return cache;
}
/**
 * Inserts a BVHTree of the given type under the cache
 * After that the caller no longer needs to worry when to free the BVHTree
 * as that will be done when the cache is freed.
 *
 * A call to this assumes that there was no previous cached tree of the given type
 * \warning The #BVHTree can be nullptr.
 */
static void bvhcache_insert(BVHCache *bvh_cache, BVHTree *tree, BVHCacheType type)
{
  BVHCacheItem *item = &bvh_cache->items[type];
  BLI_assert(!item->is_filled);
  item->tree = tree;
  item->is_filled = true;
}

void bvhcache_free(BVHCache *bvh_cache)
{
  for (int index = 0; index < BVHTREE_MAX_ITEM; index++) {
    BVHCacheItem *item = &bvh_cache->items[index];
    BLI_bvhtree_free(item->tree);
    item->tree = nullptr;
  }
  BLI_mutex_end(&bvh_cache->mutex);
  MEM_freeN(bvh_cache);
}

/**
 * BVH-tree balancing inside a mutex lock must be run in isolation. Balancing
 * is multithreaded, and we do not want the current thread to start another task
 * that may involve acquiring the same mutex lock that it is waiting for.
 */
static void bvhtree_balance_isolated(void *userdata)
{
  BLI_bvhtree_balance((BVHTree *)userdata);
}

static void bvhtree_balance(BVHTree *tree, const bool isolate)
{
  if (tree) {
    if (isolate) {
      BLI_task_isolate(bvhtree_balance_isolated, tree);
    }
    else {
      BLI_bvhtree_balance(tree);
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Local Callbacks
 * \{ */

/* Math stuff for ray casting on mesh faces and for nearest surface */

float bvhtree_ray_tri_intersection(const BVHTreeRay *ray,
                                   const float UNUSED(m_dist),
                                   const float v0[3],
                                   const float v1[3],
                                   const float v2[3])
{
  float dist;

#ifdef USE_KDOPBVH_WATERTIGHT
  if (isect_ray_tri_watertight_v3(ray->origin, ray->isect_precalc, v0, v1, v2, &dist, nullptr))
#else
  if (isect_ray_tri_epsilon_v3(
          ray->origin, ray->direction, v0, v1, v2, &dist, nullptr, FLT_EPSILON))
#endif
  {
    return dist;
  }

  return FLT_MAX;
}

float bvhtree_sphereray_tri_intersection(const BVHTreeRay *ray,
                                         float radius,
                                         const float m_dist,
                                         const float v0[3],
                                         const float v1[3],
                                         const float v2[3])
{

  float idist;
  float p1[3];
  float hit_point[3];

  madd_v3_v3v3fl(p1, ray->origin, ray->direction, m_dist);
  if (isect_sweeping_sphere_tri_v3(ray->origin, p1, radius, v0, v1, v2, &idist, hit_point)) {
    return idist * m_dist;
  }

  return FLT_MAX;
}

/*
 * BVH from meshes callbacks
 */

/**
 * Callback to BVH-tree nearest point.
 * The tree must have been built using #bvhtree_from_mesh_faces.
 *
 * \param userdata: Must be a #BVHMeshCallbackUserdata built from the same mesh as the tree.
 */
static void mesh_faces_nearest_point(void *userdata,
                                     int index,
                                     const float co[3],
                                     BVHTreeNearest *nearest)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const MVert *vert = data->vert;
  const MFace *face = data->face + index;

  const float *t0, *t1, *t2, *t3;
  t0 = vert[face->v1].co;
  t1 = vert[face->v2].co;
  t2 = vert[face->v3].co;
  t3 = face->v4 ? vert[face->v4].co : nullptr;

  do {
    float nearest_tmp[3], dist_sq;

    closest_on_tri_to_point_v3(nearest_tmp, co, t0, t1, t2);
    dist_sq = len_squared_v3v3(co, nearest_tmp);

    if (dist_sq < nearest->dist_sq) {
      nearest->index = index;
      nearest->dist_sq = dist_sq;
      copy_v3_v3(nearest->co, nearest_tmp);
      normal_tri_v3(nearest->no, t0, t1, t2);
    }

    t1 = t2;
    t2 = t3;
    t3 = nullptr;

  } while (t2);
}
/* copy of function above */
static void mesh_looptri_nearest_point(void *userdata,
                                       int index,
                                       const float co[3],
                                       BVHTreeNearest *nearest)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const MVert *vert = data->vert;
  const MLoopTri *lt = &data->looptri[index];
  const float *vtri_co[3] = {
      vert[data->loop[lt->tri[0]].v].co,
      vert[data->loop[lt->tri[1]].v].co,
      vert[data->loop[lt->tri[2]].v].co,
  };
  float nearest_tmp[3], dist_sq;

  closest_on_tri_to_point_v3(nearest_tmp, co, UNPACK3(vtri_co));
  dist_sq = len_squared_v3v3(co, nearest_tmp);

  if (dist_sq < nearest->dist_sq) {
    nearest->index = index;
    nearest->dist_sq = dist_sq;
    copy_v3_v3(nearest->co, nearest_tmp);
    normal_tri_v3(nearest->no, UNPACK3(vtri_co));
  }
}
/* copy of function above (warning, should de-duplicate with editmesh_bvh.c) */
static void editmesh_looptri_nearest_point(void *userdata,
                                           int index,
                                           const float co[3],
                                           BVHTreeNearest *nearest)
{
  const BVHTreeFromEditMesh *data = (const BVHTreeFromEditMesh *)userdata;
  BMEditMesh *em = data->em;
  const BMLoop **ltri = (const BMLoop **)em->looptris[index];

  const float *t0, *t1, *t2;
  t0 = ltri[0]->v->co;
  t1 = ltri[1]->v->co;
  t2 = ltri[2]->v->co;

  {
    float nearest_tmp[3], dist_sq;

    closest_on_tri_to_point_v3(nearest_tmp, co, t0, t1, t2);
    dist_sq = len_squared_v3v3(co, nearest_tmp);

    if (dist_sq < nearest->dist_sq) {
      nearest->index = index;
      nearest->dist_sq = dist_sq;
      copy_v3_v3(nearest->co, nearest_tmp);
      normal_tri_v3(nearest->no, t0, t1, t2);
    }
  }
}

/**
 * Callback to BVH-tree ray-cast.
 * The tree must have been built using bvhtree_from_mesh_faces.
 *
 * \param userdata: Must be a #BVHMeshCallbackUserdata built from the same mesh as the tree.
 */
static void mesh_faces_spherecast(void *userdata,
                                  int index,
                                  const BVHTreeRay *ray,
                                  BVHTreeRayHit *hit)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const MVert *vert = data->vert;
  const MFace *face = &data->face[index];

  const float *t0, *t1, *t2, *t3;
  t0 = vert[face->v1].co;
  t1 = vert[face->v2].co;
  t2 = vert[face->v3].co;
  t3 = face->v4 ? vert[face->v4].co : nullptr;

  do {
    float dist;
    if (ray->radius == 0.0f) {
      dist = bvhtree_ray_tri_intersection(ray, hit->dist, t0, t1, t2);
    }
    else {
      dist = bvhtree_sphereray_tri_intersection(ray, ray->radius, hit->dist, t0, t1, t2);
    }

    if (dist >= 0 && dist < hit->dist) {
      hit->index = index;
      hit->dist = dist;
      madd_v3_v3v3fl(hit->co, ray->origin, ray->direction, dist);

      normal_tri_v3(hit->no, t0, t1, t2);
    }

    t1 = t2;
    t2 = t3;
    t3 = nullptr;

  } while (t2);
}
/* copy of function above */
static void mesh_looptri_spherecast(void *userdata,
                                    int index,
                                    const BVHTreeRay *ray,
                                    BVHTreeRayHit *hit)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const MVert *vert = data->vert;
  const MLoopTri *lt = &data->looptri[index];
  const float *vtri_co[3] = {
      vert[data->loop[lt->tri[0]].v].co,
      vert[data->loop[lt->tri[1]].v].co,
      vert[data->loop[lt->tri[2]].v].co,
  };
  float dist;

  if (ray->radius == 0.0f) {
    dist = bvhtree_ray_tri_intersection(ray, hit->dist, UNPACK3(vtri_co));
  }
  else {
    dist = bvhtree_sphereray_tri_intersection(ray, ray->radius, hit->dist, UNPACK3(vtri_co));
  }

  if (dist >= 0 && dist < hit->dist) {
    hit->index = index;
    hit->dist = dist;
    madd_v3_v3v3fl(hit->co, ray->origin, ray->direction, dist);

    normal_tri_v3(hit->no, UNPACK3(vtri_co));
  }
}
/* copy of function above (warning, should de-duplicate with editmesh_bvh.c) */
static void editmesh_looptri_spherecast(void *userdata,
                                        int index,
                                        const BVHTreeRay *ray,
                                        BVHTreeRayHit *hit)
{
  const BVHTreeFromEditMesh *data = (BVHTreeFromEditMesh *)userdata;
  BMEditMesh *em = data->em;
  const BMLoop **ltri = (const BMLoop **)em->looptris[index];

  const float *t0, *t1, *t2;
  t0 = ltri[0]->v->co;
  t1 = ltri[1]->v->co;
  t2 = ltri[2]->v->co;

  {
    float dist;
    if (ray->radius == 0.0f) {
      dist = bvhtree_ray_tri_intersection(ray, hit->dist, t0, t1, t2);
    }
    else {
      dist = bvhtree_sphereray_tri_intersection(ray, ray->radius, hit->dist, t0, t1, t2);
    }

    if (dist >= 0 && dist < hit->dist) {
      hit->index = index;
      hit->dist = dist;
      madd_v3_v3v3fl(hit->co, ray->origin, ray->direction, dist);

      normal_tri_v3(hit->no, t0, t1, t2);
    }
  }
}

/**
 * Callback to BVH-tree nearest point.
 * The tree must have been built using #bvhtree_from_mesh_edges.
 *
 * \param userdata: Must be a #BVHMeshCallbackUserdata built from the same mesh as the tree.
 */
static void mesh_edges_nearest_point(void *userdata,
                                     int index,
                                     const float co[3],
                                     BVHTreeNearest *nearest)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const MVert *vert = data->vert;
  const MEdge *edge = data->edge + index;
  float nearest_tmp[3], dist_sq;

  const float *t0, *t1;
  t0 = vert[edge->v1].co;
  t1 = vert[edge->v2].co;

  closest_to_line_segment_v3(nearest_tmp, co, t0, t1);
  dist_sq = len_squared_v3v3(nearest_tmp, co);

  if (dist_sq < nearest->dist_sq) {
    nearest->index = index;
    nearest->dist_sq = dist_sq;
    copy_v3_v3(nearest->co, nearest_tmp);
    sub_v3_v3v3(nearest->no, t0, t1);
    normalize_v3(nearest->no);
  }
}

/* Helper, does all the point-sphere-cast work actually. */
static void mesh_verts_spherecast_do(int index,
                                     const float v[3],
                                     const BVHTreeRay *ray,
                                     BVHTreeRayHit *hit)
{
  float dist;
  const float *r1;
  float r2[3], i1[3];
  r1 = ray->origin;
  add_v3_v3v3(r2, r1, ray->direction);

  closest_to_line_segment_v3(i1, v, r1, r2);

  /* No hit if closest point is 'behind' the origin of the ray, or too far away from it. */
  if ((dot_v3v3v3(r1, i1, r2) >= 0.0f) && ((dist = len_v3v3(r1, i1)) < hit->dist)) {
    hit->index = index;
    hit->dist = dist;
    copy_v3_v3(hit->co, i1);
  }
}

static void editmesh_verts_spherecast(void *userdata,
                                      int index,
                                      const BVHTreeRay *ray,
                                      BVHTreeRayHit *hit)
{
  const BVHTreeFromEditMesh *data = (const BVHTreeFromEditMesh *)userdata;
  BMVert *eve = BM_vert_at_index(data->em->bm, index);

  mesh_verts_spherecast_do(index, eve->co, ray, hit);
}

/**
 * Callback to BVH-tree ray-cast.
 * The tree must have been built using bvhtree_from_mesh_verts.
 *
 * \param userdata: Must be a #BVHMeshCallbackUserdata built from the same mesh as the tree.
 */
static void mesh_verts_spherecast(void *userdata,
                                  int index,
                                  const BVHTreeRay *ray,
                                  BVHTreeRayHit *hit)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const float *v = data->vert[index].co;

  mesh_verts_spherecast_do(index, v, ray, hit);
}

/**
 * Callback to BVH-tree ray-cast.
 * The tree must have been built using bvhtree_from_mesh_edges.
 *
 * \param userdata: Must be a #BVHMeshCallbackUserdata built from the same mesh as the tree.
 */
static void mesh_edges_spherecast(void *userdata,
                                  int index,
                                  const BVHTreeRay *ray,
                                  BVHTreeRayHit *hit)
{
  const BVHTreeFromMesh *data = (BVHTreeFromMesh *)userdata;
  const MVert *vert = data->vert;
  const MEdge *edge = &data->edge[index];

  const float radius_sq = square_f(ray->radius);
  float dist;
  const float *v1, *v2, *r1;
  float r2[3], i1[3], i2[3];
  v1 = vert[edge->v1].co;
  v2 = vert[edge->v2].co;

  /* In case we get a zero-length edge, handle it as a point! */
  if (equals_v3v3(v1, v2)) {
    mesh_verts_spherecast_do(index, v1, ray, hit);
    return;
  }

  r1 = ray->origin;
  add_v3_v3v3(r2, r1, ray->direction);

  if (isect_line_line_v3(v1, v2, r1, r2, i1, i2)) {
    /* No hit if intersection point is 'behind' the origin of the ray, or too far away from it. */
    if ((dot_v3v3v3(r1, i2, r2) >= 0.0f) && ((dist = len_v3v3(r1, i2)) < hit->dist)) {
      const float e_fac = line_point_factor_v3(i1, v1, v2);
      if (e_fac < 0.0f) {
        copy_v3_v3(i1, v1);
      }
      else if (e_fac > 1.0f) {
        copy_v3_v3(i1, v2);
      }
      /* Ensure ray is really close enough from edge! */
      if (len_squared_v3v3(i1, i2) <= radius_sq) {
        hit->index = index;
        hit->dist = dist;
        copy_v3_v3(hit->co, i2);
      }
    }
  }
}

/** \} */

/*
 * BVH builders
 */

/* -------------------------------------------------------------------- */
/** \name Vertex Builder
 * \{ */

static BVHTree *bvhtree_from_editmesh_verts_create_tree(float epsilon,
                                                        int tree_type,
                                                        int axis,
                                                        BMEditMesh *em,
                                                        const BLI_bitmap *verts_mask,
                                                        int verts_num_active)
{
  BM_mesh_elem_table_ensure(em->bm, BM_VERT);
  const int verts_num = em->bm->totvert;
  if (verts_mask) {
    BLI_assert(IN_RANGE_INCL(verts_num_active, 0, verts_num));
  }
  else {
    verts_num_active = verts_num;
  }

  BVHTree *tree = BLI_bvhtree_new(verts_num_active, epsilon, tree_type, axis);

  if (tree) {
    for (int i = 0; i < verts_num; i++) {
      if (verts_mask && !BLI_BITMAP_TEST_BOOL(verts_mask, i)) {
        continue;
      }
      BMVert *eve = BM_vert_at_index(em->bm, i);
      BLI_bvhtree_insert(tree, i, eve->co, 1);
    }
    BLI_assert(BLI_bvhtree_get_len(tree) == verts_num_active);
  }

  return tree;
}

static BVHTree *bvhtree_from_mesh_verts_create_tree(float epsilon,
                                                    int tree_type,
                                                    int axis,
                                                    const MVert *vert,
                                                    const int verts_num,
                                                    const BLI_bitmap *verts_mask,
                                                    int verts_num_active)
{
  BVHTree *tree = nullptr;

  if (verts_mask) {
    BLI_assert(IN_RANGE_INCL(verts_num_active, 0, verts_num));
  }
  else {
    verts_num_active = verts_num;
  }

  if (verts_num_active) {
    tree = BLI_bvhtree_new(verts_num_active, epsilon, tree_type, axis);

    if (tree) {
      for (int i = 0; i < verts_num; i++) {
        if (verts_mask && !BLI_BITMAP_TEST_BOOL(verts_mask, i)) {
          continue;
        }
        BLI_bvhtree_insert(tree, i, vert[i].co, 1);
      }
      BLI_assert(BLI_bvhtree_get_len(tree) == verts_num_active);
    }
  }

  return tree;
}

static void bvhtree_from_mesh_verts_setup_data(BVHTreeFromMesh *data,
                                               BVHTree *tree,
                                               const bool is_cached,
                                               const MVert *vert,
                                               const bool vert_allocated)
{
  memset(data, 0, sizeof(*data));

  data->tree = tree;
  data->cached = is_cached;

  /* a nullptr nearest callback works fine
   * remember the min distance to point is the same as the min distance to BV of point */
  data->nearest_callback = nullptr;
  data->raycast_callback = mesh_verts_spherecast;

  data->vert = vert;
  data->vert_allocated = vert_allocated;
}

BVHTree *bvhtree_from_editmesh_verts_ex(BVHTreeFromEditMesh *data,
                                        BMEditMesh *em,
                                        const BLI_bitmap *verts_mask,
                                        int verts_num_active,
                                        float epsilon,
                                        int tree_type,
                                        int axis,
                                        const BVHCacheType bvh_cache_type,
                                        BVHCache **bvh_cache_p,
                                        ThreadMutex *mesh_eval_mutex)
{
  BVHTree *tree = nullptr;

  if (bvh_cache_p) {
    bool lock_started = false;
    data->cached = bvhcache_find(
        bvh_cache_p, bvh_cache_type, &data->tree, &lock_started, mesh_eval_mutex);

    if (data->cached == false) {
      tree = bvhtree_from_editmesh_verts_create_tree(
          epsilon, tree_type, axis, em, verts_mask, verts_num_active);
      bvhtree_balance(tree, true);

      /* Save on cache for later use */
      // printf("BVHTree built and saved on cache\n");
      bvhcache_insert(*bvh_cache_p, tree, bvh_cache_type);
      data->cached = true;
    }
    bvhcache_unlock(*bvh_cache_p, lock_started);
  }
  else {
    tree = bvhtree_from_editmesh_verts_create_tree(
        epsilon, tree_type, axis, em, verts_mask, verts_num_active);
    bvhtree_balance(tree, false);
  }

  if (tree) {
    memset(data, 0, sizeof(*data));
    data->tree = tree;
    data->em = em;
    data->nearest_callback = nullptr;
    data->raycast_callback = editmesh_verts_spherecast;
    data->cached = bvh_cache_p != nullptr;
  }

  return tree;
}

BVHTree *bvhtree_from_editmesh_verts(
    BVHTreeFromEditMesh *data, BMEditMesh *em, float epsilon, int tree_type, int axis)
{
  return bvhtree_from_editmesh_verts_ex(
      data, em, nullptr, -1, epsilon, tree_type, axis, BVHTREE_FROM_VERTS, nullptr, nullptr);
}

BVHTree *bvhtree_from_mesh_verts_ex(BVHTreeFromMesh *data,
                                    const MVert *vert,
                                    const int verts_num,
                                    const bool vert_allocated,
                                    const BLI_bitmap *verts_mask,
                                    int verts_num_active,
                                    float epsilon,
                                    int tree_type,
                                    int axis,
                                    const BVHCacheType bvh_cache_type,
                                    BVHCache **bvh_cache_p,
                                    ThreadMutex *mesh_eval_mutex)
{
  bool in_cache = false;
  bool lock_started = false;
  BVHTree *tree = nullptr;
  if (bvh_cache_p) {
    in_cache = bvhcache_find(bvh_cache_p, bvh_cache_type, &tree, &lock_started, mesh_eval_mutex);
  }

  if (in_cache == false) {
    tree = bvhtree_from_mesh_verts_create_tree(
        epsilon, tree_type, axis, vert, verts_num, verts_mask, verts_num_active);
    bvhtree_balance(tree, bvh_cache_p != nullptr);

    if (bvh_cache_p) {
      /* Save on cache for later use */
      // printf("BVHTree built and saved on cache\n");
      BVHCache *bvh_cache = *bvh_cache_p;
      bvhcache_insert(bvh_cache, tree, bvh_cache_type);
      in_cache = true;
    }
  }

  if (bvh_cache_p) {
    bvhcache_unlock(*bvh_cache_p, lock_started);
  }

  /* Setup BVHTreeFromMesh */
  bvhtree_from_mesh_verts_setup_data(data, tree, in_cache, vert, vert_allocated);

  return tree;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edge Builder
 * \{ */

static BVHTree *bvhtree_from_editmesh_edges_create_tree(float epsilon,
                                                        int tree_type,
                                                        int axis,
                                                        BMEditMesh *em,
                                                        const BLI_bitmap *edges_mask,
                                                        int edges_num_active)
{
  BM_mesh_elem_table_ensure(em->bm, BM_EDGE);
  const int edges_num = em->bm->totedge;

  if (edges_mask) {
    BLI_assert(IN_RANGE_INCL(edges_num_active, 0, edges_num));
  }
  else {
    edges_num_active = edges_num;
  }

  BVHTree *tree = BLI_bvhtree_new(edges_num_active, epsilon, tree_type, axis);

  if (tree) {
    int i;
    BMIter iter;
    BMEdge *eed;
    BM_ITER_MESH_INDEX (eed, &iter, em->bm, BM_EDGES_OF_MESH, i) {
      if (edges_mask && !BLI_BITMAP_TEST_BOOL(edges_mask, i)) {
        continue;
      }
      float co[2][3];
      copy_v3_v3(co[0], eed->v1->co);
      copy_v3_v3(co[1], eed->v2->co);

      BLI_bvhtree_insert(tree, i, co[0], 2);
    }
    BLI_assert(BLI_bvhtree_get_len(tree) == edges_num_active);
  }

  return tree;
}

static BVHTree *bvhtree_from_mesh_edges_create_tree(const MVert *vert,
                                                    const MEdge *edge,
                                                    const int edge_num,
                                                    const BLI_bitmap *edges_mask,
                                                    int edges_num_active,
                                                    float epsilon,
                                                    int tree_type,
                                                    int axis)
{
  BVHTree *tree = nullptr;

  if (edges_mask) {
    BLI_assert(IN_RANGE_INCL(edges_num_active, 0, edge_num));
  }
  else {
    edges_num_active = edge_num;
  }

  if (edges_num_active) {
    /* Create a BVH-tree of the given target */
    tree = BLI_bvhtree_new(edges_num_active, epsilon, tree_type, axis);
    if (tree) {
      for (int i = 0; i < edge_num; i++) {
        if (edges_mask && !BLI_BITMAP_TEST_BOOL(edges_mask, i)) {
          continue;
        }
        float co[2][3];
        copy_v3_v3(co[0], vert[edge[i].v1].co);
        copy_v3_v3(co[1], vert[edge[i].v2].co);

        BLI_bvhtree_insert(tree, i, co[0], 2);
      }
    }
  }

  return tree;
}

static void bvhtree_from_mesh_edges_setup_data(BVHTreeFromMesh *data,
                                               BVHTree *tree,
                                               const bool is_cached,
                                               const MVert *vert,
                                               const bool vert_allocated,
                                               const MEdge *edge,
                                               const bool edge_allocated)
{
  memset(data, 0, sizeof(*data));

  data->tree = tree;

  data->cached = is_cached;

  data->nearest_callback = mesh_edges_nearest_point;
  data->raycast_callback = mesh_edges_spherecast;

  data->vert = vert;
  data->vert_allocated = vert_allocated;
  data->edge = edge;
  data->edge_allocated = edge_allocated;
}

BVHTree *bvhtree_from_editmesh_edges_ex(BVHTreeFromEditMesh *data,
                                        BMEditMesh *em,
                                        const BLI_bitmap *edges_mask,
                                        int edges_num_active,
                                        float epsilon,
                                        int tree_type,
                                        int axis,
                                        const BVHCacheType bvh_cache_type,
                                        BVHCache **bvh_cache_p,
                                        ThreadMutex *mesh_eval_mutex)
{
  BVHTree *tree = nullptr;

  if (bvh_cache_p) {
    bool lock_started = false;
    data->cached = bvhcache_find(
        bvh_cache_p, bvh_cache_type, &data->tree, &lock_started, mesh_eval_mutex);
    BVHCache *bvh_cache = *bvh_cache_p;
    if (data->cached == false) {
      tree = bvhtree_from_editmesh_edges_create_tree(
          epsilon, tree_type, axis, em, edges_mask, edges_num_active);
      bvhtree_balance(tree, true);
      /* Save on cache for later use */
      // printf("BVHTree built and saved on cache\n");
      bvhcache_insert(bvh_cache, tree, bvh_cache_type);
      data->cached = true;
    }
    bvhcache_unlock(bvh_cache, lock_started);
  }
  else {
    tree = bvhtree_from_editmesh_edges_create_tree(
        epsilon, tree_type, axis, em, edges_mask, edges_num_active);
    bvhtree_balance(tree, false);
  }

  if (tree) {
    memset(data, 0, sizeof(*data));
    data->tree = tree;
    data->em = em;
    data->nearest_callback = nullptr; /* TODO */
    data->raycast_callback = nullptr; /* TODO */
    data->cached = bvh_cache_p != nullptr;
  }

  return tree;
}

BVHTree *bvhtree_from_editmesh_edges(
    BVHTreeFromEditMesh *data, BMEditMesh *em, float epsilon, int tree_type, int axis)
{
  return bvhtree_from_editmesh_edges_ex(
      data, em, nullptr, -1, epsilon, tree_type, axis, BVHTREE_FROM_VERTS, nullptr, nullptr);
}

BVHTree *bvhtree_from_mesh_edges_ex(BVHTreeFromMesh *data,
                                    const MVert *vert,
                                    const bool vert_allocated,
                                    const MEdge *edge,
                                    const int edges_num,
                                    const bool edge_allocated,
                                    const BLI_bitmap *edges_mask,
                                    int edges_num_active,
                                    float epsilon,
                                    int tree_type,
                                    int axis,
                                    const BVHCacheType bvh_cache_type,
                                    BVHCache **bvh_cache_p,
                                    ThreadMutex *mesh_eval_mutex)
{
  bool in_cache = false;
  bool lock_started = false;
  BVHTree *tree = nullptr;
  if (bvh_cache_p) {
    in_cache = bvhcache_find(bvh_cache_p, bvh_cache_type, &tree, &lock_started, mesh_eval_mutex);
  }

  if (in_cache == false) {
    tree = bvhtree_from_mesh_edges_create_tree(
        vert, edge, edges_num, edges_mask, edges_num_active, epsilon, tree_type, axis);

    if (bvh_cache_p) {
      bvhtree_balance(tree, true);

      BVHCache *bvh_cache = *bvh_cache_p;
      /* Save on cache for later use */
      // printf("BVHTree built and saved on cache\n");
      bvhcache_insert(bvh_cache, tree, bvh_cache_type);
      in_cache = true;
    }
    else {
      bvhtree_balance(tree, false);
    }
  }

  if (bvh_cache_p) {
    bvhcache_unlock(*bvh_cache_p, lock_started);
  }

  /* Setup BVHTreeFromMesh */
  bvhtree_from_mesh_edges_setup_data(
      data, tree, in_cache, vert, vert_allocated, edge, edge_allocated);

  return tree;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Tessellated Face Builder
 * \{ */

static BVHTree *bvhtree_from_mesh_faces_create_tree(float epsilon,
                                                    int tree_type,
                                                    int axis,
                                                    const MVert *vert,
                                                    const MFace *face,
                                                    const int faces_num,
                                                    const BLI_bitmap *faces_mask,
                                                    int faces_num_active)
{
  BVHTree *tree = nullptr;

  if (faces_num) {
    if (faces_mask) {
      BLI_assert(IN_RANGE_INCL(faces_num_active, 0, faces_num));
    }
    else {
      faces_num_active = faces_num;
    }

    /* Create a BVH-tree of the given target. */
    // printf("%s: building BVH, total=%d\n", __func__, numFaces);
    tree = BLI_bvhtree_new(faces_num_active, epsilon, tree_type, axis);
    if (tree) {
      if (vert && face) {
        for (int i = 0; i < faces_num; i++) {
          float co[4][3];
          if (faces_mask && !BLI_BITMAP_TEST_BOOL(faces_mask, i)) {
            continue;
          }

          copy_v3_v3(co[0], vert[face[i].v1].co);
          copy_v3_v3(co[1], vert[face[i].v2].co);
          copy_v3_v3(co[2], vert[face[i].v3].co);
          if (face[i].v4) {
            copy_v3_v3(co[3], vert[face[i].v4].co);
          }

          BLI_bvhtree_insert(tree, i, co[0], face[i].v4 ? 4 : 3);
        }
      }
      BLI_assert(BLI_bvhtree_get_len(tree) == faces_num_active);
    }
  }

  return tree;
}

static void bvhtree_from_mesh_faces_setup_data(BVHTreeFromMesh *data,
                                               BVHTree *tree,
                                               const bool is_cached,
                                               const MVert *vert,
                                               const bool vert_allocated,
                                               const MFace *face,
                                               const bool face_allocated)
{
  memset(data, 0, sizeof(*data));

  data->tree = tree;
  data->cached = is_cached;

  data->nearest_callback = mesh_faces_nearest_point;
  data->raycast_callback = mesh_faces_spherecast;

  data->vert = vert;
  data->vert_allocated = vert_allocated;
  data->face = face;
  data->face_allocated = face_allocated;
}

BVHTree *bvhtree_from_mesh_faces_ex(BVHTreeFromMesh *data,
                                    const MVert *vert,
                                    const bool vert_allocated,
                                    const MFace *face,
                                    const int numFaces,
                                    const bool face_allocated,
                                    const BLI_bitmap *faces_mask,
                                    int faces_num_active,
                                    float epsilon,
                                    int tree_type,
                                    int axis,
                                    const BVHCacheType bvh_cache_type,
                                    BVHCache **bvh_cache_p,
                                    ThreadMutex *mesh_eval_mutex)
{
  bool in_cache = false;
  bool lock_started = false;
  BVHTree *tree = nullptr;
  if (bvh_cache_p) {
    in_cache = bvhcache_find(bvh_cache_p, bvh_cache_type, &tree, &lock_started, mesh_eval_mutex);
  }

  if (in_cache == false) {
    tree = bvhtree_from_mesh_faces_create_tree(
        epsilon, tree_type, axis, vert, face, numFaces, faces_mask, faces_num_active);
    bvhtree_balance(tree, bvh_cache_p != nullptr);

    if (bvh_cache_p) {
      /* Save on cache for later use */
      // printf("BVHTree built and saved on cache\n");
      BVHCache *bvh_cache = *bvh_cache_p;
      bvhcache_insert(bvh_cache, tree, bvh_cache_type);
      in_cache = true;
    }
  }

  if (bvh_cache_p) {
    bvhcache_unlock(*bvh_cache_p, lock_started);
  }

  /* Setup BVHTreeFromMesh */
  bvhtree_from_mesh_faces_setup_data(
      data, tree, in_cache, vert, vert_allocated, face, face_allocated);

  return tree;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name LoopTri Face Builder
 * \{ */

static BVHTree *bvhtree_from_editmesh_looptri_create_tree(float epsilon,
                                                          int tree_type,
                                                          int axis,
                                                          BMEditMesh *em,
                                                          const BLI_bitmap *looptri_mask,
                                                          int looptri_num_active)
{
  BVHTree *tree = nullptr;
  const int looptri_num = em->tottri;

  if (looptri_num) {
    if (looptri_mask) {
      BLI_assert(IN_RANGE_INCL(looptri_num_active, 0, looptri_num));
    }
    else {
      looptri_num_active = looptri_num;
    }

    /* Create a BVH-tree of the given target */
    // printf("%s: building BVH, total=%d\n", __func__, numFaces);
    tree = BLI_bvhtree_new(looptri_num_active, epsilon, tree_type, axis);
    if (tree) {
      const BMLoop *(*looptris)[3] = (const BMLoop *(*)[3])em->looptris;

      /* Insert BMesh-tessellation triangles into the BVH-tree, unless they are hidden
       * and/or selected. Even if the faces themselves are not selected for the snapped
       * transform, having a vertex selected means the face (and thus it's tessellated
       * triangles) will be moving and will not be a good snap targets. */
      for (int i = 0; i < looptri_num; i++) {
        const BMLoop **ltri = looptris[i];
        bool insert = looptri_mask ? BLI_BITMAP_TEST_BOOL(looptri_mask, i) : true;

        if (insert) {
          /* No reason found to block hit-testing the triangle for snap, so insert it now. */
          float co[3][3];
          copy_v3_v3(co[0], ltri[0]->v->co);
          copy_v3_v3(co[1], ltri[1]->v->co);
          copy_v3_v3(co[2], ltri[2]->v->co);

          BLI_bvhtree_insert(tree, i, co[0], 3);
        }
      }
      BLI_assert(BLI_bvhtree_get_len(tree) == looptri_num_active);
    }
  }

  return tree;
}

static BVHTree *bvhtree_from_mesh_looptri_create_tree(float epsilon,
                                                      int tree_type,
                                                      int axis,
                                                      const MVert *vert,
                                                      const MLoop *mloop,
                                                      const MLoopTri *looptri,
                                                      const int looptri_num,
                                                      const BLI_bitmap *looptri_mask,
                                                      int looptri_num_active)
{
  BVHTree *tree = nullptr;

  if (looptri_mask) {
    BLI_assert(IN_RANGE_INCL(looptri_num_active, 0, looptri_num));
  }
  else {
    looptri_num_active = looptri_num;
  }

  if (looptri_num_active) {
    /* Create a BVH-tree of the given target */
    // printf("%s: building BVH, total=%d\n", __func__, numFaces);
    tree = BLI_bvhtree_new(looptri_num_active, epsilon, tree_type, axis);
    if (tree) {
      if (vert && looptri) {
        for (int i = 0; i < looptri_num; i++) {
          float co[3][3];
          if (looptri_mask && !BLI_BITMAP_TEST_BOOL(looptri_mask, i)) {
            continue;
          }

          copy_v3_v3(co[0], vert[mloop[looptri[i].tri[0]].v].co);
          copy_v3_v3(co[1], vert[mloop[looptri[i].tri[1]].v].co);
          copy_v3_v3(co[2], vert[mloop[looptri[i].tri[2]].v].co);

          BLI_bvhtree_insert(tree, i, co[0], 3);
        }
      }
      BLI_assert(BLI_bvhtree_get_len(tree) == looptri_num_active);
    }
  }

  return tree;
}

static void bvhtree_from_mesh_looptri_setup_data(BVHTreeFromMesh *data,
                                                 BVHTree *tree,
                                                 const bool is_cached,
                                                 const MVert *vert,
                                                 const bool vert_allocated,
                                                 const MLoop *mloop,
                                                 const bool loop_allocated,
                                                 const MLoopTri *looptri,
                                                 const bool looptri_allocated)
{
  memset(data, 0, sizeof(*data));

  data->tree = tree;
  data->cached = is_cached;

  data->nearest_callback = mesh_looptri_nearest_point;
  data->raycast_callback = mesh_looptri_spherecast;

  data->vert = vert;
  data->vert_allocated = vert_allocated;
  data->loop = mloop;
  data->loop_allocated = loop_allocated;
  data->looptri = looptri;
  data->looptri_allocated = looptri_allocated;
}

