/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "STRUCTS_anim_types.h"
#include "STRUCTS_collection_types.h"
#include "STRUCTS_curveprofile_types.h"
#include "STRUCTS_defaults.h"
#include "STRUCTS_gpencil_types.h"
#include "STRUCTS_linestyle_types.h"
#include "STRUCTS_mask_types.h"
#include "STRUCTS_material_types.h"
#include "STRUCTS_mesh_types.h"
#include "STRUCTS_node_types.h"
#include "STRUCTS_object_types.h"
#include "STRUCTS_rigidbody_types.h"
#include "STRUCTS_scene_types.h"
#include "STRUCTS_screen_types.h"
#include "STRUCTS_sequence_types.h"
#include "STRUCTS_sound_types.h"
#include "STRUCTS_space_types.h"
#include "STRUCTS_text_types.h"
#include "STRUCTS_vfont_types.h"
#include "STRUCTS_view3d_types.h"
#include "STRUCTS_windowmanager_types.h"
#include "STRUCTS_workspace_types.h"
#include "STRUCTS_world_types.h"

#include "KERNEL_callbacks.h"
#include "LI_blenlib.h"
#include "LI_math.h"
#include "LI_string.h"
#include "LI_string_utils.h"
#include "LI_task.h"
#include "LI_threads.h"
#include "LI_utildefines.h"

#include "LOADER_readfile.h"

#include "TRANSLATION_translation.h"

#include "KE_action.h"
#include "KE_anim_data.h"
#include "KE_animsys.h"
#include "KE_armature.h"
#include "BKE_bpath.h"
#include "BKE_cachefile.h"
#include "BKE_collection.h"
#include "BKE_colortools.h"
#include "BKE_curveprofile.h"
#include "BKE_duplilist.h"
#include "BKE_editmesh.h"
#include "BKE_effect.h"
#include "BKE_fcurve.h"
#include "BKE_freestyle.h"
#include "BKE_gpencil.h"
#include "BKE_icons.h"
#include "BKE_idprop.h"
#include "BKE_idtype.h"
#include "BKE_image.h"
#include "BKE_image_format.h"
#include "BKE_layer.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_lib_remap.h"
#include "BKE_linestyle.h"
#include "BKE_main.h"
#include "BKE_mask.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_pointcache.h"
#include "BKE_rigidbody.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_sound.h"
#include "BKE_unit.h"
#include "BKE_workspace.h"
#include "BKE_world.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_debug.h"
#include "DEG_depsgraph_query.h"

#include "RE_engine.h"

#include "RNA_access.h"

#include "SEQ_edit.h"
#include "SEQ_iterator.h"
#include "SEQ_sequencer.h"

#include "BLO_read_write.h"

#include "engines/eevee/eevee_lightcache.h"

#include "PIL_time.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf.h"

#include "bmesh.h"

static void scene_init_data(ID *id)
{
  Scene *scene = (Scene *)id;
  const char *colorspace_name;
  SceneRenderView *srv;
  CurveMapping *mblur_shutter_curve;

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(scene, id));

  MEMCPY_STRUCT_AFTER(scene, DNA_struct_default_get(Scene), id);

  BLI_strncpy(scene->r.bake.filepath, U.renderdir, sizeof(scene->r.bake.filepath));

  mblur_shutter_curve = &scene->r.mblur_shutter_curve;
  BKE_curvemapping_set_defaults(mblur_shutter_curve, 1, 0.0f, 0.0f, 1.0f, 1.0f);
  BKE_curvemapping_init(mblur_shutter_curve);
  BKE_curvemap_reset(mblur_shutter_curve->cm,
                     &mblur_shutter_curve->clipr,
                     CURVE_PRESET_MAX,
                     CURVEMAP_SLOPE_POS_NEG);

  scene->toolsettings = DNA_struct_default_alloc(ToolSettings);

  scene->toolsettings->autokey_mode = (uchar)U.autokey_mode;

  /* grease pencil multiframe falloff curve */
  scene->toolsettings->gp_sculpt.cur_falloff = BKE_curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
  CurveMapping *gp_falloff_curve = scene->toolsettings->gp_sculpt.cur_falloff;
  BKE_curvemapping_init(gp_falloff_curve);
  BKE_curvemap_reset(
      gp_falloff_curve->cm, &gp_falloff_curve->clipr, CURVE_PRESET_GAUSS, CURVEMAP_SLOPE_POSITIVE);

  scene->toolsettings->gp_sculpt.cur_primitive = BKE_curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
  CurveMapping *gp_primitive_curve = scene->toolsettings->gp_sculpt.cur_primitive;
  BKE_curvemapping_init(gp_primitive_curve);
  BKE_curvemap_reset(gp_primitive_curve->cm,
                     &gp_primitive_curve->clipr,
                     CURVE_PRESET_BELL,
                     CURVEMAP_SLOPE_POSITIVE);

  scene->unit.system = USER_UNIT_METRIC;
  scene->unit.scale_length = 1.0f;
  scene->unit.length_unit = (uchar)BKE_unit_base_of_type_get(USER_UNIT_METRIC, B_UNIT_LENGTH);
  scene->unit.mass_unit = (uchar)BKE_unit_base_of_type_get(USER_UNIT_METRIC, B_UNIT_MASS);
  scene->unit.time_unit = (uchar)BKE_unit_base_of_type_get(USER_UNIT_METRIC, B_UNIT_TIME);
  scene->unit.temperature_unit = (uchar)BKE_unit_base_of_type_get(USER_UNIT_METRIC,
                                                                  B_UNIT_TEMPERATURE);

  /* Anti-Aliasing threshold. */
  scene->grease_pencil_settings.smaa_threshold = 1.0f;

  {
    ParticleEditSettings *pset;
    pset = &scene->toolsettings->particle;
    for (size_t i = 1; i < ARRAY_SIZE(pset->brush); i++) {
      pset->brush[i] = pset->brush[0];
    }
    pset->brush[PE_BRUSH_CUT].strength = 1.0f;
  }

  BLI_strncpy(scene->r.engine, RE_engine_id_BLENDER_EEVEE, sizeof(scene->r.engine));

  BLI_strncpy(scene->r.pic, U.renderdir, sizeof(scene->r.pic));

  /* NOTE: in header_info.c the scene copy happens...,
   * if you add more to renderdata it has to be checked there. */

  /* multiview - stereo */
  BKE_scene_add_render_view(scene, STEREO_LEFT_NAME);
  srv = scene->r.views.first;
  BLI_strncpy(srv->suffix, STEREO_LEFT_SUFFIX, sizeof(srv->suffix));

  BKE_scene_add_render_view(scene, STEREO_RIGHT_NAME);
  srv = scene->r.views.last;
  BLI_strncpy(srv->suffix, STEREO_RIGHT_SUFFIX, sizeof(srv->suffix));

  BKE_sound_reset_scene_runtime(scene);

  /* color management */
  colorspace_name = IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DEFAULT_SEQUENCER);

  BKE_color_managed_display_settings_init(&scene->display_settings);
  BKE_color_managed_view_settings_init_render(
      &scene->view_settings, &scene->display_settings, "Filmic");
  BLI_strncpy(scene->sequencer_colorspace_settings.name,
              colorspace_name,
              sizeof(scene->sequencer_colorspace_settings.name));

  BKE_image_format_init(&scene->r.im_format, true);
  BKE_image_format_init(&scene->r.bake.im_format, true);

  /* Curve Profile */
  scene->toolsettings->custom_bevel_profile_preset = BKE_curveprofile_add(PROF_PRESET_LINE);

  /* Sequencer */
  scene->toolsettings->sequencer_tool_settings = SEQ_tool_settings_init();

  for (size_t i = 0; i < ARRAY_SIZE(scene->orientation_slots); i++) {
    scene->orientation_slots[i].index_custom = -1;
  }

  /* Master Collection */
  scene->master_collection = BKE_collection_master_add();

  BKE_view_layer_add(scene, "ViewLayer", NULL, VIEWLAYER_ADD_NEW);
}

static void scene_copy_markers(Scene *scene_dst, const Scene *scene_src, const int flag)
{
  BLI_duplicatelist(&scene_dst->markers, &scene_src->markers);
  LISTBASE_FOREACH (TimeMarker *, marker, &scene_dst->markers) {
    if (marker->prop != NULL) {
      marker->prop = IDP_CopyProperty_ex(marker->prop, flag);
    }
  }
}

