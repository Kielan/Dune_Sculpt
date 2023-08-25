#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_list.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "types_armature.h"
#include "types_brush.h"
#include "types_collection.h"
#include "types_linestyle.h"
#include "types_material.h"
#include "types_node.h"
#include "types_scene.h"
#include "types_window.h"
#include "types_world.h"

#include "dune_action.h"
#include "dune_armature.h"
#include "dune_context.h"
#include "dune_layer.h"
#include "dune_linestyle.h"
#include "dune_material.h"
#include "dune_mod.h"
#include "dune_object.h"
#include "dune_paint.h"
#include "dune_particle.h"
#include "dune_screen.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "ed_btns.h"
#include "ed_phys.h"
#include "ed_screen.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "wm_api.h"

#include "btns_intern.h" /* own include */

static int set_ptr_type(BtnsCxtPath *path, CxtDataResult *result, ApiStruct *type)
{
  for (int i = 0; i < path->len; i++) {
    ApiPtr *ptr = &path->ptr[i];

    if (api_struct_is_a(ptr->type, type)) {
      cxt_data_ptr_set_ptr(result, ptr);
      return CXT_RESULT_OK;
    }
  }

  return CXT_RESULT_MEMBER_NOT_FOUND;
}

static ApiPtr *get_ptr_type(BtnsCxtPath *path, ApiStruct *type)
{
  for (int i = 0; i < path->len; i++) {
    ApiPtr *ptr = &path->ptr[i];

    if (api_struct_is_a(ptr->type, type)) {
      return ptr;
    }
  }

  return NULL;
}

/************************* Creating the Path ************************/

static bool btns_cxt_path_scene(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* this one just verifies */
  return api_struct_is_a(ptr->type, &ApiScene);
}

static bool btns_cxt_path_view_layer(BtnsCxtPath *path, wmWindow *win)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* View Layer may have already been resolved in a previous call
   * (e.g. in btns_cxt_path_linestyle). */
  if (api_struct_is_a(ptr->type, &ApiViewLayer)) {
    return true;
  }

  if (btns_cxt_path_scene(path)) {
    Scene *scene = path->ptr[path->len - 1].data;
    ViewLayer *view_layer = (win->scene == scene) ? wm_window_get_active_view_layer(win) :
                                                    dune_view_layer_default_view(scene);

    api_ptr_create(&scene->id, &ApiViewLayer, view_layer, &path->ptr[path->len]);
    path->len++;
    return true;
  }

  return false;
}

/* NOTE: this function can return true without adding a world to the path
 * so the btns stay visible, but be sure to check the ID type if a ID_WO */
static bool btns_cxt_path_world(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) world, we're done */
  if (api_struct_is_a(ptr->type, &ApiWorld)) {
    return true;
  }
  /* if we have a scene, use the scene's world */
  if (btns_cxt_path_scene(path)) {
    Scene *scene = path->ptr[path->len - 1].data;
    World *world = scene->world;

    if (world) {
      api_id_ptr_create(&scene->world->id, &path->ptr[path->len]);
      path->len++;
      return true;
    }

    return true;
  }

  /* no path to a world possible */
  return false;
}

static bool btns_cxt_path_collection(const Cxt *C,
                                     BtnsCxtPath *path,
                                     wmWindow *window)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) collection, we're done */
  if (api_struct_is_a(ptr->type, &ApiCollection)) {
    return true;
  }

  Scene *scene = cxt_data_scene(C);

  /* if we have a view layer, use the view layer's active collection */
  if (btns_cxt_path_view_layer(path, window)) {
    ViewLayer *view_layer = path->ptr[path->len - 1].data;
    Collection *c = view_layer->active_collection->collection;

    /* Do not show collection tab for master collection. */
    if (c == scene->master_collection) {
      return false;
    }

    if (c) {
      api_id_ptr_create(&c->id, &path->ptr[path->len]);
      path->len++;
      return true;
    }
  }

  /* no path to a collection possible */
  return false;
}

static bool btns_cxt_path_linestyle(BtnsCxtPath *path, wmWindow *window)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) linestyle, we're done */
  if (api_struct_is_a(ptr->type, &ApiFreestyleLineStyle)) {
    return true;
  }
  /* if we have a view layer, use the lineset's linestyle */
  if (btns_cxt_path_view_layer(path, window)) {
    ViewLayer *view_layer = path->ptr[path->len - 1].data;
    FreestyleLineStyle *linestyle = dune_linestyle_active_from_view_layer(view_layer);
    if (linestyle) {
      api_id_ptr_create(&linestyle->id, &path->ptr[path->len]);
      path->len++;
      return true;
    }
  }

  /* no path to a linestyle possible */
  return false;
}

