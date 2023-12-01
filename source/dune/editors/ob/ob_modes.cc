/** \file
 * \ingroup edobj
 *
 * General utils to handle mode switching,
 * actual mode switching logic is per-object type.
 */

#include "DNA_gpencil_legacy_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_workspace_types.h"

#include "BLI_kdopbvh.h"
#include "BLI_utildefines.h"

#include "PIL_time.h"

#include "BLT_translation.h"

#include "BKE_context.hh"
#include "BKE_gpencil_modifier_legacy.h"
#include "BKE_layer.h"
#include "BKE_main.h"
#include "BKE_modifier.hh"
#include "BKE_object.hh"
#include "BKE_object_types.hh"
#include "BKE_paint.hh"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.hh"

#include "BLI_math_vector.h"

#include "WM_api.hh"
#include "WM_types.hh"

#include "RNA_access.hh"
#include "RNA_define.hh"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "ED_armature.hh"
#include "ED_gpencil_legacy.hh"
#include "ED_outliner.hh"
#include "ED_screen.hh"
#include "ED_transform_snap_object_context.hh"
#include "ED_undo.hh"
#include "ED_view3d.hh"

#include "WM_toolsystem.h"

#include "ED_object.hh" /* own include */
#include "object_intern.h"

/* -------------------------------------------------------------------- */
/** \name High Level Mode Operations
 * \{ */

static const char *object_mode_op_string(eObjectMode mode)
{
  if (mode & OB_MODE_EDIT) {
    return "OBJECT_OT_editmode_toggle";
  }
  if (mode == OB_MODE_SCULPT) {
    return "SCULPT_OT_sculptmode_toggle";
  }
  if (mode == OB_MODE_VERTEX_PAINT) {
    return "PAINT_OT_vertex_paint_toggle";
  }
  if (mode == OB_MODE_WEIGHT_PAINT) {
    return "PAINT_OT_weight_paint_toggle";
  }
  if (mode == OB_MODE_TEXTURE_PAINT) {
    return "PAINT_OT_texture_paint_toggle";
  }
  if (mode == OB_MODE_PARTICLE_EDIT) {
    return "PARTICLE_OT_particle_edit_toggle";
  }
  if (mode == OB_MODE_POSE) {
    return "OBJECT_OT_posemode_toggle";
  }
  if (mode == OB_MODE_EDIT_GPENCIL_LEGACY) {
    return "GPENCIL_OT_editmode_toggle";
  }
  if (mode == OB_MODE_PAINT_GREASE_PENCIL) {
    return "GREASE_PENCIL_OT_draw_mode_toggle";
  }
  if (mode == OB_MODE_PAINT_GPENCIL_LEGACY) {
    return "GPENCIL_OT_paintmode_toggle";
  }
  if (mode == OB_MODE_SCULPT_GPENCIL_LEGACY) {
    return "GPENCIL_OT_sculptmode_toggle";
  }
  if (mode == OB_MODE_WEIGHT_GPENCIL_LEGACY) {
    return "GPENCIL_OT_weightmode_toggle";
  }
  if (mode == OB_MODE_VERTEX_GPENCIL_LEGACY) {
    return "GPENCIL_OT_vertexmode_toggle";
  }
  if (mode == OB_MODE_SCULPT_CURVES) {
    return "CURVES_OT_sculptmode_toggle";
  }
  return nullptr;
}

bool ED_object_mode_compat_test(const Object *ob, eObjectMode mode)
{
  if (mode == OB_MODE_OBJECT) {
    return true;
  }

  switch (ob->type) {
    case OB_MESH:
      if (mode & (OB_MODE_EDIT | OB_MODE_SCULPT | OB_MODE_VERTEX_PAINT | OB_MODE_WEIGHT_PAINT |
                  OB_MODE_TEXTURE_PAINT))
      {
        return true;
      }
      if (mode & OB_MODE_PARTICLE_EDIT) {
        if (ED_object_particle_edit_mode_supported(ob)) {
          return true;
        }
      }
      break;
    case OB_CURVES_LEGACY:
    case OB_SURF:
    case OB_FONT:
    case OB_MBALL:
    case OB_POINTCLOUD:
    case OB_LATTICE:
      if (mode & OB_MODE_EDIT) {
        return true;
      }
      break;
    case OB_ARMATURE:
      if (mode & (OB_MODE_EDIT | OB_MODE_POSE)) {
        return true;
      }
      break;
    case OB_GPENCIL_LEGACY:
      if (mode & (OB_MODE_EDIT_GPENCIL_LEGACY | OB_MODE_ALL_PAINT_GPENCIL)) {
        return true;
      }
      break;
    case OB_CURVES:
      if (mode & (OB_MODE_EDIT | OB_MODE_SCULPT_CURVES)) {
        return true;
      }
      break;
    case OB_GREASE_PENCIL:
      if (mode & (OB_MODE_EDIT | OB_MODE_PAINT_GREASE_PENCIL)) {
        return true;
      }
      break;
  }

  return false;
}