static void scene_copy_data(Main *bmain, ID *id_dst, const ID *id_src, const int flag)
{
  Scene *scene_dst = (Scene *)id_dst;
  const Scene *scene_src = (const Scene *)id_src;
  /* We never handle usercount here for own data. */
  const int flag_subdata = flag | LIB_ID_CREATE_NO_USER_REFCOUNT;
  /* We always need allocation of our private ID data. */
  const int flag_private_id_data = flag & ~LIB_ID_CREATE_NO_ALLOCATE;

  scene_dst->ed = NULL;
  scene_dst->depsgraph_hash = NULL;
  scene_dst->fps_info = NULL;

  /* Master Collection */
  if (scene_src->master_collection) {
    BKE_id_copy_ex(bmain,
                   (ID *)scene_src->master_collection,
                   (ID **)&scene_dst->master_collection,
                   flag_private_id_data);
  }

  /* View Layers */
  BLI_duplicatelist(&scene_dst->view_layers, &scene_src->view_layers);
  for (ViewLayer *view_layer_src = scene_src->view_layers.first,
                 *view_layer_dst = scene_dst->view_layers.first;
       view_layer_src;
       view_layer_src = view_layer_src->next, view_layer_dst = view_layer_dst->next) {
    BKE_view_layer_copy_data(scene_dst, scene_src, view_layer_dst, view_layer_src, flag_subdata);
  }

  scene_copy_markers(scene_dst, scene_src, flag);

  BLI_duplicatelist(&(scene_dst->transform_spaces), &(scene_src->transform_spaces));
  BLI_duplicatelist(&(scene_dst->r.views), &(scene_src->r.views));
  BKE_keyingsets_copy(&(scene_dst->keyingsets), &(scene_src->keyingsets));

  if (scene_src->nodetree) {
    BKE_id_copy_ex(
        bmain, (ID *)scene_src->nodetree, (ID **)&scene_dst->nodetree, flag_private_id_data);
    BKE_libblock_relink_ex(bmain,
                           scene_dst->nodetree,
                           (void *)(&scene_src->id),
                           &scene_dst->id,
                           ID_REMAP_SKIP_NEVER_NULL_USAGE);
  }

  if (scene_src->rigidbody_world) {
    scene_dst->rigidbody_world = BKE_rigidbody_world_copy(scene_src->rigidbody_world,
                                                          flag_subdata);
  }

  /* copy color management settings */
  BKE_color_managed_display_settings_copy(&scene_dst->display_settings,
                                          &scene_src->display_settings);
  BKE_color_managed_view_settings_copy(&scene_dst->view_settings, &scene_src->view_settings);
  BKE_color_managed_colorspace_settings_copy(&scene_dst->sequencer_colorspace_settings,
                                             &scene_src->sequencer_colorspace_settings);

  BKE_image_format_copy(&scene_dst->r.im_format, &scene_src->r.im_format);
  BKE_image_format_copy(&scene_dst->r.bake.im_format, &scene_src->r.bake.im_format);

  BKE_curvemapping_copy_data(&scene_dst->r.mblur_shutter_curve, &scene_src->r.mblur_shutter_curve);

  /* tool settings */
  scene_dst->toolsettings = BKE_toolsettings_copy(scene_dst->toolsettings, flag_subdata);

  /* make a private copy of the avicodecdata */
  if (scene_src->r.avicodecdata) {
    scene_dst->r.avicodecdata = MEM_dupallocN(scene_src->r.avicodecdata);
    scene_dst->r.avicodecdata->lpFormat = MEM_dupallocN(scene_dst->r.avicodecdata->lpFormat);
    scene_dst->r.avicodecdata->lpParms = MEM_dupallocN(scene_dst->r.avicodecdata->lpParms);
  }

  if (scene_src->display.shading.prop) {
    scene_dst->display.shading.prop = IDP_CopyProperty(scene_src->display.shading.prop);
  }

  BKE_sound_reset_scene_runtime(scene_dst);

  /* Copy sequencer, this is local data! */
  if (scene_src->ed) {
    scene_dst->ed = MEM_callocN(sizeof(*scene_dst->ed), __func__);
    scene_dst->ed->seqbasep = &scene_dst->ed->seqbase;
    SEQ_sequence_base_dupli_recursive(scene_src,
                                      scene_dst,
                                      &scene_dst->ed->seqbase,
                                      &scene_src->ed->seqbase,
                                      SEQ_DUPE_ALL,
                                      flag_subdata);
  }

  if ((flag & LIB_ID_COPY_NO_PREVIEW) == 0) {
    BKE_previewimg_id_copy(&scene_dst->id, &scene_src->id);
  }
  else {
    scene_dst->preview = NULL;
  }

  BKE_scene_copy_data_eevee(scene_dst, scene_src);
}

static void scene_free_markers(Scene *scene, bool do_id_user)
{
  LISTBASE_FOREACH_MUTABLE (TimeMarker *, marker, &scene->markers) {
    if (marker->prop != NULL) {
      IDP_FreePropertyContent_ex(marker->prop, do_id_user);
      MEM_freeN(marker->prop);
    }
    MEM_freeN(marker);
  }
}

static void scene_free_data(ID *id)
{

  Scene *scene = (Scene *)id;
  const bool do_id_user = false;

  SEQ_editing_free(scene, do_id_user);

  BKE_keyingsets_free(&scene->keyingsets);

  /* is no lib link block, but scene extension */
  if (scene->nodetree) {
    ntreeFreeEmbeddedTree(scene->nodetree);
    MEM_freeN(scene->nodetree);
    scene->nodetree = NULL;
  }

  if (scene->rigidbody_world) {
    /* Prevent rigidbody freeing code to follow other IDs pointers, this should never be allowed
     * nor necessary from here, and with new undo code, those pointers may be fully invalid or
     * worse, pointing to data actually belonging to new BMain! */
    scene->rigidbody_world->constraints = NULL;
    scene->rigidbody_world->group = NULL;
    BKE_rigidbody_free_world(scene);
  }

  if (scene->r.avicodecdata) {
    free_avicodecdata(scene->r.avicodecdata);
    MEM_freeN(scene->r.avicodecdata);
    scene->r.avicodecdata = NULL;
  }

  scene_free_markers(scene, do_id_user);
  BLI_freelistN(&scene->transform_spaces);
  BLI_freelistN(&scene->r.views);

  BKE_toolsettings_free(scene->toolsettings);
  scene->toolsettings = NULL;

  BKE_scene_free_depsgraph_hash(scene);

  MEM_SAFE_FREE(scene->fps_info);

  BKE_sound_destroy_scene(scene);

  BKE_color_managed_view_settings_free(&scene->view_settings);
  BKE_image_format_free(&scene->r.im_format);
  BKE_image_format_free(&scene->r.bake.im_format);

  BKE_previewimg_free(&scene->preview);
  BKE_curvemapping_free_data(&scene->r.mblur_shutter_curve);

  for (ViewLayer *view_layer = scene->view_layers.first, *view_layer_next; view_layer;
       view_layer = view_layer_next) {
    view_layer_next = view_layer->next;

    BLI_remlink(&scene->view_layers, view_layer);
    BKE_view_layer_free_ex(view_layer, do_id_user);
  }

  /* Master Collection */
  /* TODO: what to do with do_id_user? it's also true when just
   * closing the file which seems wrong? should decrement users
   * for objects directly in the master collection? then other
   * collections in the scene need to do it too? */
  if (scene->master_collection) {
    BKE_collection_free_data(scene->master_collection);
    BKE_libblock_free_data_py(&scene->master_collection->id);
    MEM_freeN(scene->master_collection);
    scene->master_collection = NULL;
  }

  if (scene->eevee.light_cache_data) {
    EEVEE_lightcache_free(scene->eevee.light_cache_data);
    scene->eevee.light_cache_data = NULL;
  }

  if (scene->display.shading.prop) {
    IDP_FreeProperty(scene->display.shading.prop);
    scene->display.shading.prop = NULL;
  }

  /* These are freed on doversion. */
  BLI_assert(scene->layer_properties == NULL);
}

static void scene_foreach_rigidbodyworldSceneLooper(struct RigidBodyWorld *UNUSED(rbw),
                                                    ID **id_pointer,
                                                    void *user_data,
                                                    int cb_flag)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;
  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
      data, BKE_lib_query_foreachid_process(data, id_pointer, cb_flag));
}

/**
 * This code is shared by both the regular `foreach_id` looper, and the code trying to restore or
 * preserve ID pointers like brushes across undo-steps.
 */
typedef enum eSceneForeachUndoPreserveProcess {
  /* Undo when preserving tool-settings from old scene, we also want to try to preserve that ID
   * pointer from its old scene's value. */
  SCENE_FOREACH_UNDO_RESTORE,
  /* Undo when preserving tool-settings from old scene, we want to keep the new value of that ID
   * pointer. */
  SCENE_FOREACH_UNDO_NO_RESTORE,
} eSceneForeachUndoPreserveProcess;

static void scene_foreach_toolsettings_id_pointer_process(
    ID **id_p,
    const eSceneForeachUndoPreserveProcess action,
    BlendLibReader *reader,
    ID **id_old_p,
    const uint cb_flag)
{
  switch (action) {
    case SCENE_FOREACH_UNDO_RESTORE: {
      ID *id_old = *id_old_p;
      /* Old data has not been remapped to new values of the pointers, if we want to keep the old
       * pointer here we need its new address. */
      ID *id_old_new = id_old != NULL ? BLO_read_get_new_id_address(reader, id_old->lib, id_old) :
                                        NULL;
      if (id_old_new != NULL) {
        BLI_assert(ELEM(id_old, id_old_new, id_old_new->orig_id));
        *id_old_p = id_old_new;
        if (cb_flag & IDWALK_CB_USER) {
          id_us_plus_no_lib(id_old_new);
          id_us_min(id_old);
        }
        break;
      }
      /* We failed to find a new valid pointer for the previous ID, just keep the current one as
       * if we had been under SCENE_FOREACH_UNDO_NO_RESTORE case. */
      SWAP(ID *, *id_p, *id_old_p);
      break;
    }
    case SCENE_FOREACH_UNDO_NO_RESTORE:
      /* Counteract the swap of the whole ToolSettings container struct. */
      SWAP(ID *, *id_p, *id_old_p);
      break;
  }
}

/* Special handling is needed here, as `scene_foreach_toolsettings` (and its dependency
 * `scene_foreach_paint`) are also used by `scene_undo_preserve`, where `LibraryForeachIDData
 * *data` is NULL. */
#define BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER( \
    __data, __id, __do_undo_restore, __action, __reader, __id_old, __cb_flag) \
  { \
    if (__do_undo_restore) { \
      scene_foreach_toolsettings_id_pointer_process( \
          (ID **)&(__id), __action, __reader, (ID **)&(__id_old), __cb_flag); \
    } \
    else { \
      BLI_assert((__data) != NULL); \
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(__data, __id, __cb_flag); \
    } \
  } \
  (void)0

#define BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL( \
    __data, __do_undo_restore, __func_call) \
  { \
    if (__do_undo_restore) { \
      __func_call; \
    } \
    else { \
      BLI_assert((__data) != NULL); \
      BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(__data, __func_call); \
    } \
  } \
  (void)0

static void scene_foreach_paint(LibraryForeachIDData *data,
                                Paint *paint,
                                const bool do_undo_restore,
                                BlendLibReader *reader,
                                Paint *paint_old)
{
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  paint->brush,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_RESTORE,
                                                  reader,
                                                  paint_old->brush,
                                                  IDWALK_CB_USER);
  for (int i = 0; i < paint_old->tool_slots_len; i++) {
    /* This is a bit tricky.
     *  - In case we do not do `undo_restore`, `paint` and `paint_old` pointers are the same, so
     *    this is equivalent to simply looping over slots from `paint`.
     *  - In case we do `undo_restore`, we only want to consider the slots from the old one, since
     *    those are the one we keep in the end.
     *    + In case the new data has less valid slots, we feed in a dummy NULL pointer.
     *    + In case the new data has more valid slots, the extra ones are ignored.
     */
    Brush *brush_tmp = NULL;
    Brush **brush_p = i < paint->tool_slots_len ? &paint->tool_slots[i].brush : &brush_tmp;
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                    *brush_p,
                                                    do_undo_restore,
                                                    SCENE_FOREACH_UNDO_RESTORE,
                                                    reader,
                                                    paint_old->brush,
                                                    IDWALK_CB_USER);
  }
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  paint->palette,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_RESTORE,
                                                  reader,
                                                  paint_old->palette,
                                                  IDWALK_CB_USER);
}