static bool btns_cxt_path_object(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) object, we're done */
  if (api_struct_is_a(ptr->type, &ApiObject)) {
    return true;
  }
  if (!api_struct_is_a(ptr->type, &ApiViewLayer)) {
    return false;
  }

  ViewLayer *view_layer = ptr->data;
  Object *ob = (view_layer->basact) ? view_layer->basact->object : NULL;

  if (ob) {
    api_id_ptr_create(&ob->id, &path->ptr[path->len]);
    path->len++;

    return true;
  }

  /* no path to a object possible */
  return false;
}

static bool btns_cxt_path_data(BtnsCxtPath *path, int type)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a data, we're done */
  if (api_struct_is_a(ptr->type, &ApiMesh) && (ELEM(type, -1, OB_MESH))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiCurve) &&
      (type == -1 || ELEM(type, OB_CURVES_LEGACY, OB_SURF, OB_FONT))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiArmature) && (ELEM(type, -1, OB_ARMATURE))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiMetaBall) && (ELEM(type, -1, OB_MBALL))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiLattice) && (ELEM(type, -1, OB_LATTICE))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiCamera) && (ELEM(type, -1, OB_CAMERA))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiLight) && (ELEM(type, -1, OB_LAMP))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiSpeaker) && (ELEM(type, -1, OB_SPEAKER))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiLightProbe) && (ELEM(type, -1, OB_LIGHTPROBE))) {
    return true;
  }
  if (api_struct_is_a(ptr->type, &ApiPen) && (ELEM(type, -1, OB_GPENCIL))) {
    return true;
  }
#ifdef WITH_NEW_CURVES_TYPE
  if (api_struct_is_a(ptr->type, &ApiCurves) && (ELEM(type, -1, OB_CURVES))) {
    return true;
  }
#endif
#ifdef WITH_POINT_CLOUD
  if (api_struct_is_a(ptr->type, &ApiPointCloud) && (ELEM(type, -1, OB_POINTCLOUD))) {
    return true;
  }
#endif
  if (api_struct_is_a(ptr->type, &ApiVolume) && (ELEM(type, -1, OB_VOLUME))) {
    return true;
  }
  /* try to get an object in the path, no pinning supported here */
  if (btns_cxt_path_object(path)) {
    Object *ob = path->ptr[path->len - 1].data;

    if (ob && (ELEM(type, -1, ob->type))) {
      api_id_ptr_create(ob->data, &path->ptr[path->len]);
      path->len++;

      return true;
    }
  }

  /* no path to data possible */
  return false;
}
/* path modifier */
static bool btns_cxt_path_mod(BtnsCxtPath *path)
{
  if (btns_cxt_path_object(path)) {
    Object *ob = path->ptr[path->len - 1].data;

    if (ELEM(ob->type,
             OB_MESH,
             OB_CURVES_LEGACY,
             OB_FONT,
             OB_SURF,
             OB_LATTICE,
             OB_PEN,
             OB_CURVES,
             OB_POINTCLOUD,
             OB_VOLUME)) {
      ModData *md = dune_object_active_mod(ob);
      if (md != NULL) {
        api_ptr_create(&ob->id, &ApiMod, md, &path->ptr[path->len]);
        path->len++;
      }

      return true;
    }
  }

  return false;
}

static bool btns_cxt_path_shaderfx(BtnsCxtPath *path)
{
  if (btns_cxt_path_object(path)) {
    Object *ob = path->ptr[path->len - 1].data;

    if (ob && ELEM(ob->type, OB_PEN)) {
      return true;
    }
  }

  return false;
}

static bool btns_cxt_path_material(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) material, we're done */
  if (api_struct_is_a(ptr->type, &ApiMaterial)) {
    return true;
  }
  /* if we have an object, use the object material slot */
  if (btns_cxt_path_object(path)) {
    Object *ob = path->ptr[path->len - 1].data;

    if (ob && OB_TYPE_SUPPORT_MATERIAL(ob->type)) {
      Material *ma = dune_object_material_get(ob, ob->actcol);
      if (ma != NULL) {
        api_id_ptr_create(&ma->id, &path->ptr[path->len]);
        path->len++;
      }
      return true;
    }
  }

  /* no path to a material possible */
  return false;
}

static bool btns_cxt_path_bone(BtnsCxtPath *path)
{
  /* if we have an armature, get the active bone */
  if (btns_cxt_path_data(path, OB_ARMATURE)) {
    Armature *arm = path->ptr[path->len - 1].data;

    if (arm->edbo) {
      if (arm->act_edbone) {
        EditBone *edbo = arm->act_edbone;
        api_ptr_create(&arm->id, &ApiEditBone, edbo, &path->ptr[path->len]);
        path->len++;
        return true;
      }
    }
    else {
      if (arm->act_bone) {
        api_ptr_create(&arm->id, &ApiBone, arm->act_bone, &path->ptr[path->len]);
        path->len++;
        return true;
      }
    }
  }

  /* no path to a bone possible */
  return false;
}

