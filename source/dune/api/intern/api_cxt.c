#include <stdlib.h>

#include "types_id.h"
#include "types_userdef.h"

#include "dune_cxt.h"
#include "lib_utildefines.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h" /* own include */

const EnumPropItem api_enum_cxt_mode_items[] = {
    {CXT_MODE_EDIT_MESH, "EDIT_MESH", 0, "Mesh Edit", ""},
    {CXT_MODE_EDIT_CURVE, "EDIT_CURVE", 0, "Curve Edit", ""},
    {CXT_MODE_EDIT_CURVES, "EDIT_CURVES", 0, "Curves Edit", ""},
    {CXT_MODE_EDIT_SURFACE, "EDIT_SURFACE", 0, "Surface Edit", ""},
    {CXT_MODE_EDIT_TEXT, "EDIT_TEXT", 0, "Text Edit", ""},
    /* PARSKEL reuse will give issues */
    {CXT_MODE_EDIT_ARMATURE, "EDIT_ARMATURE", 0, "Armature Edit", ""},
    {CXT_MODE_EDIT_METABALL, "EDIT_METABALL", 0, "Metaball Edit", ""},
    {CXT_MODE_EDIT_LATTICE, "EDIT_LATTICE", 0, "Lattice Edit", ""},
    {CXT_MODE_POSE, "POSE", 0, "Pose", ""},
    {CXT_MODE_SCULPT, "SCULPT", 0, "Sculpt", ""},
    {CXT_MODE_PAINT_WEIGHT, "PAINT_WEIGHT", 0, "Weight Paint", ""},
    {CXT_MODE_PAINT_VERTEX, "PAINT_VERTEX", 0, "Vertex Paint", ""},
    {CXT_MODE_PAINT_TEXTURE, "PAINT_TEXTURE", 0, "Texture Paint", ""},
    {CXT_MODE_PARTICLE, "PARTICLE", 0, "Particle", ""},
    {CXT_MODE_OBJECT, "OBJECT", 0, "Object", ""},
    {CXT_MODE_PAINT_PEN, "PAINT_PEN", 0, "Pen Paint", ""},
    {CXT_MODE_EDIT_PEN, "EDIT_PEN", 0, "Pen Edit", ""},
    {CXT_MODE_SCULPT_PEN, "SCULPT_PEN", 0, "Pen Sculpt", ""},
    {CXT_MODE_WEIGHT_PEN, "WEIGHT_PEN", 0, "Pen Weight Paint", ""},
    {CXT_MODE_VERTEX_PEN, "VERTEX_PEN", 0, "Pen Vertex Paint", ""},
    {CXT_MODE_SCULPT_CURVES, "SCULPT_CURVES", 0, "Curves Sculpt", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "types_asset.h"

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

#  include "render_engine.h"

static ApiPtr rna_Context_manager_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_WindowManager, CTX_wm_manager(C));
}

static PointerRNA rna_Context_window_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_Window, CTX_wm_window(C));
}

static PointerRNA rna_Context_workspace_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_WorkSpace, CTX_wm_workspace(C));
}

static PointerRNA rna_Context_screen_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_Screen, CTX_wm_screen(C));
}

static PointerRNA rna_Context_area_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  PointerRNA newptr;
  RNA_pointer_create((ID *)CTX_wm_screen(C), &RNA_Area, CTX_wm_area(C), &newptr);
  return newptr;
}

static PointerRNA rna_Context_space_data_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  PointerRNA newptr;
  RNA_pointer_create((ID *)CTX_wm_screen(C), &RNA_Space, CTX_wm_space_data(C), &newptr);
  return newptr;
}

static PointerRNA rna_Context_region_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  PointerRNA newptr;
  RNA_pointer_create((ID *)CTX_wm_screen(C), &RNA_Region, CTX_wm_region(C), &newptr);
  return newptr;
}

static ApiPtr api_cxt_region_data_get(ApiPtr *ptr)
{
  Cxt *C = (Cxt *)ptr->data;

  /* only exists for one space still, no generic system yet */
  if (cxt_wm_view3d(C)) {
    PointerRNA newptr;
    api_ptr_create((Id *)CTX_wm_screen(C), &ApiRegionView3D, cxt_wm_region_data(C), &newptr);
    return newptr;
  }

  return ApiPtrNULL;
}

static ApiPtr api_cxt_gizmo_group_get(ApiPtr *ptr)
{
  Cxt *C = (Cxt *)ptr->data;
  ApiPtr newptr;
  api_ptr_create(NULL, &ApiGizmoGroup, cxt_wm_gizmo_group(C), &newptr);
  return newptr;
}

static PointerRNA rna_Context_asset_file_handle_get(ApiPtr *ptr)
{
  Cxt *C = (Cxt *)ptr->data;
  bool is_handle_valid;
  AssetHandle asset_handle = cxt_wm_asset_handle(C, &is_handle_valid);
  if (!is_handle_valid) {
    return ApiPtrNULL;
  }

  ApiPtr newptr;
  /* Have to cast away const, but the file entry API doesn't allow modifications anyway. */
  RNA_pointer_create(
      NULL, &RNA_FileSelectEntry, (struct FileDirEntry *)asset_handle.file_data, &newptr);
  return newptr;
}

static PointerRNA rna_Context_main_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_BlendData, CTX_data_main(C));
}

static PointerRNA rna_Context_scene_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_Scene, CTX_data_scene(C));
}