static void scene_foreach_toolsettings(LibraryForeachIDData *data,
                                       ToolSettings *toolsett,
                                       const bool do_undo_restore,
                                       BlendLibReader *reader,
                                       ToolSettings *toolsett_old)
{
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->particle.scene,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_NO_RESTORE,
                                                  reader,
                                                  toolsett_old->particle.scene,
                                                  IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->particle.object,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_NO_RESTORE,
                                                  reader,
                                                  toolsett_old->particle.object,
                                                  IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->particle.shape_object,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_NO_RESTORE,
                                                  reader,
                                                  toolsett_old->particle.shape_object,
                                                  IDWALK_CB_NOP);

  scene_foreach_paint(
      data, &toolsett->imapaint.paint, do_undo_restore, reader, &toolsett_old->imapaint.paint);
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->imapaint.stencil,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_RESTORE,
                                                  reader,
                                                  toolsett_old->imapaint.stencil,
                                                  IDWALK_CB_USER);
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->imapaint.clone,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_RESTORE,
                                                  reader,
                                                  toolsett_old->imapaint.clone,
                                                  IDWALK_CB_USER);
  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->imapaint.canvas,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_RESTORE,
                                                  reader,
                                                  toolsett_old->imapaint.canvas,
                                                  IDWALK_CB_USER);

  if (toolsett->vpaint) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->vpaint->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->vpaint->paint));
  }
  if (toolsett->wpaint) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->wpaint->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->wpaint->paint));
  }
  if (toolsett->sculpt) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->sculpt->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->sculpt->paint));
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                    toolsett->sculpt->gravity_object,
                                                    do_undo_restore,
                                                    SCENE_FOREACH_UNDO_NO_RESTORE,
                                                    reader,
                                                    toolsett_old->sculpt->gravity_object,
                                                    IDWALK_CB_NOP);
  }
  if (toolsett->uvsculpt) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->uvsculpt->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->uvsculpt->paint));
  }
  if (toolsett->gp_paint) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->gp_paint->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->gp_paint->paint));
  }
  if (toolsett->gp_vertexpaint) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->gp_vertexpaint->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->gp_vertexpaint->paint));
  }
  if (toolsett->gp_sculptpaint) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->gp_sculptpaint->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->gp_sculptpaint->paint));
  }
  if (toolsett->gp_weightpaint) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->gp_weightpaint->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->gp_weightpaint->paint));
  }
  if (toolsett->curves_sculpt) {
    BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL(
        data,
        do_undo_restore,
        scene_foreach_paint(data,
                            &toolsett->curves_sculpt->paint,
                            do_undo_restore,
                            reader,
                            &toolsett_old->curves_sculpt->paint));
  }

  BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER(data,
                                                  toolsett->gp_sculpt.guide.reference_object,
                                                  do_undo_restore,
                                                  SCENE_FOREACH_UNDO_NO_RESTORE,
                                                  reader,
                                                  toolsett_old->gp_sculpt.guide.reference_object,
                                                  IDWALK_CB_NOP);
}

#undef BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_IDSUPER
#undef BKE_LIB_FOREACHID_UNDO_PRESERVE_PROCESS_FUNCTION_CALL

static void scene_foreach_layer_collection(LibraryForeachIDData *data, ListBase *lb)
{
  LISTBASE_FOREACH (LayerCollection *, lc, lb) {
    /* XXX This is very weak. The whole idea of keeping pointers to private IDs is very bad
     * anyway... */
    const int cb_flag = (lc->collection != NULL &&
                         (lc->collection->id.flag & LIB_EMBEDDED_DATA) != 0) ?
                            IDWALK_CB_EMBEDDED :
                            IDWALK_CB_NOP;
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, lc->collection, cb_flag);
    scene_foreach_layer_collection(data, &lc->layer_collections);
  }
}

static bool seq_foreach_member_id_cb(Sequence *seq, void *user_data)
{
  LibraryForeachIDData *data = (LibraryForeachIDData *)user_data;

#define FOREACHID_PROCESS_IDSUPER(_data, _id_super, _cb_flag) \
  { \
    CHECK_TYPE(&((_id_super)->id), ID *); \
    BKE_lib_query_foreachid_process((_data), (ID **)&(_id_super), (_cb_flag)); \
    if (BKE_lib_query_foreachid_iter_stop((_data))) { \
      return false; \
    } \
  } \
  ((void)0)

  FOREACHID_PROCESS_IDSUPER(data, seq->scene, IDWALK_CB_NEVER_SELF);
  FOREACHID_PROCESS_IDSUPER(data, seq->scene_camera, IDWALK_CB_NOP);
  FOREACHID_PROCESS_IDSUPER(data, seq->clip, IDWALK_CB_USER);
  FOREACHID_PROCESS_IDSUPER(data, seq->mask, IDWALK_CB_USER);
  FOREACHID_PROCESS_IDSUPER(data, seq->sound, IDWALK_CB_USER);
  IDP_foreach_property(
      seq->prop, IDP_TYPE_FILTER_ID, BKE_lib_query_idpropertiesForeachIDLink_callback, data);
  LISTBASE_FOREACH (SequenceModifierData *, smd, &seq->modifiers) {
    FOREACHID_PROCESS_IDSUPER(data, smd->mask_id, IDWALK_CB_USER);
  }

  if (seq->type == SEQ_TYPE_TEXT && seq->effectdata) {
    TextVars *text_data = seq->effectdata;
    FOREACHID_PROCESS_IDSUPER(data, text_data->text_font, IDWALK_CB_USER);
  }

#undef FOREACHID_PROCESS_IDSUPER

  return true;
}

static void scene_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Scene *scene = (Scene *)id;

  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, scene->camera, IDWALK_CB_NOP);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, scene->world, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, scene->set, IDWALK_CB_NEVER_SELF);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, scene->clip, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, scene->gpd, IDWALK_CB_USER);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, scene->r.bake.cage_object, IDWALK_CB_NOP);
  if (scene->nodetree) {
    /* nodetree **are owned by IDs**, treat them as mere sub-data and not real ID! */
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, BKE_library_foreach_ID_embedded(data, (ID **)&scene->nodetree));
  }
  if (scene->ed) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, SEQ_for_each_callback(&scene->ed->seqbase, seq_foreach_member_id_cb, data));
  }

  BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(data,
                                          BKE_keyingsets_foreach_id(data, &scene->keyingsets));

  /* This pointer can be NULL during old files reading, better be safe than sorry. */
  if (scene->master_collection != NULL) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, BKE_library_foreach_ID_embedded(data, (ID **)&scene->master_collection));
  }

  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, view_layer->mat_override, IDWALK_CB_USER);

    LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
      BKE_LIB_FOREACHID_PROCESS_IDSUPER(
          data, base->object, IDWALK_CB_NOP | IDWALK_CB_OVERRIDE_LIBRARY_NOT_OVERRIDABLE);
    }

    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, scene_foreach_layer_collection(data, &view_layer->layer_collections));

    LISTBASE_FOREACH (FreestyleModuleConfig *, fmc, &view_layer->freestyle_config.modules) {
      if (fmc->script) {
        BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, fmc->script, IDWALK_CB_NOP);
      }
    }

    LISTBASE_FOREACH (FreestyleLineSet *, fls, &view_layer->freestyle_config.linesets) {
      if (fls->group) {
        BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, fls->group, IDWALK_CB_USER);
      }

      if (fls->linestyle) {
        BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, fls->linestyle, IDWALK_CB_USER);
      }
    }
  }

  LISTBASE_FOREACH (TimeMarker *, marker, &scene->markers) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, marker->camera, IDWALK_CB_NOP);
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data,
        IDP_foreach_property(marker->prop,
                             IDP_TYPE_FILTER_ID,
                             BKE_lib_query_idpropertiesForeachIDLink_callback,
                             data));
  }

  ToolSettings *toolsett = scene->toolsettings;
  if (toolsett) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data, scene_foreach_toolsettings(data, toolsett, false, NULL, toolsett));
  }

  if (scene->rigidbody_world) {
    BKE_LIB_FOREACHID_PROCESS_FUNCTION_CALL(
        data,
        BKE_rigidbody_world_id_loop(
            scene->rigidbody_world, scene_foreach_rigidbodyworldSceneLooper, data));
  }
}

static void scene_foreach_cache(ID *id,
                                IDTypeForeachCacheFunctionCallback function_callback,
                                void *user_data)
{
  Scene *scene = (Scene *)id;
  IDCacheKey key = {
      .id_session_uuid = id->session_uuid,
      .offset_in_ID = offsetof(Scene, eevee.light_cache_data),
      .cache_v = scene->eevee.light_cache_data,
  };

  function_callback(id,
                    &key,
                    (void **)&scene->eevee.light_cache_data,
                    IDTYPE_CACHE_CB_FLAGS_PERSISTENT,
                    user_data);
}

static bool seq_foreach_path_callback(Sequence *seq, void *user_data)
{
  if (SEQ_HAS_PATH(seq)) {
    StripElem *se = seq->strip->stripdata;
    BPathForeachPathData *bpath_data = (BPathForeachPathData *)user_data;

    if (ELEM(seq->type, SEQ_TYPE_MOVIE, SEQ_TYPE_SOUND_RAM) && se) {
      BKE_bpath_foreach_path_dirfile_fixed_process(bpath_data, seq->strip->dir, se->name);
    }
    else if ((seq->type == SEQ_TYPE_IMAGE) && se) {
      /* NOTE: An option not to loop over all strips could be useful? */
      unsigned int len = (unsigned int)MEM_allocN_len(se) / (unsigned int)sizeof(*se);
      unsigned int i;

      if (bpath_data->flag & BKE_BPATH_FOREACH_PATH_SKIP_MULTIFILE) {
        /* only operate on one path */
        len = MIN2(1u, len);
      }

      for (i = 0; i < len; i++, se++) {
        BKE_bpath_foreach_path_dirfile_fixed_process(bpath_data, seq->strip->dir, se->name);
      }
    }
    else {
      /* simple case */
      BKE_bpath_foreach_path_fixed_process(bpath_data, seq->strip->dir);
    }
  }
  return true;
}

static void scene_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Scene *scene = (Scene *)id;
  if (scene->ed != NULL) {
    SEQ_for_each_callback(&scene->ed->seqbase, seq_foreach_path_callback, bpath_data);
  }
}

