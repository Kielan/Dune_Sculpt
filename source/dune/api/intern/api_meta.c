#include <stdlib.h>

#include "types_mesh.h"
#include "types_meta.h"

#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"
#include "apo_enum_types.h"

#include "api_internal.h"

#ifdef API_RUNTIME

#  include "lib_math.h"

#  include "mem_guardedalloc.h"

#  include "types_object.h"
#  include "types_scene.h"

#  include "dune_main.h"
#  include "dune_mball.h"
#  include "dune_scene.h"

#  include "graph.h"

#  include "wm_api.h"
#  include "wm_types.h"

static int api_Meta_texspace_editable(ApiPtr *ptr, const char **UNUSED(r_info))
{
  MetaBall *mb = (MetaBall *)ptr->data;
  return (mb->texflag & MB_AUTOSPACE) ? 0 : PROP_EDITABLE;
}

static void api_Meta_texspace_loc_get(ApiPtr *ptr, float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  /* tex_space_mball() needs object.. ugh */

  copy_v3_v3(values, mb->loc);
}

static void api_Meta_texspace_loc_set(ApiPtr *ptr, const float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  copy_v3_v3(mb->loc, values);
}

static void api_Meta_texspace_size_get(ApiPtr *ptr, float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  /* tex_space_mball() needs object.. ugh */

  copy_v3_v3(values, mb->size);
}

static void api_Meta_texspace_size_set(ApiPtr *ptr, const float *values)
{
  MetaBall *mb = (MetaBall *)ptr->data;

  copy_v3_v3(mb->size, values);
}

static void api_MetaBall_redraw_data(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  graph_id_tag_update(id, ID_RECALC_COPY_ON_WRITE);
  wm_main_add_notifier(NC_GEOM | ND_DATA, id);
}

static void api_MetaBall_update_data(Main *main, Scene *scene, ApiPtr *ptr)
{
  MetaBall *mb = (MetaBall *)ptr->owner_id;
  Object *ob;

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    for (ob = main->objects.first; ob; ob = ob->id.next) {
      if (ob->data == mb) {
        dune_mball_props_copy(scene, ob);
      }
    }

    DEG_id_tag_update(&mb->id, 0);
    WM_main_add_notifier(NC_GEOM | ND_DATA, mb);
  }
}

static void api_MetaBall_update_rotation(Main *main, Scene *scene, ApiPtr *ptr)
{
  MetaElem *ml = ptr->data;
  normalize_qt(ml->quat);
  api_MetaBall_update_data(main, scene, ptr);
}

static MetaElem *api_MetaBall_elements_new(MetaBall *mb, int type)
{
  MetaElem *ml = dune_mball_element_add(mb, type);

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, &mb->id);
  }

  return ml;
}

static void api_MetaBall_elements_remove(MetaBall *mb, ReportList *reports, ApiPtr *ml_ptr)
{
  MetaElem *ml = ml_ptr->data;

  if (lib_remlink_safe(&mb->elems, ml) == false) {
    dune_reportf(
        reports, RPT_ERROR, "Metaball '%s' does not contain spline given", mb->id.name + 2);
    return;
  }

  mem_freen(ml);
  API_PTR_INVALIDATE(ml_ptr);

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, &mb->id);
  }
}

static void api_MetaBall_elements_clear(MetaBall *mb)
{
  lib_freelistn(&mb->elems);

  /* cheating way for importers to avoid slow updates */
  if (mb->id.us > 0) {
    graph_id_tag_update(&mb->id, 0);
    wm_main_add_notifier(NC_GEOM | ND_DATA, &mb->id);
  }
}

static bool rna_Meta_is_editmode_get(PointerRNA *ptr)
{
  MetaBall *mb = (MetaBall *)ptr->owner_id;
  return (mb->editelems != NULL);
}

static char *rna_MetaElement_path(PointerRNA *ptr)
{
  MetaBall *mb = (MetaBall *)ptr->owner_id;
  MetaElem *ml = ptr->data;
  int index = -1;

  if (mb->editelems) {
    index = BLI_findindex(mb->editelems, ml);
  }
  if (index == -1) {
    index = BLI_findindex(&mb->elems, ml);
  }
  if (index == -1) {
    return NULL;
  }

  return BLI_sprintfN("elements[%d]", index);
}

#else