static bool btns_cxt_path_pose_bone(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) PoseBone, we're done */
  if (api_struct_is_a(ptr->type, &ApiPoseBone)) {
    return true;
  }

  /* if we have an armature, get the active bone */
  if (btns_cxt_path_object(path)) {
    Object *ob = path->ptr[path->len - 1].data;
    Armature *arm = ob->data; /* path->ptr[path->len-1].data - works too */

    if (ob->type != OB_ARMATURE || arm->edbo) {
      return false;
    }

    if (arm->act_bone) {
      PoseChannel *pchan = dune_pose_channel_find_name(ob->pose, arm->act_bone->name);
      if (pchan) {
        api_ptr_create(&ob->id, &ApiPoseBone, pchan, &path->ptr[path->len]);
        path->len++;
        return true;
      }
    }
  }

  /* no path to a bone possible */
  return false;
}

static bool btns_cxt_path_particle(BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have (pinned) particle settings, we're done */
  if (api_struct_is_a(ptr->type, &ApiParticleSettings)) {
    return true;
  }
  /* if we have an object, get the active particle system */
  if (btns_cxt_path_object(path)) {
    Object *ob = path->ptr[path->len - 1].data;

    if (ob && ob->type == OB_MESH) {
      ParticleSystem *psys = psys_get_current(ob);

      api_ptr_create(&ob->id, &ApiParticleSystem, psys, &path->ptr[path->len]);
      path->len++;
      return true;
    }
  }

  /* no path to a particle system possible */
  return false;
}

static bool btns_cxt_path_brush(const Cxt *C, BtnsCxtPath *path)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  /* if we already have a (pinned) brush, we're done */
  if (api_struct_is_a(ptr->type, &ApiBrush)) {
    return true;
  }
  /* if we have a scene, use the toolsettings brushes */
  if (btns_cxt_path_scene(path)) {
    Scene *scene = path->ptr[path->len - 1].data;

    Brush *br = NULL;
    if (scene) {
      wmWindow *window = cxt_wm_window(C);
      ViewLayer *view_layer = wm_window_get_active_view_layer(window);
      br = dune_paint_brush(dune_paint_get_active(scene, view_layer));
    }

    if (br) {
      api_id_ptr_create((Id *)br, &path->ptr[path->len]);
      path->len++;

      return true;
    }
  }

  /* no path to a brush possible */
  return false;
}

static bool btns_cxt_path_texture(const Cxt *C,
                                  BtnsCxtPath *path,
                                  BtnsCxtTexture *ct)
{
  ApiPtr *ptr = &path->ptr[path->len - 1];

  if (!ct) {
    return false;
  }

  /* if we already have a (pinned) texture, we're done */
  if (api_struct_is_a(ptr->type, &ApiTexture)) {
    return true;
  }

  if (!ct->user) {
    return false;
  }

  Id *id = ct->user->id;

  if (id) {
    if (GS(id->name) == ID_BR) {
      btns_cxt_path_brush(C, path);
    }
    else if (GS(id->name) == ID_PA) {
      btns_cxt_path_particle(path);
    }
    else if (GS(id->name) == ID_OB) {
      btns_cxt_path_object(path);
    }
    else if (GS(id->name) == ID_LS) {
      btns_cxt_path_linestyle(path, cxt_wm_window(C));
    }
  }

  if (ct->texture) {
    api_id_ptr_create(&ct->texture->id, &path->ptr[path->len]);
    path->len++;
  }

  return true;
}

#ifdef WITH_FREESTYLE
static bool btns_cxt_linestyle_pinnable(const Cxt *C, ViewLayer *view_layer)
{
  wmWindow *window = cxt_wm_window(C);
  Scene *scene = wm_window_get_active_scene(window);

  /* if Freestyle is disabled in the scene */
  if ((scene->r.mode & R_EDGE_FRS) == 0) {
    return false;
  }
  /* if Freestyle is not in the Parameter Editor mode */
  FreestyleConfig *config = &view_layer->freestyle_config;
  if (config->mode != FREESTYLE_CONTROL_EDITOR_MODE) {
    return false;
  }
  /* if the scene has already been pinned */
  SpaceProps *sbtns = cxt_wm_space_props(C);
  if (sbtns->pinid && sbtns->pinid == &scene->id) {
    return false;
  }
  return true;
}
#endif