static void scene_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  Scene *sce = (Scene *)id;

  if (BLO_write_is_undo(writer)) {
    /* Clean up, important in undo case to reduce false detection of changed data-blocks. */
    /* XXX This UI data should not be stored in Scene at all... */
    memset(&sce->cursor, 0, sizeof(sce->cursor));
  }

  /* write LibData */
  BLO_write_id_struct(writer, Scene, id_address, &sce->id);
  BKE_id_blend_write(writer, &sce->id);

  if (sce->adt) {
    BKE_animdata_blend_write(writer, sce->adt);
  }
  BKE_keyingsets_blend_write(writer, &sce->keyingsets);

  /* direct data */
  ToolSettings *tos = sce->toolsettings;
  BLO_write_struct(writer, ToolSettings, tos);
  if (tos->vpaint) {
    BLO_write_struct(writer, VPaint, tos->vpaint);
    BKE_paint_blend_write(writer, &tos->vpaint->paint);
  }
  if (tos->wpaint) {
    BLO_write_struct(writer, VPaint, tos->wpaint);
    BKE_paint_blend_write(writer, &tos->wpaint->paint);
  }
  if (tos->sculpt) {
    BLO_write_struct(writer, Sculpt, tos->sculpt);
    BKE_paint_blend_write(writer, &tos->sculpt->paint);
  }
  if (tos->uvsculpt) {
    BLO_write_struct(writer, UvSculpt, tos->uvsculpt);
    BKE_paint_blend_write(writer, &tos->uvsculpt->paint);
  }
  if (tos->gp_paint) {
    BLO_write_struct(writer, GpPaint, tos->gp_paint);
    BKE_paint_blend_write(writer, &tos->gp_paint->paint);
  }
  if (tos->gp_vertexpaint) {
    BLO_write_struct(writer, GpVertexPaint, tos->gp_vertexpaint);
    BKE_paint_blend_write(writer, &tos->gp_vertexpaint->paint);
  }
  if (tos->gp_sculptpaint) {
    BLO_write_struct(writer, GpSculptPaint, tos->gp_sculptpaint);
    BKE_paint_blend_write(writer, &tos->gp_sculptpaint->paint);
  }
  if (tos->gp_weightpaint) {
    BLO_write_struct(writer, GpWeightPaint, tos->gp_weightpaint);
    BKE_paint_blend_write(writer, &tos->gp_weightpaint->paint);
  }
  if (tos->curves_sculpt) {
    BLO_write_struct(writer, CurvesSculpt, tos->curves_sculpt);
    BKE_paint_blend_write(writer, &tos->curves_sculpt->paint);
  }
  /* write grease-pencil custom ipo curve to file */
  if (tos->gp_interpolate.custom_ipo) {
    BKE_curvemapping_blend_write(writer, tos->gp_interpolate.custom_ipo);
  }
  /* write grease-pencil multiframe falloff curve to file */
  if (tos->gp_sculpt.cur_falloff) {
    BKE_curvemapping_blend_write(writer, tos->gp_sculpt.cur_falloff);
  }
  /* write grease-pencil primitive curve to file */
  if (tos->gp_sculpt.cur_primitive) {
    BKE_curvemapping_blend_write(writer, tos->gp_sculpt.cur_primitive);
  }
  /* Write the curve profile to the file. */
  if (tos->custom_bevel_profile_preset) {
    BKE_curveprofile_blend_write(writer, tos->custom_bevel_profile_preset);
  }
  if (tos->sequencer_tool_settings) {
    BLO_write_struct(writer, SequencerToolSettings, tos->sequencer_tool_settings);
  }

  BKE_paint_blend_write(writer, &tos->imapaint.paint);

  Editing *ed = sce->ed;
  if (ed) {
    BLO_write_struct(writer, Editing, ed);

    SEQ_blend_write(writer, &ed->seqbase);
    /* new; meta stack too, even when its nasty restore code */
    LISTBASE_FOREACH (MetaStack *, ms, &ed->metastack) {
      BLO_write_struct(writer, MetaStack, ms);
    }
  }

  if (sce->r.avicodecdata) {
    BLO_write_struct(writer, AviCodecData, sce->r.avicodecdata);
    if (sce->r.avicodecdata->lpFormat) {
      BLO_write_raw(writer, (size_t)sce->r.avicodecdata->cbFormat, sce->r.avicodecdata->lpFormat);
    }
    if (sce->r.avicodecdata->lpParms) {
      BLO_write_raw(writer, (size_t)sce->r.avicodecdata->cbParms, sce->r.avicodecdata->lpParms);
    }
  }

  /* writing dynamic list of TimeMarkers to the blend file */
  LISTBASE_FOREACH (TimeMarker *, marker, &sce->markers) {
    BLO_write_struct(writer, TimeMarker, marker);

    if (marker->prop != NULL) {
      IDP_BlendWrite(writer, marker->prop);
    }
  }

  /* writing dynamic list of TransformOrientations to the blend file */
  LISTBASE_FOREACH (TransformOrientation *, ts, &sce->transform_spaces) {
    BLO_write_struct(writer, TransformOrientation, ts);
  }

  /* writing MultiView to the blend file */
  LISTBASE_FOREACH (SceneRenderView *, srv, &sce->r.views) {
    BLO_write_struct(writer, SceneRenderView, srv);
  }

  if (sce->nodetree) {
    BLO_write_struct(writer, bNodeTree, sce->nodetree);
    ntreeBlendWrite(writer, sce->nodetree);
  }

  BKE_color_managed_view_settings_blend_write(writer, &sce->view_settings);
  BKE_image_format_blend_write(writer, &sce->r.im_format);
  BKE_image_format_blend_write(writer, &sce->r.bake.im_format);

  /* writing RigidBodyWorld data to the blend file */
  if (sce->rigidbody_world) {
    /* Set deprecated pointers to prevent crashes of older Blenders */
    sce->rigidbody_world->pointcache = sce->rigidbody_world->shared->pointcache;
    sce->rigidbody_world->ptcaches = sce->rigidbody_world->shared->ptcaches;
    BLO_write_struct(writer, RigidBodyWorld, sce->rigidbody_world);

    BLO_write_struct(writer, RigidBodyWorld_Shared, sce->rigidbody_world->shared);
    BLO_write_struct(writer, EffectorWeights, sce->rigidbody_world->effector_weights);
    BKE_ptcache_blend_write(writer, &(sce->rigidbody_world->shared->ptcaches));
  }

  BKE_previewimg_blend_write(writer, sce->preview);
  BKE_curvemapping_curves_blend_write(writer, &sce->r.mblur_shutter_curve);

  LISTBASE_FOREACH (ViewLayer *, view_layer, &sce->view_layers) {
    BKE_view_layer_blend_write(writer, view_layer);
  }

  if (sce->master_collection) {
    BLO_write_struct(writer, Collection, sce->master_collection);
    BKE_collection_blend_write_nolib(writer, sce->master_collection);
  }

  /* Eevee Lightcache */
  if (sce->eevee.light_cache_data && !BLO_write_is_undo(writer)) {
    BLO_write_struct(writer, LightCache, sce->eevee.light_cache_data);
    EEVEE_lightcache_blend_write(writer, sce->eevee.light_cache_data);
  }

  BKE_screen_view3d_shading_blend_write(writer, &sce->display.shading);

  /* Freed on doversion. */
  BLI_assert(sce->layer_properties == NULL);
}

static void direct_link_paint_helper(BlendDataReader *reader, const Scene *scene, Paint **paint)
{
  /* TODO: is this needed. */
  BLO_read_data_address(reader, paint);

  if (*paint) {
    BKE_paint_blend_read_data(reader, scene, *paint);
  }
}

static void link_recurs_seq(BlendDataReader *reader, ListBase *lb)
{
  BLO_read_list(reader, lb);

  LISTBASE_FOREACH_MUTABLE (Sequence *, seq, lb) {
    /* Sanity check. */
    if (!SEQ_valid_strip_channel(seq)) {
      BLI_freelinkN(lb, seq);
      BLO_read_data_reports(reader)->count.sequence_strips_skipped++;
    }
    else if (seq->seqbase.first) {
      link_recurs_seq(reader, &seq->seqbase);
    }
  }
}

static void scene_blend_read_data(BlendDataReader *reader, ID *id)
{
  Scene *sce = (Scene *)id;

  sce->depsgraph_hash = NULL;
  sce->fps_info = NULL;

  memset(&sce->customdata_mask, 0, sizeof(sce->customdata_mask));
  memset(&sce->customdata_mask_modal, 0, sizeof(sce->customdata_mask_modal));

  BKE_sound_reset_scene_runtime(sce);

  /* set users to one by default, not in lib-link, this will increase it for compo nodes */
  id_us_ensure_real(&sce->id);

  BLO_read_list(reader, &(sce->base));

  BLO_read_data_address(reader, &sce->adt);
  BKE_animdata_blend_read_data(reader, sce->adt);

  BLO_read_list(reader, &sce->keyingsets);
  BKE_keyingsets_blend_read_data(reader, &sce->keyingsets);

  BLO_read_data_address(reader, &sce->basact);

  BLO_read_data_address(reader, &sce->toolsettings);
  if (sce->toolsettings) {

    /* Reset last_location and last_hit, so they are not remembered across sessions. In some files
     * these are also NaN, which could lead to crashes in painting. */
    struct UnifiedPaintSettings *ups = &sce->toolsettings->unified_paint_settings;
    zero_v3(ups->last_location);
    ups->last_hit = 0;

    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->sculpt);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->vpaint);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->wpaint);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->uvsculpt);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->gp_paint);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->gp_vertexpaint);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->gp_sculptpaint);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->gp_weightpaint);
    direct_link_paint_helper(reader, sce, (Paint **)&sce->toolsettings->curves_sculpt);

    BKE_paint_blend_read_data(reader, sce, &sce->toolsettings->imapaint.paint);

    sce->toolsettings->particle.paintcursor = NULL;
    sce->toolsettings->particle.scene = NULL;
    sce->toolsettings->particle.object = NULL;
    sce->toolsettings->gp_sculpt.paintcursor = NULL;

    /* relink grease pencil interpolation curves */
    BLO_read_data_address(reader, &sce->toolsettings->gp_interpolate.custom_ipo);
    if (sce->toolsettings->gp_interpolate.custom_ipo) {
      BKE_curvemapping_blend_read(reader, sce->toolsettings->gp_interpolate.custom_ipo);
    }
    /* relink grease pencil multiframe falloff curve */
    BLO_read_data_address(reader, &sce->toolsettings->gp_sculpt.cur_falloff);
    if (sce->toolsettings->gp_sculpt.cur_falloff) {
      BKE_curvemapping_blend_read(reader, sce->toolsettings->gp_sculpt.cur_falloff);
    }
    /* relink grease pencil primitive curve */
    BLO_read_data_address(reader, &sce->toolsettings->gp_sculpt.cur_primitive);
    if (sce->toolsettings->gp_sculpt.cur_primitive) {
      BKE_curvemapping_blend_read(reader, sce->toolsettings->gp_sculpt.cur_primitive);
    }

    /* Relink toolsettings curve profile */
    BLO_read_data_address(reader, &sce->toolsettings->custom_bevel_profile_preset);
    if (sce->toolsettings->custom_bevel_profile_preset) {
      BKE_curveprofile_blend_read(reader, sce->toolsettings->custom_bevel_profile_preset);
    }

    BLO_read_data_address(reader, &sce->toolsettings->sequencer_tool_settings);
  }

  if (sce->ed) {
    ListBase *old_seqbasep = &sce->ed->seqbase;

    BLO_read_data_address(reader, &sce->ed);
    Editing *ed = sce->ed;

    BLO_read_data_address(reader, &ed->act_seq);
    ed->cache = NULL;
    ed->prefetch_job = NULL;
    ed->runtime.sequence_lookup = NULL;

    /* recursive link sequences, lb will be correctly initialized */
    link_recurs_seq(reader, &ed->seqbase);

    /* Read in sequence member data. */
    SEQ_blend_read(reader, &ed->seqbase);

    /* link metastack, slight abuse of structs here,
     * have to restore pointer to internal part in struct */
    {
      Sequence temp;
      void *poin;
      intptr_t offset;

      offset = ((intptr_t) & (temp.seqbase)) - ((intptr_t)&temp);

      /* root pointer */
      if (ed->seqbasep == old_seqbasep) {
        ed->seqbasep = &ed->seqbase;
      }
      else {
        poin = POINTER_OFFSET(ed->seqbasep, -offset);

        poin = BLO_read_get_new_data_address(reader, poin);

        if (poin) {
          ed->seqbasep = (ListBase *)POINTER_OFFSET(poin, offset);
        }
        else {
          ed->seqbasep = &ed->seqbase;
        }
      }
      /* stack */
      BLO_read_list(reader, &(ed->metastack));

      LISTBASE_FOREACH (MetaStack *, ms, &ed->metastack) {
        BLO_read_data_address(reader, &ms->parseq);

        if (ms->oldbasep == old_seqbasep) {
          ms->oldbasep = &ed->seqbase;
        }
        else {
          poin = POINTER_OFFSET(ms->oldbasep, -offset);
          poin = BLO_read_get_new_data_address(reader, poin);
          if (poin) {
            ms->oldbasep = (ListBase *)POINTER_OFFSET(poin, offset);
          }
          else {
            ms->oldbasep = &ed->seqbase;
          }
        }
      }
    }
  }