static PointerRNA rna_Context_view_layer_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  Scene *scene = CTX_data_scene(C);
  PointerRNA scene_ptr;

  RNA_id_pointer_create(&scene->id, &scene_ptr);
  return rna_pointer_inherit_refine(&scene_ptr, &RNA_ViewLayer, CTX_data_view_layer(C));
}

static void rna_Context_engine_get(PointerRNA *ptr, char *value)
{
  bContext *C = (bContext *)ptr->data;
  RenderEngineType *engine_type = CTX_data_engine_type(C);
  strcpy(value, engine_type->idname);
}

static int rna_Context_engine_length(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  RenderEngineType *engine_type = CTX_data_engine_type(C);
  return strlen(engine_type->idname);
}

static PointerRNA rna_Context_collection_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return rna_pointer_inherit_refine(ptr, &RNA_Collection, CTX_data_collection(C));
}

static PointerRNA rna_Context_layer_collection_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  ptr->owner_id = &CTX_data_scene(C)->id;
  return rna_pointer_inherit_refine(ptr, &RNA_LayerCollection, CTX_data_layer_collection(C));
}

static PointerRNA rna_Context_tool_settings_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  ptr->owner_id = &CTX_data_scene(C)->id;
  return rna_pointer_inherit_refine(ptr, &RNA_ToolSettings, CTX_data_tool_settings(C));
}

static PointerRNA rna_Context_preferences_get(PointerRNA *UNUSED(ptr))
{
  PointerRNA newptr;
  RNA_pointer_create(NULL, &RNA_Preferences, &U, &newptr);
  return newptr;
}

static int rna_Context_mode_get(PointerRNA *ptr)
{
  bContext *C = (bContext *)ptr->data;
  return CTX_data_mode_enum(C);
}

static struct Depsgraph *rna_Context_evaluated_depsgraph_get(bContext *C)
{
  struct Depsgraph *depsgraph;

#  ifdef WITH_PYTHON
  /* Allow drivers to be evaluated */
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  depsgraph = CTX_data_ensure_evaluated_depsgraph(C);

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif

  return depsgraph;
}

#else

void RNA_def_context(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  srna = RNA_def_struct(brna, "Context", NULL);
  RNA_def_struct_ui_text(srna, "Context", "Current windowmanager and data context");
  RNA_def_struct_sdna(srna, "bContext");

  /* WM */
  prop = RNA_def_property(srna, "window_manager", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "WindowManager");
  RNA_def_property_pointer_funcs(prop, "rna_Context_manager_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "window", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Window");
  RNA_def_property_pointer_funcs(prop, "rna_Context_window_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "workspace", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "WorkSpace");
  RNA_def_property_pointer_funcs(prop, "rna_Context_workspace_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "screen", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Screen");
  RNA_def_property_pointer_funcs(prop, "rna_Context_screen_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "area", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Area");
  RNA_def_property_pointer_funcs(prop, "rna_Context_area_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "space_data", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Space");
  RNA_def_property_pointer_funcs(prop, "rna_Context_space_data_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "region", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Region");
  RNA_def_property_pointer_funcs(prop, "rna_Context_region_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "region_data", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "RegionView3D");
  RNA_def_property_pointer_funcs(prop, "rna_Context_region_data_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "gizmo_group", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "GizmoGroup");
  RNA_def_property_pointer_funcs(prop, "rna_Context_gizmo_group_get", NULL, NULL, NULL);

  /* TODO can't expose AssetHandle, since there is no permanent storage to it (so we can't
   * return a pointer). Instead provide the FileDirEntry pointer it wraps. */
  prop = RNA_def_property(srna, "asset_file_handle", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "FileSelectEntry");
  RNA_def_property_pointer_funcs(prop, "rna_Context_asset_file_handle_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop,
                           "",
                           "The file of an active asset. Avoid using this, it will be replaced by "
                           "a proper AssetHandle design");

  /* Data */
  prop = RNA_def_property(srna, "blend_data", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "BlendData");
  RNA_def_property_pointer_funcs(prop, "rna_Context_main_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "scene", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Scene");
  RNA_def_property_pointer_funcs(prop, "rna_Context_scene_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "view_layer", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "ViewLayer");
  RNA_def_property_pointer_funcs(prop, "rna_Context_view_layer_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "engine", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs(prop, "rna_Context_engine_get", "rna_Context_engine_length", NULL);

  prop = RNA_def_property(srna, "collection", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Collection");
  RNA_def_property_pointer_funcs(prop, "rna_Context_collection_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "layer_collection", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "LayerCollection");
  RNA_def_property_pointer_funcs(prop, "rna_Context_layer_collection_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "tool_settings", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "ToolSettings");
  RNA_def_property_pointer_funcs(prop, "rna_Context_tool_settings_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "preferences", PROP_POINTER, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_struct_type(prop, "Preferences");
  RNA_def_property_pointer_funcs(prop, "rna_Context_preferences_get", NULL, NULL, NULL);

  prop = RNA_def_property(srna, "mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_context_mode_items);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_enum_funcs(prop, "rna_Context_mode_get", NULL, NULL);

  func = RNA_def_function(srna, "evaluated_depsgraph_get", "rna_Context_evaluated_depsgraph_get");
  RNA_def_function_ui_description(
      func,
      "Get the dependency graph for the current scene and view layer, to access to data-blocks "
      "with animation and modifiers applied. If any data-blocks have been edited, the dependency "
      "graph will be updated. This invalidates all references to evaluated data-blocks from the "
      "dependency graph.");
  parm = RNA_def_pointer(func, "depsgraph", "Depsgraph", "", "Evaluated dependency graph");
  RNA_def_function_return(func, parm);
}

#endif