static bool btns_cxt_path(
    const Cxt *C, SpaceProps *sbtns, BtnsCxtPath *path, int mainb, int flag)
{
  /* Note we don't use ctx_data here, instead we get it from the window.
   * Otherwise there is a loop reading the context that we are setting. */
  wmWindow *window = cxt_wm_window(C);
  Scene *scene = wm_window_get_active_scene(window);
  ViewLayer *view_layer = wm_window_get_active_view_layer(window);

  memset(path, 0, sizeof(*path));
  path->flag = flag;

  /* If some ID datablock is pinned, set the root pointer. */
  if (sbuts->pinid) {
    Id *id = sbtns->pinid;

    api_id_ptr_create(id, &path->ptr[0]);
    path->len++;
  }
  /* No pinned root, use scene as initial root. */
  else if (mainb != CXT_TOOL) {
    api_id_ptr_create(&scene->id, &path->ptr[0]);
    path->len++;

    if (!ELEM(mainb,
              CXT_SCENE,
              CXT_RENDER,
              CXT_OUTPUT,
              CXT_VIEW_LAYER,
              CXT_WORLD)) {
      api_ptr_create(NULL, &ApiViewLayer, view_layer, &path->ptr[path->len]);
      path->len++;
    }
  }

  /* now for each buttons context type, we try to construct a path,
   * tracing back recursively */
  bool found;
  switch (mainb) {
    case CXT_SCENE:
    case CXT_RENDER:
    case CXT_OUTPUT:
      found = btns_cxt_path_scene(path);
      break;
    case CXT_VIEW_LAYER:
#ifdef WITH_FREESTYLE
      if (btns_cxt_linestyle_pinnable(C, view_layer)) {
        found = btns_cxt_path_linestyle(path, window);
        if (found) {
          break;
        }
      }
#endif
      found = btns_cxt_path_view_layer(path, window);
      break;
    case CXT_WORLD:
      found = btns_cxt_path_world(path);
      break;
    case CXT_COLLECTION: /* This is for Line Art collection flags */
      found = btns_cxt_path_collection(C, path, window);
      break;
    case CXT_TOOL:
      found = true;
      break;
    case CXT_OBJECT:
    case CXT_PHYS:
    case CXT_CONSTRAINT:
      found = btns_cxt_path_object(path);
      break;
    case CXT_MOD:
      found = btns_cxt_path_mod(path);
      break;
    case CXT_SHADERFX:
      found = btns_cxt_path_shaderfx(path);
      break;
    case CXT_DATA:
      found = btns_cxt_path_data(path, -1);
      break;
    case CXT_PARTICLE:
      found = buttons_context_path_particle(path);
      break;
    case BCONTEXT_MATERIAL:
      found = buttons_context_path_material(path);
      break;
    case BCONTEXT_TEXTURE:
      found = buttons_context_path_texture(C, path, sbuts->texuser);
      break;
    case BCONTEXT_BONE:
      found = buttons_context_path_bone(path);
      if (!found) {
        found = buttons_context_path_data(path, OB_ARMATURE);
      }
      break;
    case BCONTEXT_BONE_CONSTRAINT:
      found = buttons_context_path_pose_bone(path);
      break;
    default:
      found = false;
      break;
  }

  return found;
}

static bool buttons_shading_context(const bContext *C, int mainb)
{
  wmWindow *window = CTX_wm_window(C);
  ViewLayer *view_layer = WM_window_get_active_view_layer(window);
  Object *ob = OBACT(view_layer);

  if (ELEM(mainb, BCONTEXT_MATERIAL, BCONTEXT_WORLD, BCONTEXT_TEXTURE)) {
    return true;
  }
  if (mainb == BCONTEXT_DATA && ob && ELEM(ob->type, OB_LAMP, OB_CAMERA)) {
    return true;
  }

  return false;
}

static int buttons_shading_new_context(const bContext *C, int flag)
{
  wmWindow *window = CTX_wm_window(C);
  ViewLayer *view_layer = WM_window_get_active_view_layer(window);
  Object *ob = OBACT(view_layer);

  if (flag & (1 << BCONTEXT_MATERIAL)) {
    return BCONTEXT_MATERIAL;
  }
  if (ob && ELEM(ob->type, OB_LAMP, OB_CAMERA) && (flag & (1 << BCONTEXT_DATA))) {
    return BCONTEXT_DATA;
  }
  if (flag & (1 << BCONTEXT_WORLD)) {
    return BCONTEXT_WORLD;
  }

  return BCONTEXT_RENDER;
}