bool ED_object_mode_compat_set(bContext *C, Object *ob, eObjectMode mode, ReportList *reports)
{
  bool ok;
  if (!ELEM(ob->mode, mode, OB_MODE_OBJECT)) {
    const char *opstring = object_mode_op_string(eObjectMode(ob->mode));

    WM_operator_name_call(C, opstring, WM_OP_EXEC_REGION_WIN, nullptr, nullptr);
    ok = ELEM(ob->mode, mode, OB_MODE_OBJECT);
    if (!ok) {
      wmOperatorType *ot = WM_operatortype_find(opstring, false);
      BKE_reportf(reports, RPT_ERROR, "Unable to execute '%s', error changing modes", ot->name);
    }
  }
  else {
    ok = true;
  }

  return ok;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Generic Mode Enter/Exit
 *
 * Supports exiting a mode without it being in the current context.
 * This could be done for entering modes too if it's needed.
 *
 * \{ */

bool ED_object_mode_set_ex(bContext *C, eObjectMode mode, bool use_undo, ReportList *reports)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);

  BKE_view_layer_synced_ensure(scene, view_layer);
  Object *ob = BKE_view_layer_active_object_get(view_layer);
  if (ob == nullptr) {
    return (mode == OB_MODE_OBJECT);
  }

  if ((ob->type == OB_GPENCIL_LEGACY) && (mode == OB_MODE_EDIT)) {
    mode = OB_MODE_EDIT_GPENCIL_LEGACY;
  }

  if (ob->mode == mode) {
    return true;
  }

  if (!ED_object_mode_compat_test(ob, mode)) {
    return false;
  }

  const char *opstring = object_mode_op_string((mode == OB_MODE_OBJECT) ? eObjectMode(ob->mode) :
                                                                          mode);
  wmOperatorType *ot = WM_operatortype_find(opstring, false);

  if (!use_undo) {
    wm->op_undo_depth++;
  }
  WM_operator_name_call_ptr(C, ot, WM_OP_EXEC_REGION_WIN, nullptr, nullptr);
  if (!use_undo) {
    wm->op_undo_depth--;
  }

  if (ob->mode != mode) {
    BKE_reportf(reports, RPT_ERROR, "Unable to execute '%s', error changing modes", ot->name);
    return false;
  }

  return true;
}

bool ED_object_mode_set(bContext *C, eObjectMode mode)
{
  /* Don't do undo push by default, since this may be called by lower level code. */
  return ED_object_mode_set_ex(C, mode, true, nullptr);
}

/**
 * Use for changing works-paces or changing active object.
 * Caller can check #OB_MODE_ALL_MODE_DATA to test if this needs to be run.
 */
static bool ed_object_mode_generic_exit_ex(
    Main *bmain, Depsgraph *depsgraph, Scene *scene, Object *ob, bool only_test)
{
  BLI_assert((bmain == nullptr) == only_test);
  if (ob->mode & OB_MODE_EDIT) {
    if (BKE_object_is_in_editmode(ob)) {
      if (only_test) {
        return true;
      }
      ED_object_editmode_exit_ex(bmain, scene, ob, EM_FREEDATA);
    }
  }
  else if (ob->mode & OB_MODE_VERTEX_PAINT) {
    if (ob->sculpt && (ob->sculpt->mode_type == OB_MODE_VERTEX_PAINT)) {
      if (only_test) {
        return true;
      }
      ED_object_vpaintmode_exit_ex(ob);
    }
  }
  else if (ob->mode & OB_MODE_WEIGHT_PAINT) {
    if (ob->sculpt && (ob->sculpt->mode_type == OB_MODE_WEIGHT_PAINT)) {
      if (only_test) {
        return true;
      }
      ED_object_wpaintmode_exit_ex(ob);
    }
  }
  else if (ob->mode & OB_MODE_SCULPT) {
    if (ob->sculpt && (ob->sculpt->mode_type == OB_MODE_SCULPT)) {
      if (only_test) {
        return true;
      }
      ED_object_sculptmode_exit_ex(bmain, depsgraph, scene, ob);
    }
  }
  else if (ob->mode & OB_MODE_POSE) {
    if (ob->pose != nullptr) {
      if (only_test) {
        return true;
      }
      ED_object_posemode_exit_ex(bmain, ob);
    }
  }
  else if (ob->mode & OB_MODE_TEXTURE_PAINT) {
    if (only_test) {
      return true;
    }
    ED_object_texture_paint_mode_exit_ex(bmain, scene, ob);
  }
  else if (ob->mode & OB_MODE_PARTICLE_EDIT) {
    if (only_test) {
      return true;
    }
    ED_object_particle_edit_mode_exit_ex(scene, ob);
  }
  else if (ob->type == OB_GPENCIL_LEGACY) {
    /* Accounted for above. */
    BLI_assert((ob->mode & OB_MODE_OBJECT) == 0);
    if (only_test) {
      return true;
    }
    ED_object_gpencil_exit(bmain, ob);
  }
  else if (ob->mode & OB_MODE_PAINT_GREASE_PENCIL) {
    ob->mode &= ~OB_MODE_PAINT_GREASE_PENCIL;
    DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
    WM_main_add_notifier(NC_SCENE | ND_MODE | NS_MODE_OBJECT, nullptr);
  }
  else {
    if (only_test) {
      return false;
    }
    BLI_assert((ob->mode & OB_MODE_ALL_MODE_DATA) == 0);
  }

  return false;
}