#ifdef DURIAN_CAMERA_SWITCH
  /* Runtime */
  sce->r.mode &= ~R_NO_CAMERA_SWITCH;
#endif

  BLO_read_data_address(reader, &sce->r.avicodecdata);
  if (sce->r.avicodecdata) {
    BLO_read_data_address(reader, &sce->r.avicodecdata->lpFormat);
    BLO_read_data_address(reader, &sce->r.avicodecdata->lpParms);
  }
  BLO_read_list(reader, &(sce->markers));
  LISTBASE_FOREACH (TimeMarker *, marker, &sce->markers) {
    BLO_read_data_address(reader, &marker->prop);
    IDP_BlendDataRead(reader, &marker->prop);
  }

  BLO_read_list(reader, &(sce->transform_spaces));
  BLO_read_list(reader, &(sce->r.layers));
  BLO_read_list(reader, &(sce->r.views));

  LISTBASE_FOREACH (SceneRenderLayer *, srl, &sce->r.layers) {
    BLO_read_data_address(reader, &srl->prop);
    IDP_BlendDataRead(reader, &srl->prop);
    BLO_read_list(reader, &(srl->freestyleConfig.modules));
    BLO_read_list(reader, &(srl->freestyleConfig.linesets));
  }

  BKE_color_managed_view_settings_blend_read_data(reader, &sce->view_settings);
  BKE_image_format_blend_read_data(reader, &sce->r.im_format);
  BKE_image_format_blend_read_data(reader, &sce->r.bake.im_format);

  BLO_read_data_address(reader, &sce->rigidbody_world);
  RigidBodyWorld *rbw = sce->rigidbody_world;
  if (rbw) {
    BLO_read_data_address(reader, &rbw->shared);

    if (rbw->shared == NULL) {
      /* Link deprecated caches if they exist, so we can use them for versioning.
       * We should only do this when rbw->shared == NULL, because those pointers
       * are always set (for compatibility with older Blenders). We mustn't link
       * the same pointcache twice. */
      BKE_ptcache_blend_read_data(reader, &rbw->ptcaches, &rbw->pointcache, false);

      /* make sure simulation starts from the beginning after loading file */
      if (rbw->pointcache) {
        rbw->ltime = (float)rbw->pointcache->startframe;
      }
    }
    else {
      /* must nullify the reference to physics sim object, since it no-longer exist
       * (and will need to be recalculated)
       */
      rbw->shared->physics_world = NULL;

      /* link caches */
      BKE_ptcache_blend_read_data(reader, &rbw->shared->ptcaches, &rbw->shared->pointcache, false);

      /* make sure simulation starts from the beginning after loading file */
      if (rbw->shared->pointcache) {
        rbw->ltime = (float)rbw->shared->pointcache->startframe;
      }
    }
    rbw->objects = NULL;
    rbw->numbodies = 0;

    /* set effector weights */
    BLO_read_data_address(reader, &rbw->effector_weights);
    if (!rbw->effector_weights) {
      rbw->effector_weights = BKE_effector_add_weights(NULL);
    }
  }

  BLO_read_data_address(reader, &sce->preview);
  BKE_previewimg_blend_read(reader, sce->preview);

  BKE_curvemapping_blend_read(reader, &sce->r.mblur_shutter_curve);

#ifdef USE_COLLECTION_COMPAT_28
  /* this runs before the very first doversion */
  if (sce->collection) {
    BLO_read_data_address(reader, &sce->collection);
    BKE_collection_compat_blend_read_data(reader, sce->collection);
  }
#endif

  /* insert into global old-new map for reading without UI (link_global accesses it again) */
  BLO_read_glob_list(reader, &sce->view_layers);
  LISTBASE_FOREACH (ViewLayer *, view_layer, &sce->view_layers) {
    BKE_view_layer_blend_read_data(reader, view_layer);
  }

  if (BLO_read_data_is_undo(reader)) {
    /* If it's undo do nothing here, caches are handled by higher-level generic calling code. */
  }
  else {
    /* else try to read the cache from file. */
    BLO_read_data_address(reader, &sce->eevee.light_cache_data);
    if (sce->eevee.light_cache_data) {
      EEVEE_lightcache_blend_read_data(reader, sce->eevee.light_cache_data);
    }
  }
  EEVEE_lightcache_info_update(&sce->eevee);

  BKE_screen_view3d_shading_blend_read_data(reader, &sce->display.shading);

  BLO_read_data_address(reader, &sce->layer_properties);
  IDP_BlendDataRead(reader, &sce->layer_properties);
}

/* patch for missing scene IDs, can't be in do-versions */
static void composite_patch(bNodeTree *ntree, Scene *scene)
{
  LISTBASE_FOREACH (bNode *, node, &ntree->nodes) {
    if (node->id == NULL &&
        ((node->type == CMP_NODE_R_LAYERS) ||
         (node->type == CMP_NODE_CRYPTOMATTE && node->custom1 == CMP_CRYPTOMATTE_SRC_RENDER))) {
      node->id = &scene->id;
    }
  }
}

static void scene_blend_read_lib(BlendLibReader *reader, ID *id)
{
  Scene *sce = (Scene *)id;

  BKE_keyingsets_blend_read_lib(reader, &sce->id, &sce->keyingsets);

  BLO_read_id_address(reader, sce->id.lib, &sce->camera);
  BLO_read_id_address(reader, sce->id.lib, &sce->world);
  BLO_read_id_address(reader, sce->id.lib, &sce->set);
  BLO_read_id_address(reader, sce->id.lib, &sce->gpd);

  BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->imapaint.paint);
  if (sce->toolsettings->sculpt) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->sculpt->paint);
  }
  if (sce->toolsettings->vpaint) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->vpaint->paint);
  }
  if (sce->toolsettings->wpaint) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->wpaint->paint);
  }
  if (sce->toolsettings->uvsculpt) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->uvsculpt->paint);
  }
  if (sce->toolsettings->gp_paint) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->gp_paint->paint);
  }
  if (sce->toolsettings->gp_vertexpaint) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->gp_vertexpaint->paint);
  }
  if (sce->toolsettings->gp_sculptpaint) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->gp_sculptpaint->paint);
  }
  if (sce->toolsettings->gp_weightpaint) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->gp_weightpaint->paint);
  }
  if (sce->toolsettings->curves_sculpt) {
    BKE_paint_blend_read_lib(reader, sce, &sce->toolsettings->curves_sculpt->paint);
  }

  if (sce->toolsettings->sculpt) {
    BLO_read_id_address(reader, sce->id.lib, &sce->toolsettings->sculpt->gravity_object);
  }

  if (sce->toolsettings->imapaint.stencil) {
    BLO_read_id_address(reader, sce->id.lib, &sce->toolsettings->imapaint.stencil);
  }

  if (sce->toolsettings->imapaint.clone) {
    BLO_read_id_address(reader, sce->id.lib, &sce->toolsettings->imapaint.clone);
  }

  if (sce->toolsettings->imapaint.canvas) {
    BLO_read_id_address(reader, sce->id.lib, &sce->toolsettings->imapaint.canvas);
  }

  BLO_read_id_address(reader, sce->id.lib, &sce->toolsettings->particle.shape_object);

  BLO_read_id_address(reader, sce->id.lib, &sce->toolsettings->gp_sculpt.guide.reference_object);

  LISTBASE_FOREACH_MUTABLE (Base *, base_legacy, &sce->base) {
    BLO_read_id_address(reader, sce->id.lib, &base_legacy->object);

    if (base_legacy->object == NULL) {
      BLO_reportf_wrap(BLO_read_lib_reports(reader),
                       RPT_WARNING,
                       TIP_("LIB: object lost from scene: '%s'"),
                       sce->id.name + 2);
      BLI_remlink(&sce->base, base_legacy);
      if (base_legacy == sce->basact) {
        sce->basact = NULL;
      }
      MEM_freeN(base_legacy);
    }
  }

  if (sce->ed) {
    SEQ_blend_read_lib(reader, sce, &sce->ed->seqbase);
  }

  LISTBASE_FOREACH (TimeMarker *, marker, &sce->markers) {
    IDP_BlendReadLib(reader, marker->prop);

    if (marker->camera) {
      BLO_read_id_address(reader, sce->id.lib, &marker->camera);
    }
  }

  /* rigidbody world relies on its linked collections */
  if (sce->rigidbody_world) {
    RigidBodyWorld *rbw = sce->rigidbody_world;
    if (rbw->group) {
      BLO_read_id_address(reader, sce->id.lib, &rbw->group);
    }
    if (rbw->constraints) {
      BLO_read_id_address(reader, sce->id.lib, &rbw->constraints);
    }
    if (rbw->effector_weights) {
      BLO_read_id_address(reader, sce->id.lib, &rbw->effector_weights->group);
    }
  }

  if (sce->nodetree) {
    composite_patch(sce->nodetree, sce);
  }

  LISTBASE_FOREACH (SceneRenderLayer *, srl, &sce->r.layers) {
    BLO_read_id_address(reader, sce->id.lib, &srl->mat_override);
    LISTBASE_FOREACH (FreestyleModuleConfig *, fmc, &srl->freestyleConfig.modules) {
      BLO_read_id_address(reader, sce->id.lib, &fmc->script);
    }
    LISTBASE_FOREACH (FreestyleLineSet *, fls, &srl->freestyleConfig.linesets) {
      BLO_read_id_address(reader, sce->id.lib, &fls->linestyle);
      BLO_read_id_address(reader, sce->id.lib, &fls->group);
    }
  }
  /* Motion Tracking */
  BLO_read_id_address(reader, sce->id.lib, &sce->clip);