void buttons_context_compute(const bContext *C, SpaceProperties *sbuts)
{
  if (!sbuts->path) {
    sbuts->path = MEM_callocN(sizeof(ButsContextPath), "ButsContextPath");
  }

  ButsContextPath *path = sbuts->path;

  int pflag = 0;
  int flag = 0;

  /* Set scene path. */
  buttons_context_path(C, sbuts, path, BCONTEXT_SCENE, pflag);

  buttons_texture_context_compute(C, sbuts);

  /* for each context, see if we can compute a valid path to it, if
   * this is the case, we know we have to display the button */
  for (int i = 0; i < BCONTEXT_TOT; i++) {
    if (buttons_context_path(C, sbuts, path, i, pflag)) {
      flag |= (1 << i);

      /* setting icon for data context */
      if (i == BCONTEXT_DATA) {
        PointerRNA *ptr = &path->ptr[path->len - 1];

        if (ptr->type) {
          if (RNA_struct_is_a(ptr->type, &RNA_Light)) {
            sbuts->dataicon = ICON_OUTLINER_DATA_LIGHT;
          }
          else {
            sbuts->dataicon = RNA_struct_ui_icon(ptr->type);
          }
        }
        else {
          sbuts->dataicon = ICON_EMPTY_DATA;
        }
      }
    }
  }

  /* always try to use the tab that was explicitly
   * set to the user, so that once that context comes
   * back, the tab is activated again */
  sbuts->mainb = sbuts->mainbuser;

  /* in case something becomes invalid, change */
  if ((flag & (1 << sbuts->mainb)) == 0) {
    if (sbuts->flag & SB_SHADING_CONTEXT) {
      /* try to keep showing shading related buttons */
      sbuts->mainb = buttons_shading_new_context(C, flag);
    }
    else if (flag & BCONTEXT_OBJECT) {
      sbuts->mainb = BCONTEXT_OBJECT;
    }
    else {
      for (int i = 0; i < BCONTEXT_TOT; i++) {
        if (flag & (1 << i)) {
          sbuts->mainb = i;
          break;
        }
      }
    }
  }

  buttons_context_path(C, sbuts, path, sbuts->mainb, pflag);

  if (!(flag & (1 << sbuts->mainb))) {
    if (flag & (1 << BCONTEXT_OBJECT)) {
      sbuts->mainb = BCONTEXT_OBJECT;
    }
    else {
      sbuts->mainb = BCONTEXT_SCENE;
    }
  }

  if (buttons_shading_context(C, sbuts->mainb)) {
    sbuts->flag |= SB_SHADING_CONTEXT;
  }
  else {
    sbuts->flag &= ~SB_SHADING_CONTEXT;
  }

  sbuts->pathflag = flag;
}

static bool is_pointer_in_path(ButsContextPath *path, PointerRNA *ptr)
{
  for (int i = 0; i < path->len; ++i) {
    if (ptr->owner_id == path->ptr[i].owner_id) {
      return true;
    }
  }
  return false;
}

bool ED_buttons_should_sync_with_outliner(const bContext *C,
                                          const SpaceProperties *sbuts,
                                          ScrArea *area)
{
  ScrArea *active_area = CTX_wm_area(C);
  const bool auto_sync = ED_area_has_shared_border(active_area, area) &&
                         sbuts->outliner_sync == PROPERTIES_SYNC_AUTO;
  return auto_sync || sbuts->outliner_sync == PROPERTIES_SYNC_ALWAYS;
}

void ED_buttons_set_context(const bContext *C,
                            SpaceProperties *sbuts,
                            PointerRNA *ptr,
                            const int context)
{
  ButsContextPath path;
  if (buttons_context_path(C, sbuts, &path, context, 0) && is_pointer_in_path(&path, ptr)) {
    sbuts->mainbuser = context;
    sbuts->mainb = sbuts->mainbuser;
  }
}

/************************* Context Callback ************************/

const char *buttons_context_dir[] = {
    "texture_slot",
    "scene",
    "world",
    "object",
    "mesh",
    "armature",
    "lattice",
    "curve",
    "meta_ball",
    "light",
    "speaker",
    "lightprobe",
    "camera",
    "material",
    "material_slot",
    "texture",
    "texture_user",
    "texture_user_property",
    "bone",
    "edit_bone",
    "pose_bone",
    "particle_system",
    "particle_system_editable",
    "particle_settings",
    "cloth",
    "soft_body",
    "fluid",
    "collision",
    "brush",
    "dynamic_paint",
    "line_style",
    "collection",
    "gpencil",
#ifdef WITH_NEW_CURVES_TYPE
    "curves",
#endif
#ifdef WITH_POINT_CLOUD
    "pointcloud",
#endif
    "volume",
    NULL,
};