/* When locked, it's almost impossible to select the pose-object
 * then the mesh-object to enter weight paint mode.
 * Even when the object mode is not locked this is inconvenient - so allow in either case.
 *
 * In this case move our pose object in/out of pose mode.
 * This is in fits with the convention of selecting multiple objects and entering a mode.
 */
static void ed_object_posemode_set_for_weight_paint_ex(bContext *C,
                                                       Main *bmain,
                                                       Object *ob_arm,
                                                       const bool is_mode_set)
{
  View3D *v3d = CTX_wm_view3d(C);
  const Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);

  if (ob_arm != nullptr) {
    BKE_view_layer_synced_ensure(scene, view_layer);
    const Base *base_arm = BKE_view_layer_base_find(view_layer, ob_arm);
    if (base_arm && BASE_VISIBLE(v3d, base_arm)) {
      if (is_mode_set) {
        if ((ob_arm->mode & OB_MODE_POSE) != 0) {
          ED_object_posemode_exit_ex(bmain, ob_arm);
        }
      }
      else {
        /* Only check selected status when entering weight-paint mode
         * because we may have multiple armature objects.
         * Selecting one will de-select the other, which would leave it in pose-mode
         * when exiting weight paint mode. While usable, this looks like inconsistent
         * behavior from a user perspective. */
        if (base_arm->flag & BASE_SELECTED) {
          if ((ob_arm->mode & OB_MODE_POSE) == 0) {
            ED_object_posemode_enter_ex(bmain, ob_arm);
          }
        }
      }
    }
  }
}

void ED_object_posemode_set_for_weight_paint(bContext *C,
                                             Main *bmain,
                                             Object *ob,
                                             const bool is_mode_set)
{
  if (ob->type == OB_GPENCIL_LEGACY) {
    GpencilVirtualModifierData virtual_modifier_data;
    GpencilModifierData *md = BKE_gpencil_modifiers_get_virtual_modifierlist(
        ob, &virtual_modifier_data);
    for (; md; md = md->next) {
      if (md->type == eGpencilModifierType_Armature) {
        ArmatureGpencilModifierData *amd = (ArmatureGpencilModifierData *)md;
        Object *ob_arm = amd->object;
        ed_object_posemode_set_for_weight_paint_ex(C, bmain, ob_arm, is_mode_set);
      }
    }
  }
  else {
    VirtualModifierData virtual_modifier_data;
    ModifierData *md = BKE_modifiers_get_virtual_modifierlist(ob, &virtual_modifier_data);
    for (; md; md = md->next) {
      if (md->type == eModifierType_Armature) {
        ArmatureModifierData *amd = (ArmatureModifierData *)md;
        Object *ob_arm = amd->object;
        ed_object_posemode_set_for_weight_paint_ex(C, bmain, ob_arm, is_mode_set);
      }
    }
  }
}

void ed_ob_mode_generic_exit(Main *main, Graph *graph, Scene *scene, Ob *ob)
{
  ed_ob_mode_generic_exit_ex(main, graph, scene, ob, false);
}

bool ed_ob_mode_generic_has_data(Graph *graph, const Ob *ob)
{
  return ed_ob_mode_generic_exit_ex(nullptr, graph, nullptr, (Ob *)ob, true);
}

/* Transfer Mode Enters the same mode
 * of the current active ob in another ob,
 * leaving the mode of the current ob */
static bool ob_transfer_mode_poll(Cxt *C)
{
  if (!cxt_win_rgn_view3d(C)) {
    return false;
  }
  const Ob *ob = cxt_data_active_ob(C);
  return ob && (ob->mode != OB_MODE_OB);
}