static void rna_def_metaelement(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  sapi = RNA_def_struct(dapi, "MetaElement", NULL);
  api_def_struct_stype(sapi, "MetaElem");
  api_def_struct_ui_text(sapi, "Metaball Element", "Blobby element in a metaball data-block");
  api_def_struct_path_fn(sapi, "api_MetaElement_path");
  api_def_struct_ui_icon(sapi, ICON_OUTLINER_DATA_META);

  /* enums */
  prop = RNA_def_prop(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_metaelem_type_items);
  RNA_def_property_ui_text(prop, "Type", "Metaball types");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  /* number values */
  prop = RNA_def_property(srna, "co", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_float_sdna(prop, NULL, "x");
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Location", "");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  prop = api_def_prop(sapi, "rotation", PROP_FLOAT, PROP_QUATERNION);
  api_def_prop_float_stype(prop, NULL, "quat");
  api_def_prop_ui_text(prop, "Rotation", "Normalized quaternion rotation");
  api_def_prop_update(prop, 0, "api_MetaBall_update_rotation");

  prop = api_def_prop(sapi, "radius", PROP_FLOAT, PROP_UNSIGNED | PROP_UNIT_LENGTH);
  RNA_def_prop_float_sdna(prop, NULL, "rad");
  RNA_def_prop_ui_text(prop, "Radius", "");
  RNA_def_prop_range(prop, 0.0f, FLT_MAX);
  RNA_def_prop_update(prop, 0, "rna_MetaBall_update_data");

  prop = RNA_def_property(srna, "size_x", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "expx");
  RNA_def_property_flag(prop, PROP_PROPORTIONAL);
  RNA_def_property_range(prop, 0.0f, 20.0f);
  RNA_def_property_ui_text(
      prop, "Size X", "Size of element, use of components depends on element type");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  prop = RNA_def_property(srna, "size_y", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_sdna(prop, NULL, "expy");
  api_def_property_flag(prop, PROP_PROPORTIONAL);
  api_def_property_range(prop, 0.0f, 20.0f);
  api_def_property_ui_text(
      prop, "Size Y", "Size of element, use of components depends on element type");
  api_def_prop_update(prop, 0, "rna_MetaBall_update_data");

  prop = api_def_prop(sapi, "size_z", PROP_FLOAT, PROP_DISTANCE);
  api_def_prop_float_stype(prop, NULL, "expz");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_range(prop, 0.0f, 20.0f);
  api_def_prop_ui_text(
      prop, "Size Z", "Size of element, use of components depends on element type");
  api_def_prop_update(prop, 0, "rna_MetaBall_update_data");

  prop = api_def_prop(sapi, "stiffness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "s");
  api_def_prop_range(prop, 0.0f, 10.0f);
  api_def_prop_ui_text(prop, "Stiffness", "Stiffness defines how much of the element to fill");
  api_def_prop_update(prop, 0, "rna_MetaBall_update_data");

  /* flags */
  prop = RNA_def_prop(srna, "use_negative", PROP_BOOLEAN, PROP_NONE);
  RNA_def_prop_bool_stype(prop, NULL, "flag", MB_NEGATIVE);
  RNA_def_prop_ui_text(prop, "Negative", "Set metaball as negative one");
  RNA_def_prop_update(prop, 0, "rna_MetaBall_update_data");

  prop = api_def_prop(sapi, "use_scale_stiffness", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_negative_sdna(prop, NULL, "flag", MB_SCALE_RAD);
  RNA_def_property_ui_text(prop, "Scale Stiffness", "Scale stiffness instead of radius");
  RNA_def_property_update(prop, 0, "rna_MetaBall_redraw_data");

  prop = RNA_def_property(srna, "select", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", 1); /* SELECT */
  RNA_def_property_ui_text(prop, "Select", "Select element");
  RNA_def_property_update(prop, 0, "rna_MetaBall_redraw_data");

  prop = RNA_def_property(srna, "hide", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", MB_HIDE);
  RNA_def_property_ui_text(prop, "Hide", "Hide element");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");
}

/* mball.elements */
static void rna_def_metaball_elements(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "MetaBallElements");
  srna = RNA_def_struct(brna, "MetaBallElements", NULL);
  RNA_def_struct_sdna(srna, "MetaBall");
  RNA_def_struct_ui_text(srna, "Metaball Elements", "Collection of metaball elements");

  func = RNA_def_function(srna, "new", "rna_MetaBall_elements_new");
  RNA_def_function_ui_description(func, "Add a new element to the metaball");
  RNA_def_enum(func,
               "type",
               rna_enum_metaelem_type_items,
               MB_BALL,
               "",
               "Type for the new metaball element");
  parm = RNA_def_pointer(func, "element", "MetaElement", "", "The newly created metaball element");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "remove", "rna_MetaBall_elements_remove");
  RNA_def_function_ui_description(func, "Remove an element from the metaball");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_pointer(func, "element", "MetaElement", "", "The element to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);

  func = RNA_def_function(srna, "clear", "rna_MetaBall_elements_clear");
  RNA_def_function_ui_description(func, "Remove all elements from the metaball");

  prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "lastelem");
  RNA_def_property_ui_text(prop, "Active Element", "Last selected element");
}

static void rna_def_metaball(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  static const EnumPropertyItem prop_update_items[] = {
      {MB_UPDATE_ALWAYS, "UPDATE_ALWAYS", 0, "Always", "While editing, update metaball always"},
      {MB_UPDATE_HALFRES,
       "HALFRES",
       0,
       "Half",
       "While editing, update metaball in half resolution"},
      {MB_UPDATE_FAST, "FAST", 0, "Fast", "While editing, update metaball without polygonization"},
      {MB_UPDATE_NEVER, "NEVER", 0, "Never", "While editing, don't update metaball at all"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "MetaBall", "ID");
  RNA_def_struct_ui_text(srna, "MetaBall", "Metaball data-block to defined blobby surfaces");
  RNA_def_struct_ui_icon(srna, ICON_META_DATA);

  prop = RNA_def_property(srna, "elements", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "elems", NULL);
  RNA_def_property_struct_type(prop, "MetaElement");
  RNA_def_property_ui_text(prop, "Elements", "Metaball elements");
  rna_def_metaball_elements(brna, prop);

  /* enums */
  prop = RNA_def_property(srna, "update_method", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "flag");
  RNA_def_property_enum_items(prop, prop_update_items);
  RNA_def_property_ui_text(prop, "Update", "Metaball edit update behavior");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  /* number values */
  prop = RNA_def_property(srna, "resolution", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "wiresize");
  RNA_def_property_range(prop, 0.005f, 10000.0f);
  RNA_def_property_ui_range(prop, 0.05f, 1000.0f, 2.5f, 3);
  RNA_def_property_ui_text(prop, "Wire Size", "Polygonization resolution in the 3D viewport");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  prop = RNA_def_property(srna, "render_resolution", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_float_sdna(prop, NULL, "rendersize");
  RNA_def_property_range(prop, 0.005f, 10000.0f);
  RNA_def_property_ui_range(prop, 0.025f, 1000.0f, 2.5f, 3);
  RNA_def_property_ui_text(prop, "Render Size", "Polygonization resolution in rendering");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  prop = RNA_def_property(srna, "threshold", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "thresh");
  RNA_def_property_range(prop, 0.0f, 5.0f);
  RNA_def_property_ui_text(prop, "Threshold", "Influence of metaball elements");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  /* texture space */
  prop = RNA_def_property(srna, "use_auto_texspace", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "texflag", MB_AUTOSPACE);
  RNA_def_property_ui_text(
      prop,
      "Auto Texture Space",
      "Adjust active object's texture space automatically when transforming object");

  prop = RNA_def_property(srna, "texspace_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_ui_text(prop, "Texture Space Location", "Texture space location");
  RNA_def_property_editable_func(prop, "rna_Meta_texspace_editable");
  RNA_def_property_float_funcs(
      prop, "rna_Meta_texspace_loc_get", "rna_Meta_texspace_loc_set", NULL);
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  prop = RNA_def_property(srna, "texspace_size", PROP_FLOAT, PROP_XYZ);
  RNA_def_property_array(prop, 3);
  RNA_def_property_flag(prop, PROP_PROPORTIONAL);
  RNA_def_property_ui_text(prop, "Texture Space Size", "Texture space size");
  RNA_def_property_editable_func(prop, "rna_Meta_texspace_editable");
  RNA_def_property_float_funcs(
      prop, "rna_Meta_texspace_size_get", "rna_Meta_texspace_size_set", NULL);
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");

  /* not supported yet */
#  if 0
  prop = RNA_def_property(srna, "texspace_rot", PROP_FLOAT, PROP_EULER);
  RNA_def_property_float(prop, NULL, "rot");
  RNA_def_property_ui_text(prop, "Texture Space Rotation", "Texture space rotation");
  RNA_def_property_editable_func(prop, "rna_Meta_texspace_editable");
  RNA_def_property_update(prop, 0, "rna_MetaBall_update_data");
#  endif

  /* materials */
  prop = RNA_def_property(srna, "materials", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "mat", "totcol");
  RNA_def_property_struct_type(prop, "Material");
  RNA_def_property_ui_text(prop, "Materials", "");
  RNA_def_property_srna(prop, "IDMaterials"); /* see rna_ID.c */
  RNA_def_property_collection_funcs(
      prop, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "rna_IDMaterials_assign_int");

  prop = RNA_def_property(srna, "is_editmode", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_Meta_is_editmode_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Is Editmode", "True when used in editmode");

  /* anim */
  rna_def_animdata_common(srna);

  RNA_api_meta(srna);
}

void RNA_def_meta(BlenderRNA *brna)
{
  rna_def_metaelement(brna);
  rna_def_metaball(brna);
}

#endif