int /*eContextResult*/ buttons_context(const bContext *C,
                                       const char *member,
                                       bContextDataResult *result)
{
  SpaceProperties *sbuts = CTX_wm_space_properties(C);
  if (sbuts && sbuts->path == NULL) {
    /* path is cleared for SCREEN_OT_redo_last, when global undo does a file-read which clears the
     * path (see lib_link_workspace_layout_restore). */
    buttons_context_compute(C, sbuts);
  }
  ButsContextPath *path = sbuts ? sbuts->path : NULL;

  if (!path) {
    return CTX_RESULT_MEMBER_NOT_FOUND;
  }

  if (sbuts->mainb == BCONTEXT_TOOL) {
    return CTX_RESULT_MEMBER_NOT_FOUND;
  }

  /* here we handle context, getting data from precomputed path */
  if (CTX_data_dir(member)) {
    /* in case of new shading system we skip texture_slot, complex python
     * UI script logic depends on checking if this is available */
    if (sbuts->texuser) {
      CTX_data_dir_set(result, buttons_context_dir + 1);
    }
    else {
      CTX_data_dir_set(result, buttons_context_dir);
    }
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "scene")) {
    /* Do not return one here if scene is not found in path,
     * in this case we want to get default context scene! */
    return set_pointer_type(path, result, &RNA_Scene);
  }
  if (CTX_data_equals(member, "world")) {
    set_pointer_type(path, result, &RNA_World);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "collection")) {
    /* Do not return one here if collection is not found in path,
     * in this case we want to get default context collection! */
    return set_pointer_type(path, result, &RNA_Collection);
  }
  if (CTX_data_equals(member, "object")) {
    set_pointer_type(path, result, &RNA_Object);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "mesh")) {
    set_pointer_type(path, result, &RNA_Mesh);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "armature")) {
    set_pointer_type(path, result, &RNA_Armature);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "lattice")) {
    set_pointer_type(path, result, &RNA_Lattice);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "curve")) {
    set_pointer_type(path, result, &RNA_Curve);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "meta_ball")) {
    set_pointer_type(path, result, &RNA_MetaBall);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "light")) {
    set_pointer_type(path, result, &RNA_Light);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "camera")) {
    set_pointer_type(path, result, &RNA_Camera);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "speaker")) {
    set_pointer_type(path, result, &RNA_Speaker);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "lightprobe")) {
    set_pointer_type(path, result, &RNA_LightProbe);
    return CTX_RESULT_OK;
  }
#ifdef WITH_NEW_CURVES_TYPE
  if (CTX_data_equals(member, "curves")) {
    set_pointer_type(path, result, &RNA_Curves);
    return CTX_RESULT_OK;
  }
#endif
#ifdef WITH_POINT_CLOUD
  if (CTX_data_equals(member, "pointcloud")) {
    set_pointer_type(path, result, &RNA_PointCloud);
    return CTX_RESULT_OK;
  }