#ifdef USE_COLLECTION_COMPAT_28
  if (sce->collection) {
    BKE_collection_compat_blend_read_lib(reader, sce->id.lib, sce->collection);
  }
#endif

  LISTBASE_FOREACH (ViewLayer *, view_layer, &sce->view_layers) {
    BKE_view_layer_blend_read_lib(reader, sce->id.lib, view_layer);
  }

  if (sce->r.bake.cage_object) {
    BLO_read_id_address(reader, sce->id.lib, &sce->r.bake.cage_object);
  }

#ifdef USE_SETSCENE_CHECK
  if (sce->set != NULL) {
    sce->flag |= SCE_READFILE_LIBLINK_NEED_SETSCENE_CHECK;
  }
#endif
}

static void scene_blend_read_expand(BlendExpander *expander, ID *id)
{
  Scene *sce = (Scene *)id;

  LISTBASE_FOREACH (Base *, base_legacy, &sce->base) {
    BLO_expand(expander, base_legacy->object);
  }
  BLO_expand(expander, sce->camera);
  BLO_expand(expander, sce->world);

  BKE_keyingsets_blend_read_expand(expander, &sce->keyingsets);

  if (sce->set) {
    BLO_expand(expander, sce->set);
  }

  LISTBASE_FOREACH (SceneRenderLayer *, srl, &sce->r.layers) {
    BLO_expand(expander, srl->mat_override);
    LISTBASE_FOREACH (FreestyleModuleConfig *, module, &srl->freestyleConfig.modules) {
      if (module->script) {
        BLO_expand(expander, module->script);
      }
    }
    LISTBASE_FOREACH (FreestyleLineSet *, lineset, &srl->freestyleConfig.linesets) {
      if (lineset->group) {
        BLO_expand(expander, lineset->group);
      }
      BLO_expand(expander, lineset->linestyle);
    }
  }

  LISTBASE_FOREACH (ViewLayer *, view_layer, &sce->view_layers) {
    IDP_BlendReadExpand(expander, view_layer->id_properties);

    LISTBASE_FOREACH (FreestyleModuleConfig *, module, &view_layer->freestyle_config.modules) {
      if (module->script) {
        BLO_expand(expander, module->script);
      }
    }

    LISTBASE_FOREACH (FreestyleLineSet *, lineset, &view_layer->freestyle_config.linesets) {
      if (lineset->group) {
        BLO_expand(expander, lineset->group);
      }
      BLO_expand(expander, lineset->linestyle);
    }
  }

  if (sce->gpd) {
    BLO_expand(expander, sce->gpd);
  }

  if (sce->ed) {
    SEQ_blend_read_expand(expander, &sce->ed->seqbase);
  }

  if (sce->rigidbody_world) {
    BLO_expand(expander, sce->rigidbody_world->group);
    BLO_expand(expander, sce->rigidbody_world->constraints);
  }

  LISTBASE_FOREACH (TimeMarker *, marker, &sce->markers) {
    IDP_BlendReadExpand(expander, marker->prop);

    if (marker->camera) {
      BLO_expand(expander, marker->camera);
    }
  }

  BLO_expand(expander, sce->clip);

#ifdef USE_COLLECTION_COMPAT_28
  if (sce->collection) {
    BKE_collection_compat_blend_read_expand(expander, sce->collection);
  }
#endif

  if (sce->r.bake.cage_object) {
    BLO_expand(expander, sce->r.bake.cage_object);
  }
}

static void scene_undo_preserve(BlendLibReader *reader, ID *id_new, ID *id_old)
{
  Scene *scene_new = (Scene *)id_new;
  Scene *scene_old = (Scene *)id_old;

  SWAP(View3DCursor, scene_old->cursor, scene_new->cursor);
  if (scene_new->toolsettings != NULL && scene_old->toolsettings != NULL) {
    /* First try to restore ID pointers that can be and should be preserved (like brushes or
     * palettes), and counteract the swap of the whole ToolSettings structs below for the others
     * (like object ones). */
    scene_foreach_toolsettings(
        NULL, scene_new->toolsettings, true, reader, scene_old->toolsettings);
    SWAP(ToolSettings, *scene_old->toolsettings, *scene_new->toolsettings);
  }
}

static void scene_lib_override_apply_post(ID *id_dst, ID *UNUSED(id_src))
{
  Scene *scene = (Scene *)id_dst;

  if (scene->rigidbody_world != NULL) {
    PTCacheID pid;
    BKE_ptcache_id_from_rigidbody(&pid, NULL, scene->rigidbody_world);
    LISTBASE_FOREACH (PointCache *, point_cache, pid.ptcaches) {
      point_cache->flag |= PTCACHE_FLAG_INFO_DIRTY;
    }
  }
}

IDTypeInfo IDType_ID_SCE = {
    .id_code = ID_SCE,
    .id_filter = FILTER_ID_SCE,
    .main_listbase_index = INDEX_ID_SCE,
    .struct_size = sizeof(Scene),
    .name = "Scene",
    .name_plural = "scenes",
    .translation_context = BLT_I18NCONTEXT_ID_SCENE,
    .flags = 0,
    .asset_type_info = NULL,

    .init_data = scene_init_data,
    .copy_data = scene_copy_data,
    .free_data = scene_free_data,
    /* For now default `BKE_lib_id_make_local_generic()` should work, may need more work though to
     * support all possible corner cases. */
    .make_local = NULL,
    .foreach_id = scene_foreach_id,
    .foreach_cache = scene_foreach_cache,
    .foreach_path = scene_foreach_path,
    .owner_get = NULL,

    .blend_write = scene_blend_write,
    .blend_read_data = scene_blend_read_data,
    .blend_read_lib = scene_blend_read_lib,
    .blend_read_expand = scene_blend_read_expand,

    .blend_read_undo_preserve = scene_undo_preserve,

    .lib_override_apply_post = scene_lib_override_apply_post,
};

const char *RE_engine_id_BLENDER_EEVEE = "BLENDER_EEVEE";
const char *RE_engine_id_BLENDER_WORKBENCH = "BLENDER_WORKBENCH";
const char *RE_engine_id_CYCLES = "CYCLES";

void free_avicodecdata(AviCodecData *acd)
{
  if (acd) {
    if (acd->lpFormat) {
      MEM_freeN(acd->lpFormat);
      acd->lpFormat = NULL;
      acd->cbFormat = 0;
    }
    if (acd->lpParms) {
      MEM_freeN(acd->lpParms);
      acd->lpParms = NULL;
      acd->cbParms = 0;
    }
  }
}

static void remove_sequencer_fcurves(Scene *sce)
{
  AnimData *adt = BKE_animdata_from_id(&sce->id);

  if (adt && adt->action) {
    FCurve *fcu, *nextfcu;

    for (fcu = adt->action->curves.first; fcu; fcu = nextfcu) {
      nextfcu = fcu->next;

      if ((fcu->rna_path) && strstr(fcu->rna_path, "sequences_all")) {
        action_groups_remove_channel(adt->action, fcu);
        BKE_fcurve_free(fcu);
      }
    }
  }
}

ToolSettings *BKE_toolsettings_copy(ToolSettings *toolsettings, const int flag)
{
  if (toolsettings == NULL) {
    return NULL;
  }
  ToolSettings *ts = MEM_dupallocN(toolsettings);
  if (ts->vpaint) {
    ts->vpaint = MEM_dupallocN(ts->vpaint);
    BKE_paint_copy(&ts->vpaint->paint, &ts->vpaint->paint, flag);
  }
  if (ts->wpaint) {
    ts->wpaint = MEM_dupallocN(ts->wpaint);
    BKE_paint_copy(&ts->wpaint->paint, &ts->wpaint->paint, flag);
  }
  if (ts->sculpt) {
    ts->sculpt = MEM_dupallocN(ts->sculpt);
    BKE_paint_copy(&ts->sculpt->paint, &ts->sculpt->paint, flag);
  }
  if (ts->uvsculpt) {
    ts->uvsculpt = MEM_dupallocN(ts->uvsculpt);
    BKE_paint_copy(&ts->uvsculpt->paint, &ts->uvsculpt->paint, flag);
  }
  if (ts->gp_paint) {
    ts->gp_paint = MEM_dupallocN(ts->gp_paint);
    BKE_paint_copy(&ts->gp_paint->paint, &ts->gp_paint->paint, flag);
  }
  if (ts->gp_vertexpaint) {
    ts->gp_vertexpaint = MEM_dupallocN(ts->gp_vertexpaint);
    BKE_paint_copy(&ts->gp_vertexpaint->paint, &ts->gp_vertexpaint->paint, flag);
  }
  if (ts->gp_sculptpaint) {
    ts->gp_sculptpaint = MEM_dupallocN(ts->gp_sculptpaint);
    BKE_paint_copy(&ts->gp_sculptpaint->paint, &ts->gp_sculptpaint->paint, flag);
  }
  if (ts->gp_weightpaint) {
    ts->gp_weightpaint = MEM_dupallocN(ts->gp_weightpaint);
    BKE_paint_copy(&ts->gp_weightpaint->paint, &ts->gp_weightpaint->paint, flag);
  }
  if (ts->curves_sculpt) {
    ts->curves_sculpt = MEM_dupallocN(ts->curves_sculpt);
    BKE_paint_copy(&ts->curves_sculpt->paint, &ts->curves_sculpt->paint, flag);
  }

  BKE_paint_copy(&ts->imapaint.paint, &ts->imapaint.paint, flag);
  ts->particle.paintcursor = NULL;
  ts->particle.scene = NULL;
  ts->particle.object = NULL;

  /* duplicate Grease Pencil interpolation curve */
  ts->gp_interpolate.custom_ipo = BKE_curvemapping_copy(ts->gp_interpolate.custom_ipo);
  /* Duplicate Grease Pencil multiframe falloff. */
  ts->gp_sculpt.cur_falloff = BKE_curvemapping_copy(ts->gp_sculpt.cur_falloff);
  ts->gp_sculpt.cur_primitive = BKE_curvemapping_copy(ts->gp_sculpt.cur_primitive);

  ts->custom_bevel_profile_preset = BKE_curveprofile_copy(ts->custom_bevel_profile_preset);

  ts->sequencer_tool_settings = SEQ_tool_settings_copy(ts->sequencer_tool_settings);
  return ts;
}