/* Update the viewport rotation origin to the mouse cursor. */
static void ob_transfer_mode_reposition_view_pivot(Cxt *C, const int mval[2])
{
  ARgn *rgn = cxt_win_rgn(C);
  Scene *scene = cxt_data_scene(C);

  float global_loc[3];
  if (!ed_view3d_autodist_simple(region, mval, global_loc, 0, nullptr)) {
    return;
  }
  UnifiedPaintSettings *ups = &scene->toolsettings->unified_paint_settings;
  copy_v3_v3(ups->avg_stroke_accum, global_loc);
  ups->avg_stroke_counter = 1;
  ups->last_stroke_valid = true;
}

static void ob_overlay_mode_transfer_anim_start(Cxt *C, Ob *ob_dst)
{
  Graph *graph = cxt_data_graph_ptr(C);
  Ob *ob_dst_eval = graph_get_eval_ob(graph, ob_dst);
  ob_dst_eval->runtime->overlay_mode_transfer_start_time = PIL_check_seconds_timer();
}

static bool ob_transfer_mode_to_base(Cxt *C, WinOp *op, Base *base_dst)
{
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);

  if (base_dst == nullptr) {
    return false;
  }
  
  Ob *ob_dst = base_dst->ob;
  Ob *ob_src = cxt_data_active_ob(C);

  if (ob_dst == ob_src) {
    return false;
  }

  const eObMode last_mode = (eObMode)ob_src->mode;
  if (!ed_ob_mode_compat_test(ob_dst, last_mode)) {
    return false;
  }

  bool mode_transferred = false;

  ed_undo_group_begin(C);

  if (ed_ob_mode_set_ex(C, OB_MODE_OB, true, op->reports)) {
    Ob *ob_dst_orig = graph_get_original_ob(ob_dst);
    dune_view_layer_synced_ensure(scene, view_layer);
    Base *base = dune_view_layer_base_find(view_layer, ob_dst_orig);
    dune_view_layer_base_desel_all(scene, view_layer);
    dune_view_layer_base_sel_and_set_active(view_layer, base);
    graph_id_tag_update(&scene->id, ID_RECALC_SEL);

    ed_undo_push(C, "Change Active");

    ob_dst_orig = get_get_origin_ob(ob_dst);
    ed_ob_mode_set_ex(C, last_mode, true, op->reports);

    if (api_bool_get(op->ptr, "use_flash_on_transfer")) {
      ob_overlay_mode_transfer_anim_start(C, ob_dst);
    }

    win_ev_add_notifier(C, NC_SCENE | ND_OB_SEL, scene);
    ed_outliner_sel_sync_from_ob_tag(C);

    win_toolsystem_update_from_cxt_view3d(C);
    mode_transferred = true;
  }

  ed_undo_group_end(C);
  return mode_transferred;
}

static int ob_transfer_mode_invoke(Cxt *C, WinOp *op, const WinEv *ev)
{
  Ob *ob_src = cxt_data_active_ob(C);
  const eObMode src_mode = (eObMode)ob_src->mode;

  Base *base_dst = ed_view3d_give_base_under_cursor(C, ev->mval);

  if (ID_IS_LINKED(base_dst->ob) || ID_IS_OVERRIDE_LIB(base_dst->ob)) {
    dune_reportf(op->reports,
                RPT_ERROR,
                "Unable to execute, %s ob is linked",
                base_dst->ob->id.name + 2);
    return OP_CANCELLED;
  }
  const bool mode_transferred = ob_transfer_mode_to_base(C, op, base_dst);
  if (!mode_transferred) {
    return OP_CANCELLED;
  }

  if (src_mode & OB_MODE_ALL_PAINT) {
    ob_transfer_mode_reposition_view_pivot(C, ev->mval);
  }

  return OP_FINISHED;
}

void OB_OT_transfer_mode(WinOpType *ot)
{
  /* identifiers */
  ot->name = "Transfer Mode";
  ot->idname = "OB_OT_transfer_mode";
  ot->description =
      "Switches the active ob and assigns the same mode to a new one under the mouse cursor, "
      "leaving the active mode in the current one";

  /* api cbs */
  ot->invoke = o_transfer_mode_invoke;
  ot->poll = ob_transfer_mode_poll;

  /* Undo push is handled by the op. */
  ot->flag = OPTYPE_REGISTER | OPTYPE_DEPENDS_ON_CURSOR;

  ot->cursor_pending = WIN_CURSOR_EYEDROPPER;

  api_def_bool(ot->sapi,
                  "use_flash_on_transfer",
                  true,
                  "Flash On Transfer",
                  "Flash the target ob when transferring the mode");
}

/** \} */