#endif
  if (CTX_data_equals(member, "volume")) {
    set_pointer_type(path, result, &RNA_Volume);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "material")) {
    set_pointer_type(path, result, &RNA_Material);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "texture")) {
    ButsContextTexture *ct = sbuts->texuser;

    if (ct) {
      if (ct->texture == NULL) {
        return CTX_RESULT_NO_DATA;
      }

      CTX_data_pointer_set(result, &ct->texture->id, &RNA_Texture, ct->texture);
    }

    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "material_slot")) {
    PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

    if (ptr) {
      Object *ob = ptr->data;

      if (ob && OB_TYPE_SUPPORT_MATERIAL(ob->type) && ob->totcol) {
        /* a valid actcol isn't ensured T27526. */
        int matnr = ob->actcol - 1;
        if (matnr < 0) {
          matnr = 0;
        }
        /* Keep aligned with rna_Object_material_slots_get. */
        CTX_data_pointer_set(
            result, &ob->id, &RNA_MaterialSlot, (void *)(matnr + (uintptr_t)&ob->id));
      }
    }

    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "texture_user")) {
    ButsContextTexture *ct = sbuts->texuser;

    if (!ct) {
      return CTX_RESULT_NO_DATA;
    }

    if (ct->user && ct->user->ptr.data) {
      ButsTextureUser *user = ct->user;
      CTX_data_pointer_set_ptr(result, &user->ptr);
    }

    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "texture_user_property")) {
    ButsContextTexture *ct = sbuts->texuser;

    if (!ct) {
      return CTX_RESULT_NO_DATA;
    }

    if (ct->user && ct->user->ptr.data) {
      ButsTextureUser *user = ct->user;
      CTX_data_pointer_set(result, NULL, &RNA_Property, user->prop);
    }

    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "texture_node")) {
    ButsContextTexture *ct = sbuts->texuser;

    if (ct) {
      /* new shading system */
      if (ct->user && ct->user->node) {
        CTX_data_pointer_set(result, &ct->user->ntree->id, &RNA_Node, ct->user->node);
      }

      return CTX_RESULT_OK;
    }
    return CTX_RESULT_NO_DATA;
  }
  if (CTX_data_equals(member, "texture_slot")) {
    ButsContextTexture *ct = sbuts->texuser;
    PointerRNA *ptr;

    /* Particles slots are used in both old and new textures handling. */
    if ((ptr = get_pointer_type(path, &RNA_ParticleSystem))) {
      ParticleSettings *part = ((ParticleSystem *)ptr->data)->part;

      if (part) {
        CTX_data_pointer_set(
            result, &part->id, &RNA_ParticleSettingsTextureSlot, part->mtex[(int)part->texact]);
      }
    }
    else if (ct) {
      return CTX_RESULT_MEMBER_NOT_FOUND; /* new shading system */
    }
    else if ((ptr = get_pointer_type(path, &RNA_FreestyleLineStyle))) {
      FreestyleLineStyle *ls = ptr->data;

      if (ls) {
        CTX_data_pointer_set(
            result, &ls->id, &RNA_LineStyleTextureSlot, ls->mtex[(int)ls->texact]);
      }
    }

    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "bone")) {
    set_pointer_type(path, result, &RNA_Bone);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "edit_bone")) {
    set_pointer_type(path, result, &RNA_EditBone);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "pose_bone")) {
    set_pointer_type(path, result, &RNA_PoseBone);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "particle_system")) {
    set_pointer_type(path, result, &RNA_ParticleSystem);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "particle_system_editable")) {
    if (PE_poll((bContext *)C)) {
      set_pointer_type(path, result, &RNA_ParticleSystem);
    }
    else {
      CTX_data_pointer_set(result, NULL, &RNA_ParticleSystem, NULL);
    }
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "particle_settings")) {
    /* only available when pinned */
    PointerRNA *ptr = get_pointer_type(path, &RNA_ParticleSettings);

    if (ptr && ptr->data) {
      CTX_data_pointer_set_ptr(result, ptr);
      return CTX_RESULT_OK;
    }

    /* get settings from active particle system instead */
    ptr = get_pointer_type(path, &RNA_ParticleSystem);

    if (ptr && ptr->data) {
      ParticleSettings *part = ((ParticleSystem *)ptr->data)->part;
      CTX_data_pointer_set(result, ptr->owner_id, &RNA_ParticleSettings, part);
      return CTX_RESULT_OK;
    }

    set_pointer_type(path, result, &RNA_ParticleSettings);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "cloth")) {
    PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

    if (ptr && ptr->data) {
      Object *ob = ptr->data;
      ModifierData *md = BKE_modifiers_findby_type(ob, eModifierType_Cloth);
      CTX_data_pointer_set(result, &ob->id, &RNA_ClothModifier, md);
      return CTX_RESULT_OK;
    }
    return CTX_RESULT_NO_DATA;
  }
  if (CTX_data_equals(member, "soft_body")) {
    PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

    if (ptr && ptr->data) {
      Object *ob = ptr->data;
      ModifierData *md = BKE_modifiers_findby_type(ob, eModifierType_Softbody);
      CTX_data_pointer_set(result, &ob->id, &RNA_SoftBodyModifier, md);
      return CTX_RESULT_OK;
    }
    return CTX_RESULT_NO_DATA;
  }

  if (CTX_data_equals(member, "fluid")) {
    PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

    if (ptr && ptr->data) {
      Object *ob = ptr->data;
      ModifierData *md = BKE_modifiers_findby_type(ob, eModifierType_Fluid);
      CTX_data_pointer_set(result, &ob->id, &RNA_FluidModifier, md);
      return CTX_RESULT_OK;
    }
    return CTX_RESULT_NO_DATA;
  }
  if (CTX_data_equals(member, "collision")) {
    PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

    if (ptr && ptr->data) {
      Object *ob = ptr->data;
      ModifierData *md = BKE_modifiers_findby_type(ob, eModifierType_Collision);
      CTX_data_pointer_set(result, &ob->id, &RNA_CollisionModifier, md);
      return CTX_RESULT_OK;
    }
    return CTX_RESULT_NO_DATA;
  }
  if (CTX_data_equals(member, "brush")) {
    set_pointer_type(path, result, &RNA_Brush);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "dynamic_paint")) {
    PointerRNA *ptr = get_pointer_type(path, &RNA_Object);

    if (ptr && ptr->data) {
      Object *ob = ptr->data;
      ModifierData *md = BKE_modifiers_findby_type(ob, eModifierType_DynamicPaint);
      CTX_data_pointer_set(result, &ob->id, &RNA_DynamicPaintModifier, md);
      return CTX_RESULT_OK;
    }
    return CTX_RESULT_NO_DATA;
  }
  if (CTX_data_equals(member, "line_style")) {
    set_pointer_type(path, result, &RNA_FreestyleLineStyle);
    return CTX_RESULT_OK;
  }
  if (CTX_data_equals(member, "gpencil")) {
    set_pointer_type(path, result, &RNA_GreasePencil);
    return CTX_RESULT_OK;
  }
  return CTX_RESULT_MEMBER_NOT_FOUND;
}