void BKE_toolsettings_free(ToolSettings *toolsettings)
{
  if (toolsettings == NULL) {
    return;
  }
  if (toolsettings->vpaint) {
    BKE_paint_free(&toolsettings->vpaint->paint);
    MEM_freeN(toolsettings->vpaint);
  }
  if (toolsettings->wpaint) {
    BKE_paint_free(&toolsettings->wpaint->paint);
    MEM_freeN(toolsettings->wpaint);
  }
  if (toolsettings->sculpt) {
    BKE_paint_free(&toolsettings->sculpt->paint);
    MEM_freeN(toolsettings->sculpt);
  }
  if (toolsettings->uvsculpt) {
    BKE_paint_free(&toolsettings->uvsculpt->paint);
    MEM_freeN(toolsettings->uvsculpt);
  }
  if (toolsettings->gp_paint) {
    BKE_paint_free(&toolsettings->gp_paint->paint);
    MEM_freeN(toolsettings->gp_paint);
  }
  if (toolsettings->gp_vertexpaint) {
    BKE_paint_free(&toolsettings->gp_vertexpaint->paint);
    MEM_freeN(toolsettings->gp_vertexpaint);
  }
  if (toolsettings->gp_sculptpaint) {
    BKE_paint_free(&toolsettings->gp_sculptpaint->paint);
    MEM_freeN(toolsettings->gp_sculptpaint);
  }
  if (toolsettings->gp_weightpaint) {
    BKE_paint_free(&toolsettings->gp_weightpaint->paint);
    MEM_freeN(toolsettings->gp_weightpaint);
  }
  if (toolsettings->curves_sculpt) {
    BKE_paint_free(&toolsettings->curves_sculpt->paint);
    MEM_freeN(toolsettings->curves_sculpt);
  }
  BKE_paint_free(&toolsettings->imapaint.paint);

  /* free Grease Pencil interpolation curve */
  if (toolsettings->gp_interpolate.custom_ipo) {
    BKE_curvemapping_free(toolsettings->gp_interpolate.custom_ipo);
  }
  /* free Grease Pencil multiframe falloff curve */
  if (toolsettings->gp_sculpt.cur_falloff) {
    BKE_curvemapping_free(toolsettings->gp_sculpt.cur_falloff);
  }
  if (toolsettings->gp_sculpt.cur_primitive) {
    BKE_curvemapping_free(toolsettings->gp_sculpt.cur_primitive);
  }

  if (toolsettings->custom_bevel_profile_preset) {
    BKE_curveprofile_free(toolsettings->custom_bevel_profile_preset);
  }

  if (toolsettings->sequencer_tool_settings) {
    SEQ_tool_settings_free(toolsettings->sequencer_tool_settings);
  }

  MEM_freeN(toolsettings);
}

void BKE_scene_copy_data_eevee(Scene *sce_dst, const Scene *sce_src)
{
  /* Copy eevee data between scenes. */
  sce_dst->eevee = sce_src->eevee;
  sce_dst->eevee.light_cache_data = NULL;
  sce_dst->eevee.light_cache_info[0] = '\0';
  /* TODO: Copy the cache. */
}

Scene *BKE_scene_duplicate(Main *bmain, Scene *sce, eSceneCopyMethod type)
{
  Scene *sce_copy;

  /* TODO: this should/could most likely be replaced by call to more generic code at some point...
   * But for now, let's keep it well isolated here. */
  if (type == SCE_COPY_EMPTY) {
    ListBase rv;

    sce_copy = BKE_scene_add(bmain, sce->id.name + 2);

    rv = sce_copy->r.views;
    BKE_curvemapping_free_data(&sce_copy->r.mblur_shutter_curve);
    sce_copy->r = sce->r;
    sce_copy->r.views = rv;
    sce_copy->unit = sce->unit;
    sce_copy->physics_settings = sce->physics_settings;
    sce_copy->audio = sce->audio;
    BKE_scene_copy_data_eevee(sce_copy, sce);

    if (sce->id.properties) {
      sce_copy->id.properties = IDP_CopyProperty(sce->id.properties);
    }

    BKE_sound_destroy_scene(sce_copy);

    /* copy color management settings */
    BKE_color_managed_display_settings_copy(&sce_copy->display_settings, &sce->display_settings);
    BKE_color_managed_view_settings_copy(&sce_copy->view_settings, &sce->view_settings);
    BKE_color_managed_colorspace_settings_copy(&sce_copy->sequencer_colorspace_settings,
                                               &sce->sequencer_colorspace_settings);

    BKE_image_format_copy(&sce_copy->r.im_format, &sce->r.im_format);
    BKE_image_format_copy(&sce_copy->r.bake.im_format, &sce->r.bake.im_format);

    BKE_curvemapping_copy_data(&sce_copy->r.mblur_shutter_curve, &sce->r.mblur_shutter_curve);

    /* viewport display settings */
    sce_copy->display = sce->display;

    /* tool settings */
    BKE_toolsettings_free(sce_copy->toolsettings);
    sce_copy->toolsettings = BKE_toolsettings_copy(sce->toolsettings, 0);

    /* make a private copy of the avicodecdata */
    if (sce->r.avicodecdata) {
      sce_copy->r.avicodecdata = MEM_dupallocN(sce->r.avicodecdata);
      sce_copy->r.avicodecdata->lpFormat = MEM_dupallocN(sce_copy->r.avicodecdata->lpFormat);
      sce_copy->r.avicodecdata->lpParms = MEM_dupallocN(sce_copy->r.avicodecdata->lpParms);
    }

    BKE_sound_reset_scene_runtime(sce_copy);

    /* grease pencil */
    sce_copy->gpd = NULL;

    sce_copy->preview = NULL;

    return sce_copy;
  }

  eDupli_ID_Flags duplicate_flags = U.dupflag | USER_DUP_OBJECT;

  sce_copy = (Scene *)BKE_id_copy(bmain, (ID *)sce);
  id_us_min(&sce_copy->id);
  id_us_ensure_real(&sce_copy->id);

  BKE_animdata_duplicate_id_action(bmain, &sce_copy->id, duplicate_flags);

  /* Extra actions, most notably SCE_FULL_COPY also duplicates several 'children' datablocks. */

  if (type == SCE_COPY_FULL) {
    /* Scene duplication is always root of duplication currently. */
    const bool is_subprocess = false;
    const bool is_root_id = true;
    const int copy_flags = LIB_ID_COPY_DEFAULT;

    if (!is_subprocess) {
      BKE_main_id_newptr_and_tag_clear(bmain);
    }
    if (is_root_id) {
      /* In case root duplicated ID is linked, assume we want to get a local copy of it and
       * duplicate all expected linked data. */
      if (ID_IS_LINKED(sce)) {
        duplicate_flags |= USER_DUP_LINKED_ID;
      }
    }

    /* Copy Freestyle LineStyle datablocks. */
    LISTBASE_FOREACH (ViewLayer *, view_layer_dst, &sce_copy->view_layers) {
      LISTBASE_FOREACH (FreestyleLineSet *, lineset, &view_layer_dst->freestyle_config.linesets) {
        BKE_id_copy_for_duplicate(bmain, (ID *)lineset->linestyle, duplicate_flags, copy_flags);
      }
    }

    /* Full copy of world (included animations) */
    BKE_id_copy_for_duplicate(bmain, (ID *)sce->world, duplicate_flags, copy_flags);

    /* Full copy of GreasePencil. */
    BKE_id_copy_for_duplicate(bmain, (ID *)sce->gpd, duplicate_flags, copy_flags);

    /* Deep-duplicate collections and objects (using preferences' settings for which sub-data to
     * duplicate along the object itself). */
    BKE_collection_duplicate(
        bmain, NULL, sce_copy->master_collection, duplicate_flags, LIB_ID_DUPLICATE_IS_SUBPROCESS);

    /* Rigid body world collections may not be instantiated as scene's collections, ensure they
     * also get properly duplicated. */
    if (sce_copy->rigidbody_world != NULL) {
      if (sce_copy->rigidbody_world->group != NULL) {
        BKE_collection_duplicate(bmain,
                                 NULL,
                                 sce_copy->rigidbody_world->group,
                                 duplicate_flags,
                                 LIB_ID_DUPLICATE_IS_SUBPROCESS);
      }
      if (sce_copy->rigidbody_world->constraints != NULL) {
        BKE_collection_duplicate(bmain,
                                 NULL,
                                 sce_copy->rigidbody_world->constraints,
                                 duplicate_flags,
                                 LIB_ID_DUPLICATE_IS_SUBPROCESS);
      }
    }

    if (!is_subprocess) {
      /* This code will follow into all ID links using an ID tagged with LIB_TAG_NEW. */
      BKE_libblock_relink_to_newid(bmain, &sce_copy->id, 0);

#ifndef NDEBUG
      /* Call to `BKE_libblock_relink_to_newid` above is supposed to have cleared all those
       * flags. */
      ID *id_iter;
      FOREACH_MAIN_ID_BEGIN (bmain, id_iter) {
        BLI_assert((id_iter->tag & LIB_TAG_NEW) == 0);
      }
      FOREACH_MAIN_ID_END;
#endif

      /* Cleanup. */
      BKE_main_id_newptr_and_tag_clear(bmain);

      BKE_main_collection_sync(bmain);
    }
  }
  else {
    /* Remove sequencer if not full copy */
    /* XXX Why in Hell? :/ */
    remove_sequencer_fcurves(sce_copy);
    SEQ_editing_free(sce_copy, true);
  }

  return sce_copy;
}

void BKE_scene_groups_relink(Scene *sce)
{
  if (sce->rigidbody_world) {
    BKE_rigidbody_world_groups_relink(sce->rigidbody_world);
  }
}

bool BKE_scene_can_be_removed(const Main *bmain, const Scene *scene)
{
  /* Linked scenes can always be removed. */
  if (ID_IS_LINKED(scene)) {
    return true;
  }
  /* Local scenes can only be removed, when there is at least one local scene left. */
  LISTBASE_FOREACH (Scene *, other_scene, &bmain->scenes) {
    if (other_scene != scene && !ID_IS_LINKED(other_scene)) {
      return true;
    }
  }
  return false;
}