/************************* Drawing the Path ************************/

static bool buttons_panel_context_poll(const bContext *C, PanelType *UNUSED(pt))
{
  SpaceProperties *sbuts = CTX_wm_space_properties(C);
  return sbuts->mainb != BCONTEXT_TOOL;
}

static void buttons_panel_context_draw(const bContext *C, Panel *panel)
{
  SpaceProperties *sbuts = CTX_wm_space_properties(C);
  ButsContextPath *path = sbuts->path;

  if (!path) {
    return;
  }

  uiLayout *row = uiLayoutRow(panel->layout, true);
  uiLayoutSetAlignment(row, UI_LAYOUT_ALIGN_LEFT);

  bool first = true;
  for (int i = 0; i < path->len; i++) {
    PointerRNA *ptr = &path->ptr[i];

    /* Skip scene and view layer to save space. */
    if ((!ELEM(sbuts->mainb,
               BCONTEXT_RENDER,
               BCONTEXT_OUTPUT,
               BCONTEXT_SCENE,
               BCONTEXT_VIEW_LAYER,
               BCONTEXT_WORLD) &&
         ptr->type == &RNA_Scene)) {
      continue;
    }
    if ((!ELEM(sbuts->mainb,
               BCONTEXT_RENDER,
               BCONTEXT_OUTPUT,
               BCONTEXT_SCENE,
               BCONTEXT_VIEW_LAYER,
               BCONTEXT_WORLD) &&
         ptr->type == &RNA_ViewLayer)) {
      continue;
    }

    /* Add > triangle. */
    if (!first) {
      uiItemL(row, "", ICON_RIGHTARROW);
    }

    if (ptr->data == NULL) {
      continue;
    }

    /* Add icon and name. */
    int icon = RNA_struct_ui_icon(ptr->type);
    char namebuf[128];
    char *name = RNA_struct_name_get_alloc(ptr, namebuf, sizeof(namebuf), NULL);

    if (name) {
      uiItemLDrag(row, ptr, name, icon);

      if (name != namebuf) {
        MEM_freeN(name);
      }
    }
    else {
      uiItemL(row, "", icon);
    }

    first = false;
  }

  uiLayout *pin_row = uiLayoutRow(row, false);
  uiLayoutSetAlignment(pin_row, UI_LAYOUT_ALIGN_RIGHT);
  uiItemSpacer(pin_row);
  uiLayoutSetEmboss(pin_row, UI_EMBOSS_NONE);
  uiItemO(pin_row,
          "",
          (sbuts->flag & SB_PIN_CONTEXT) ? ICON_PINNED : ICON_UNPINNED,
          "BUTTONS_OT_toggle_pin");
}

void buttons_context_register(ARegionType *art)
{
  PanelType *pt = MEM_callocN(sizeof(PanelType), "spacetype buttons panel context");
  strcpy(pt->idname, "PROPERTIES_PT_context");
  strcpy(pt->label, N_("Context")); /* XXX C panels unavailable through RNA bpy.types! */
  strcpy(pt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  pt->poll = buttons_panel_context_poll;
  pt->draw = buttons_panel_context_draw;
  pt->flag = PANEL_TYPE_NO_HEADER | PANEL_TYPE_NO_SEARCH;
  BLI_addtail(&art->paneltypes, pt);
}

ID *buttons_context_id_path(const bContext *C)
{
  SpaceProperties *sbuts = CTX_wm_space_properties(C);
  ButsContextPath *path = sbuts->path;

  if (path->len == 0) {
    return NULL;
  }

  for (int i = path->len - 1; i >= 0; i--) {
    PointerRNA *ptr = &path->ptr[i];

    /* Pin particle settings instead of system, since only settings are an idblock. */
    if (sbuts->mainb == BCONTEXT_PARTICLE && sbuts->flag & SB_PIN_CONTEXT) {
      if (ptr->type == &RNA_ParticleSystem && ptr->data) {
        ParticleSystem *psys = ptr->data;
        return &psys->part->id;
      }
    }

    /* There is no valid image ID panel, Image Empty objects need this workaround. */
    if (sbuts->mainb == BCONTEXT_DATA && sbuts->flag & SB_PIN_CONTEXT) {
      if (ptr->type == &RNA_Image && ptr->data) {
        continue;
      }
    }

    if (ptr->owner_id) {
      return ptr->owner_id;
    }
  }

  return NULL;
}