Scene *BKE_scene_add(Main *bmain, const char *name)
{
  Scene *sce;

  sce = BKE_id_new(bmain, ID_SCE, name);
  id_us_min(&sce->id);
  id_us_ensure_real(&sce->id);

  return sce;
}

bool BKE_scene_object_find(Scene *scene, Object *ob)
{
  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
    if (BLI_findptr(&view_layer->object_bases, ob, offsetof(Base, object))) {
      return true;
    }
  }
  return false;
}

Object *BKE_scene_object_find_by_name(const Scene *scene, const char *name)
{
  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
    LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
      if (STREQ(base->object->id.name + 2, name)) {
        return base->object;
      }
    }
  }
  return NULL;
}

void BKE_scene_set_background(Main *bmain, Scene *scene)
{
  Object *ob;

  /* check for cyclic sets, for reading old files but also for definite security (py?) */
  BKE_scene_validate_setscene(bmain, scene);

  /* deselect objects (for dataselect) */
  for (ob = bmain->objects.first; ob; ob = ob->id.next) {
    ob->flag &= ~SELECT;
  }

  /* copy layers and flags from bases to objects */
  LISTBASE_FOREACH (ViewLayer *, view_layer, &scene->view_layers) {
    LISTBASE_FOREACH (Base *, base, &view_layer->object_bases) {
      ob = base->object;
      /* collection patch... */
      BKE_scene_object_base_flag_sync_from_base(base);
    }
  }
  /* No full animation update, this to enable render code to work
   * (render code calls own animation updates). */
}

Scene *BKE_scene_set_name(Main *bmain, const char *name)
{
  Scene *sce = (Scene *)BKE_libblock_find_name(bmain, ID_SCE, name);
  if (sce) {
    BKE_scene_set_background(bmain, sce);
    printf("Scene switch for render: '%s' in file: '%s'\n", name, BKE_main_blendfile_path(bmain));
    return sce;
  }

  printf("Can't find scene: '%s' in file: '%s'\n", name, BKE_main_blendfile_path(bmain));
  return NULL;
}

int BKE_scene_base_iter_next(
    Depsgraph *depsgraph, SceneBaseIter *iter, Scene **scene, int val, Base **base, Object **ob)
{
  bool run_again = true;

  /* init */
  if (val == 0) {
    iter->phase = F_START;
    iter->dupob = NULL;
    iter->duplilist = NULL;
    iter->dupli_refob = NULL;
  }
  else {
    /* run_again is set when a duplilist has been ended */
    while (run_again) {
      run_again = false;

      /* the first base */
      if (iter->phase == F_START) {
        ViewLayer *view_layer = (depsgraph) ? DEG_get_evaluated_view_layer(depsgraph) :
                                              BKE_view_layer_context_active_PLACEHOLDER(*scene);
        *base = view_layer->object_bases.first;
        if (*base) {
          *ob = (*base)->object;
          iter->phase = F_SCENE;
        }
        else {
          /* exception: empty scene layer */
          while ((*scene)->set) {
            (*scene) = (*scene)->set;
            ViewLayer *view_layer_set = BKE_view_layer_default_render(*scene);
            if (view_layer_set->object_bases.first) {
              *base = view_layer_set->object_bases.first;
              *ob = (*base)->object;
              iter->phase = F_SCENE;
              break;
            }
          }
        }
      }
      else {
        if (*base && iter->phase != F_DUPLI) {
          *base = (*base)->next;
          if (*base) {
            *ob = (*base)->object;
          }
          else {
            if (iter->phase == F_SCENE) {
              /* (*scene) is finished, now do the set */
              while ((*scene)->set) {
                (*scene) = (*scene)->set;
                ViewLayer *view_layer_set = BKE_view_layer_default_render(*scene);
                if (view_layer_set->object_bases.first) {
                  *base = view_layer_set->object_bases.first;
                  *ob = (*base)->object;
                  break;
                }
              }
            }
          }
        }
      }

      if (*base == NULL) {
        iter->phase = F_START;
      }
      else {
        if (iter->phase != F_DUPLI) {
          if (depsgraph && (*base)->object->transflag & OB_DUPLI) {
            /* Collections cannot be duplicated for meta-balls yet,
             * this enters eternal loop because of
             * makeDispListMBall getting called inside of collection_duplilist */
            if ((*base)->object->instance_collection == NULL) {
              iter->duplilist = object_duplilist(depsgraph, (*scene), (*base)->object);

              iter->dupob = iter->duplilist->first;

              if (!iter->dupob) {
                free_object_duplilist(iter->duplilist);
                iter->duplilist = NULL;
              }
              iter->dupli_refob = NULL;
            }
          }
        }
        /* handle dupli's */
        if (iter->dupob) {
          (*base)->flag_legacy |= OB_FROMDUPLI;
          *ob = iter->dupob->ob;
          iter->phase = F_DUPLI;

          if (iter->dupli_refob != *ob) {
            if (iter->dupli_refob) {
              /* Restore previous object's real matrix. */
              copy_m4_m4(iter->dupli_refob->obmat, iter->omat);
            }
            /* Backup new object's real matrix. */
            iter->dupli_refob = *ob;
            copy_m4_m4(iter->omat, iter->dupli_refob->obmat);
          }
          copy_m4_m4((*ob)->obmat, iter->dupob->mat);

          iter->dupob = iter->dupob->next;
        }
        else if (iter->phase == F_DUPLI) {
          iter->phase = F_SCENE;
          (*base)->flag_legacy &= ~OB_FROMDUPLI;

          if (iter->dupli_refob) {
            /* Restore last object's real matrix. */
            copy_m4_m4(iter->dupli_refob->obmat, iter->omat);
            iter->dupli_refob = NULL;
          }

          free_object_duplilist(iter->duplilist);
          iter->duplilist = NULL;
          run_again = true;
        }
      }
    }
  }

  return iter->phase;
}

bool BKE_scene_has_view_layer(const Scene *scene, const ViewLayer *layer)
{
  return BLI_findindex(&scene->view_layers, layer) != -1;
}

Scene *BKE_scene_find_from_collection(const Main *bmain, const Collection *collection)
{
  for (Scene *scene = bmain->scenes.first; scene; scene = scene->id.next) {
    LISTBASE_FOREACH (ViewLayer *, layer, &scene->view_layers) {
      if (BKE_view_layer_has_collection(layer, collection)) {
        return scene;
      }
    }
  }

  return NULL;
}

#ifdef DURIAN_CAMERA_SWITCH
Object *BKE_scene_camera_switch_find(Scene *scene)
{
  if (scene->r.mode & R_NO_CAMERA_SWITCH) {
    return NULL;
  }

  const int ctime = (int)BKE_scene_ctime_get(scene);
  int frame = -(MAXFRAME + 1);
  int min_frame = MAXFRAME + 1;
  Object *camera = NULL;
  Object *first_camera = NULL;

  LISTBASE_FOREACH (TimeMarker *, m, &scene->markers) {
    if (m->camera && (m->camera->visibility_flag & OB_HIDE_RENDER) == 0) {
      if ((m->frame <= ctime) && (m->frame > frame)) {
        camera = m->camera;
        frame = m->frame;

        if (frame == ctime) {
          break;
        }
      }

      if (m->frame < min_frame) {
        first_camera = m->camera;
        min_frame = m->frame;
      }
    }
  }

  if (camera == NULL) {
    /* If there's no marker to the left of current frame,
     * use camera from left-most marker to solve all sort
     * of Schrodinger uncertainties.
     */
    return first_camera;
  }

  return camera;
}
#endif

bool BKE_scene_camera_switch_update(Scene *scene)
{
#ifdef DURIAN_CAMERA_SWITCH
  Object *camera = BKE_scene_camera_switch_find(scene);
  if (camera && (camera != scene->camera)) {
    scene->camera = camera;
    DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);
    return true;
  }
#else
  (void)scene;
#endif
  return false;
}

const char *BKE_scene_find_marker_name(const Scene *scene, int frame)
{
  const ListBase *markers = &scene->markers;
  const TimeMarker *m1, *m2;

  /* search through markers for match */
  for (m1 = markers->first, m2 = markers->last; m1 && m2; m1 = m1->next, m2 = m2->prev) {
    if (m1->frame == frame) {
      return m1->name;
    }

    if (m1 == m2) {
      break;
    }

    if (m2->frame == frame) {
      return m2->name;
    }
  }

  return NULL;
}

const char *BKE_scene_find_last_marker_name(const Scene *scene, int frame)
{
  const TimeMarker *marker, *best_marker = NULL;
  int best_frame = -MAXFRAME * 2;
  for (marker = scene->markers.first; marker; marker = marker->next) {
    if (marker->frame == frame) {
      return marker->name;
    }

    if (marker->frame > best_frame && marker->frame < frame) {
      best_marker = marker;
      best_frame = marker->frame;
    }
  }

  return best_marker ? best_marker->name : NULL;
}

int BKE_scene_frame_snap_by_seconds(Scene *scene, double interval_in_seconds, int frame)
{
  const int fps = round_db_to_int(FPS * interval_in_seconds);
  const int second_prev = frame - mod_i(frame, fps);
  const int second_next = second_prev + fps;
  const int delta_prev = frame - second_prev;
  const int delta_next = second_next - frame;
  return (delta_prev < delta_next) ? second_prev : second_next;
}

void BKE_scene_remove_rigidbody_object(struct Main *bmain,
                                       Scene *scene,
                                       Object *ob,
                                       const bool free_us)
{
  /* remove rigid body constraint from world before removing object */
  if (ob->rigidbody_constraint) {
    BKE_rigidbody_remove_constraint(bmain, scene, ob, free_us);
  }
  /* remove rigid body object from world before removing object */
  if (ob->rigidbody_object) {
    BKE_rigidbody_remove_object(bmain, scene, ob, free_us);
  }
}

bool BKE_scene_validate_setscene(Main *bmain, Scene *sce)
{
  Scene *sce_iter;
  int a, totscene;

  if (sce->set == NULL) {
    return true;
  }
  totscene = BLI_listbase_count(&bmain->scenes);

  for (a = 0, sce_iter = sce; sce_iter->set; sce_iter = sce_iter->set, a++) {
    /* more iterations than scenes means we have a cycle */
    if (a > totscene) {
      /* the tested scene gets zero'ed, that's typically current scene */
      sce->set = NULL;
      return false;
    }
  }

  return true;
}

float BKE_scene_ctime_get(const Scene *scene)
{
  return BKE_scene_frame_to_ctime(scene, scene->r.cfra);
}
