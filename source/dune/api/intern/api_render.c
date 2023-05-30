#include <stdlib.h>

#include "types_node.h"
#include "type_object.h"
#include "types_scene.h"

#include "lib_path_util.h"
#include "lib_utildefines.h"

#ifdef WITH_PYTHON
#  include "BPY_extern.h"
#endif

#include "graph.h"

#include "dune_image.h"
#include "dune_scene.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "render_engine.h"
#include "render_pipeline.h"

#include "ed_render.h"

/* Deprecated, only provided for API compatibility. */
const EnumPropItem api_enum_render_pass_type_items[] = {
    {SCE_PASS_COMBINED, "COMBINED", 0, "Combined", ""},
    {SCE_PASS_Z, "Z", 0, "Z", ""},
    {SCE_PASS_SHADOW, "SHADOW", 0, "Shadow", ""},
    {SCE_PASS_AO, "AO", 0, "Ambient Occlusion", ""},
    {SCE_PASS_POSITION, "POSITION", 0, "Position", ""},
    {SCE_PASS_NORMAL, "NORMAL", 0, "Normal", ""},
    {SCE_PASS_VECTOR, "VECTOR", 0, "Vector", ""},
    {SCE_PASS_INDEXOB, "OBJECT_INDEX", 0, "Object Index", ""},
    {SCE_PASS_UV, "UV", 0, "UV", ""},
    {SCE_PASS_MIST, "MIST", 0, "Mist", ""},
    {SCE_PASS_EMIT, "EMIT", 0, "Emit", ""},
    {SCE_PASS_ENVIRONMENT, "ENVIRONMENT", 0, "Environment", ""},
    {SCE_PASS_INDEXMA, "MATERIAL_INDEX", 0, "Material Index", ""},
    {SCE_PASS_DIFFUSE_DIRECT, "DIFFUSE_DIRECT", 0, "Diffuse Direct", ""},
    {SCE_PASS_DIFFUSE_INDIRECT, "DIFFUSE_INDIRECT", 0, "Diffuse Indirect", ""},
    {SCE_PASS_DIFFUSE_COLOR, "DIFFUSE_COLOR", 0, "Diffuse Color", ""},
    {SCE_PASS_GLOSSY_DIRECT, "GLOSSY_DIRECT", 0, "Glossy Direct", ""},
    {SCE_PASS_GLOSSY_INDIRECT, "GLOSSY_INDIRECT", 0, "Glossy Indirect", ""},
    {SCE_PASS_GLOSSY_COLOR, "GLOSSY_COLOR", 0, "Glossy Color", ""},
    {SCE_PASS_TRANSM_DIRECT, "TRANSMISSION_DIRECT", 0, "Transmission Direct", ""},
    {SCE_PASS_TRANSM_INDIRECT, "TRANSMISSION_INDIRECT", 0, "Transmission Indirect", ""},
    {SCE_PASS_TRANSM_COLOR, "TRANSMISSION_COLOR", 0, "Transmission Color", ""},
    {SCE_PASS_SUBSURFACE_DIRECT, "SUBSURFACE_DIRECT", 0, "Subsurface Direct", ""},
    {SCE_PASS_SUBSURFACE_INDIRECT, "SUBSURFACE_INDIRECT", 0, "Subsurface Indirect", ""},
    {SCE_PASS_SUBSURFACE_COLOR, "SUBSURFACE_COLOR", 0, "Subsurface Color", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_bake_pass_type_items[] = {
    {SCE_PASS_COMBINED, "COMBINED", 0, "Combined", ""},
    {SCE_PASS_AO, "AO", 0, "Ambient Occlusion", ""},
    {SCE_PASS_SHADOW, "SHADOW", 0, "Shadow", ""},
    {SCE_PASS_POSITION, "POSITION", 0, "Position", ""},
    {SCE_PASS_NORMAL, "NORMAL", 0, "Normal", ""},
    {SCE_PASS_UV, "UV", 0, "UV", ""},
    {SCE_PASS_ROUGHNESS, "ROUGHNESS", 0, "ROUGHNESS", ""},
    {SCE_PASS_EMIT, "EMIT", 0, "Emit", ""},
    {SCE_PASS_ENVIRONMENT, "ENVIRONMENT", 0, "Environment", ""},
    {SCE_PASS_DIFFUSE_COLOR, "DIFFUSE", 0, "Diffuse", ""},
    {SCE_PASS_GLOSSY_COLOR, "GLOSSY", 0, "Glossy", ""},
    {SCE_PASS_TRANSM_COLOR, "TRANSMISSION", 0, "Transmission", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "api_access.h"

#  include "dune_appdir.h"
#  include "dune_context.h"
#  include "dune_report.h"

#  include "gpu_capabilities.h"
#  include "gpu_shader.h"
#  include "imb_colormanagement.h"

#  include "graph_query.h"

/* RenderEngine Callbacks */

static void engine_tag_redraw(RenderEngine *engine)
{
  engine->flag |= RE_ENGINE_DO_DRAW;
}

static void engine_tag_update(RenderEngine *engine)
{
  engine->flag |= RE_ENGINE_DO_UPDATE;
}

static bool engine_support_display_space_shader(RenderEngine *UNUSED(engine), Scene *scene)
{
  return imb_colormanagement_support_glsl_draw(&scene->view_settings);
}

static int engine_get_preview_pixel_size(RenderEngine *UNUSED(engine), Scene *scene)
{
  return dune_render_preview_pixel_size(&scene->r);
}

static void engine_bind_display_space_shader(RenderEngine *UNUSED(engine), Scene *UNUSED(scene))
{
  GPUShader *shader = gpu_shader_get_builtin_shader(GPU_SHADER_2D_IMAGE);
  gpu_shader_bind(shader);

  int img_loc = gpu_shader_get_uniform(shader, "image");

  gpu_shader_uniform_int(shader, img_loc, 0);
}

static void engine_unbind_display_space_shader(RenderEngine *UNUSED(engine))
{
  gpu_shader_unbind();
}

static void engine_update(RenderEngine *engine, Main *bmain, Depsgraph *depsgraph)
{
  extern ApiFn api_RenderEngine_update_func;
  ApiPtr ptr;
  ApiParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->rna_ext.srna, engine, &ptr);
  fb = &api_RenderEngine_update_func;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "data", main);
  apu_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_render(RenderEngine *engine, Graph *graph)
{
  extern ApiFn api_RenderEngine_render_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_render_func;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "depsgraph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_render_frame_finish(RenderEngine *engine)
{
  extern ApiFn api_RenderEngine_render_frame_finish_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_render_frame_finish_fn;

  api_param_list_create(&list, &ptr, fn);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_draw(RenderEngine *engine, const struct Cxt *cxt, Graph *graph)
{
  extern ApiFn api_RenderEngine_draw_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_draw_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &cxt);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_bake(RenderEngine *engine,
                        struct Graph *graph,
                        struct Object *object,
                        const int pass_type,
                        const int pass_filter,
                        const int width,
                        const int height)
{
  extern ApiFn api_RenderEngine_bake_fn;
  ApiPtr ptr;
  ParamList list;
  ApuFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_bake_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "depsgraph", &graph);
  api_param_set_lookup(&list, "object", &object);
  api_param_set_lookup(&list, "pass_type", &pass_type);
  api_param_set_lookup(&list, "pass_filter", &pass_filter);
  api_param_set_lookup(&list, "width", &width);
  api_param_set_lookup(&list, "height", &height);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_view_update(RenderEngine *engine,
                               const struct Cxt *cxt,
                               Graph *graph)
{
  extern ApiFn api_RenderEngine_view_update_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_view_update_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &cxt);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_view_draw(RenderEngine *engine,
                             const struct Cxt *cxt,
                             Graph *graph)
{
  extern ApiFn api_RenderEngine_view_draw_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  fn = &api_RenderEngine_view_draw_func;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &cxt);
  api_param_set_lookup(&list, "graph", &graph);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_update_script_node(RenderEngine *engine,
                                      struct NodeTree *ntree,
                                      struct Node *node)
{
  extern ApiFn api_RenderEngine_update_script_node_fn;
  ApiPtr ptr, nodeptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->api_ext.sapi, engine, &ptr);
  api_ptr_create((Id *)ntree, &ApiNode, node, &nodeptr);
  fn = &api_RenderEngine_update_script_node_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "node", &nodeptr);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void engine_update_render_passes(RenderEngine *engine,
                                        struct Scene *scene,
                                        struct ViewLayer *view_layer)
{
  extern ApiFn api_RenderEngine_update_render_passes_fn;
  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, engine->type->rna_ext.srna, engine, &ptr);
  fn = &api_RenderEngine_update_render_passes_func;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "scene", &scene)
  apu_param_set_lookup(&list, "renderlayer", &view_layer);
  engine->type->api_ext.call(NULL, &ptr, fn, &list);

  api_param_list_free(&list);
}

/* RenderEngine registration */

static void api_RenderEngine_unregister(Main *main, ApiStruct *type)
{
  RenderEngineType *et = api_struct_dune_type_get(type);

  if (!et) {
    return;
  }

  /* Stop all renders in case we were using this one. */
  ed_render_engine_changed(main, false);
  render_FreeAllPersistentData();

  api_struct_free_extension(type, &et->api_ext);
  api_struct_free(&DUNE_API, type);
  lib_freelinkn(&R_engines, et);
}

static ApiStruct *api_RenderEngine_register(Main *main,
                                            ReportList *reports,
                                            void *data,
                                            const char *identifier,
                                            StructValidateFn validate,
                                            StructCbFn call,
                                            StructFreeFn free)
{
  RenderEngineType *et, dummyet = {NULL};
  RenderEngine dummyengine = {NULL};
  ApiPtr dummyptr;
  int have_fn[9];

  /* setup dummy engine & engine type to store static properties in */
  dummyengine.type = &dummyet;
  dummyet.flag |= RE_USE_SHADING_NODES_CUSTOM;
  api_ptr_create(NULL, &ApiRenderEngine, &dummyengine, &dummyptr);

  /* validate the python class */
  if (validate(&dummyptr, data, have_fn) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummyet.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "Registering render engine class: '%s' is too long, maximum length is %d",
                id,
                (int)sizeof(dummyet.idname));
    return NULL;
  }

  /* check if we have registered this engine type before, and remove it */
  for (et = R_engines.first; et; et = et->next) {
    if (STREQ(et->idname, dummyet.idname)) {
      if (et->api_ext.sapi) {
        api_RenderEngine_unregister(bmain, et->rna_ext.srna);
      }
      break;
    }
  }

  /* create a new engine type */
  et = mem_mallocn(sizeof(RenderEngineType), "python render engine");
  memcpy(et, &dummyet, sizeof(dummyet));

  et->api_ext.sapi = api_def_struct_ptr(&DUNE_API, et->idname, &ApiRenderEngine);
  et->api_ext.data = data;
  et->api_ext.call = call;
  et->api_ext.free = free;
  api_struct_dune_type_set(et->api_ext.sapi, et);

  et->update = (have_fn[0]) ? engine_update : NULL;
  et->render = (have_fn[1]) ? engine_render : NULL;
  et->render_frame_finish = (have_fn[2]) ? engine_render_frame_finish : NULL;
  et->draw = (have_fn[3]) ? engine_draw : NULL;
  et->bake = (have_fn[4]) ? engine_bake : NULL;
  et->view_update = (have_fn[5]) ? engine_view_update : NULL;
  et->view_draw = (have_fn[6]) ? engine_view_draw : NULL;
  et->update_script_node = (have_fn[7]) ? engine_update_script_node : NULL;
  et->update_render_passes = (have_fn[8]) ? engine_update_render_passes : NULL;

  render_engines_register(et);

  return et->rna_ext.srna;
}

static void **api_RenderEngine_instance(ApiPtr *ptr)
{
  RenderEngine *engine = ptr->data;
  return &engine->py_instance;
}

static ApiStruct *api_RenderEngine_refine(ApiPtr *ptr)
{
  RenderEngine *engine = (RenderEngine *)ptr->data;
  return (engine->type && engine->type->api_ext.sapi) ? engine->type->api_ext.sapi :
                                                        &ApiRenderEngine;
}

static void api_RenderEngine_tempdir_get(ApiPtr *UNUSED(ptr), char *value)
{
  lib_strncpy(value, dune_tempdir_session(), FILE_MAX);
}

static int api_RenderEngine_tempdir_length(ApiPtr *UNUSED(ptr))
{
  return strlen(dune_tempdir_session());
}

static ApiPtr api_RenderEngine_render_get(ApiPtr *ptr)
{
  RenderEngine *engine = (RenderEngine *)ptr->data;

  if (engine->re) {
    RenderData *r = render_engine_get_render_data(engine->re);

    return api_ptr_inherit_refine(ptr, &ApiRenderSettings, r);
  }
  else {
    return api_ptr_inherit_refine(ptr, &ApiRenderSettings, NULL);
  }
}

static ApiPtr api_RenderEngine_camera_override_get(ApiPtr *ptr)
{
  RenderEngine *engine = (RenderEngine *)ptr->data;
  /* TODO(sergey): Shouldn't engine point to an evaluated datablocks already? */
  if (engine->re) {
    Object *cam = render_GetCamera(engine->re);
    Object *cam_eval = graph_get_evaluated_object(engine->graph, cam);
    return api_ptr_inherit_refine(ptr, &ApiObject, cam_eval);
  }
  else {
    return api_ptr_inherit_refine(ptr, &ApiObject, engine->camera_override);
  }
}

static void api_RenderEngine_engine_frame_set(RenderEngine *engine, int frame, float subframe)
{
#  ifdef WITH_PYTHON
  BPy_BEGIN_ALLOW_THREADS;
#  endif

  render_engine_frame_set(engine, frame, subframe);

#  ifdef WITH_PYTHON
  BPy_END_ALLOW_THREADS;
#  endif
}

static void api_RenderResult_views_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  RenderResult *rr = (RenderResult *)ptr->data;
  api_iter_list_begin(iter, &rr->views, NULL);
}

static void api_RenderResult_layers_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  RenderResult *rr = (RenderResult *)ptr->data;
  api_iter_list_begin(iter, &rr->layers, NULL);
}

static void api_RenderResult_stamp_data_add_field(RenderResult *rr,
                                                  const char *field,
                                                  const char *value)
{
  dune_render_result_stamp_data(rr, field, value);
}

static void api_RenderLayer_passes_begin(CollectionPropIter *iter, ApiPtr *ptr)
{
  RenderLayer *rl = (RenderLayer *)ptr->data;
  api_iterator_listbase_begin(iter, &rl->passes, NULL);
}

static int api_RenderPass_rect_get_length(PointerRNA *ptr, int length[RNA_MAX_ARRAY_DIMENSION])
{
  RenderPass *rpass = (RenderPass *)ptr->data;

  length[0] = rpass->rectx * rpass->recty;
  length[1] = rpass->channels;

  return length[0] * length[1];
}

static void api_RenderPass_rect_get(PointerRNA *ptr, float *values)
{
  RenderPass *rpass = (RenderPass *)ptr->data;
  memcpy(values, rpass->rect, sizeof(float) * rpass->rectx * rpass->recty * rpass->channels);
}

void api_RenderPass_rect_set(PointerRNA *ptr, const float *values)
{
  RenderPass *rpass = (RenderPass *)ptr->data;
  memcpy(rpass->rect, values, sizeof(float) * rpass->rectx * rpass->recty * rpass->channels);
}

static RenderPass *api_RenderPass_find_by_type(RenderLayer *rl, int passtype, const char *view)
{
  return render_pass_find_by_type(rl, passtype, view);
}

static RenderPass *api_RenderPass_find_by_name(RenderLayer *rl, const char *name, const char *view)
{
  return render_pass_find_by_name(rl, name, view);
}

#else /* RNA_RUNTIME */

static void api_def_render_engine(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  ApiFn *fn;
  ApiProp *parm;

  static const EnumPropItem render_pass_type_items[] = {
      {SOCK_FLOAT, "VALUE", 0, "Value", ""},
      {SOCK_VECTOR, "VECTOR", 0, "Vector", ""},
      {SOCK_RGBA, "COLOR", 0, "Color", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "RenderEngine", NUL
  api_def_struct_stype(sapi, "RenderEngine");
  api_def_struct_ui_text(sapi, "Render Engine", "Render engine");
  api_def_struct_refine_fn(sapi, "api_RenderEngine_refine");
  api_def_struct_register_fns(sapi,
                                "api_RenderEngine_register",
                                "api_RenderEngine_unregister",
                                "rna_RenderEngine_instance");

  /* final render callbacks */
  func = api_def_function(srna, "update", NULL);
  RNA_def_function_ui_description(func, "Export scene data for render");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  RNA_def_pointer(func, "data", "BlendData", "", "");
  RNA_def_pointer(func, "depsgraph", "Depsgraph", "", "");

  func = RNA_def_function(srna, "render", NULL);
  RNA_def_function_ui_description(func, "Render scene into an image");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = RNA_def_pointer(func, "depsgraph", "Depsgraph", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "render_frame_finish", NULL);
  api_def_fn_ui_description(
      fn, "Perform finishing operations after all view layers in a frame were rendered");
  apo_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);

  fn = api_def_fn(sapi, "draw", NULL);
  api_def_fn_ui_description(fn, "Draw render image");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "depsgraph", "Depsgraph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "bake", NULL);
  api_def_fn_ui_description(fn, "Bake passes");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = api_def_ptr(fn, "depsgraph", "Depsgraph", "", "");
  apu_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "object", "Object", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_enum(fb, "pass_type", rna_enum_bake_pass_type_items, 0, "Pass", "Pass to bake");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn,
                     "pass_filter",
                     0,
                     0,
                     INT_MAX,
                     "Pass Filter",
                     "Filter to combined, diffuse, glossy and transmission passes",
                     0,
                     INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "width", 0, 0, INT_MAX, "Width", "Image width", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "height", 0, 0, INT_MAX, "Height", "Image height", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* viewport render callbacks */
  fn = api_def_fn(sapi, "view_update", NULL);
  api_def_fn_ui_description(fn, "Update on data changes for viewport render");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "depsgraph", "Depsgraph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  fn = api_def_fn(sapi, "view_draw", NULL);
  api_def_fn_ui_description(fn, "Draw viewport render");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL);
  parm = api_def_ptr(fn, "context", "Context", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = apu_def_ptr(fn, "graph", "Depsgraph", "", "");
  api_def_param_flags(parm, 0, PARM_REQUIRED);

  /* shader script callbacks */
  fn = api_def_fn(sapi, "update_script_node", NULL);
  api_def_fn_ui_description(func, "Compile shader script node");
  api_def_fn_flag(fn, FUNC_REGISTER_OPTIONAL | FUNC_ALLOW_WRITE);
  parm = api_def_ptr(fn, "node", "Node", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_RNAPTR);

  fn = api_def_fn(sapi, "update_render_passes", NULL);
  api_def_fn_ui_description(fn, "Update the render passes that will be generated");
  api_def_fn_flag(fn, FN_REGISTER_OPTIONAL | FN_ALLOW_WRITE);
  parm = api_def_ptr(fn, "scene", "Scene", "", "");
  parm = api_def_ptr(fn, "renderlayer", "ViewLayer", "", "");

  /* tag for redraw */
  fn = api_def_fn(sapi, "tag_redraw", "engine_tag_redraw");
  api_def_fn_ui_description(fn, "Request redraw for viewport rendering");

  /* tag for update */
  fn = api_def_fn(sapi, "tag_update", "engine_tag_update");
  api_def_fn_ui_description(fn, "Request update call for viewport rendering");

  fn = api_def_fn(sapi, "begin_result", "render_engine_begin_result");
  api_def_fn_ui_description(
      fn, "Create render result to write linear floating-point render layers and passes");
  parm = api_def_int(fn, "x", 0, 0, INT_MAX, "X", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "y", 0, 0, INT_MAX, "Y", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "w", 0, 0, INT_MAX, "Width", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "h", 0, 0, INT_MAX, "Height", "", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  api_def_string(
      func, "layer", NULL, 0, "Layer", "Single layer to get render result for"); /* NULL ok here */
  RNA_def_string(
      func, "view", NULL, 0, "View", "Single view to get render result for"); /* NULL ok here */
  parm = RNA_def_pointer(func, "result", "RenderResult", "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "update_result", "RE_engine_update_result");
  RNA_def_function_ui_description(
      func, "Signal that pixels have been updated and can be redrawn in the user interface");
  parm = RNA_def_pointer(func, "result", "RenderResult", "Result", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "end_result", "RE_engine_end_result");
  RNA_def_function_ui_description(func,
                                  "All pixels in the render result have been set and are final");
  parm = RNA_def_pointer(func, "result", "RenderResult", "Result", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(
      func, "cancel", 0, "Cancel", "Don't mark tile as done, don't merge results unless forced");
  RNA_def_boolean(func, "highlight", 0, "Highlight", "Don't mark tile as done yet");
  RNA_def_boolean(
      func, "do_merge_results", 0, "Merge Results", "Merge results even if cancel=true");

  func = RNA_def_function(srna, "add_pass", "RE_engine_add_pass");
  RNA_def_function_ui_description(func, "Add a pass to the render layer");
  parm = RNA_def_string(
      func, "name", NULL, 0, "Name", "Name of the Pass, without view or channel tag");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "channels", 0, 0, INT_MAX, "Channels", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(
      func, "chan_id", NULL, 0, "Channel IDs", "Channel names, one character per channel");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_string(
      func, "layer", NULL, 0, "Layer", "Single layer to add render pass to"); /* NULL ok here */

  func = RNA_def_function(srna, "get_result", "RE_engine_get_result");
  RNA_def_function_ui_description(func, "Get final result for non-pixel operations");
  parm = RNA_def_pointer(func, "result", "RenderResult", "Result", "");
  RNA_def_function_return(func, parm);

  func = api_def_fn(sapi, "test_break", "RE_engine_test_break");
  api_def_fn_ui_description(fn,
                            "Test if the render operation should been canceled, this is a "
                            "fast call that should be used regularly for responsiveness");
  parm = api_def_bool(fn, "do_break", 0, "Break", "");
  api_def_fn_return(fn, parm);

  fn = api_def_fn(sapi, "pass_by_index_get", "RE_engine_pass_by_index_get");
  parm = api_def_string(fn, "layer", NULL, 0, "Layer", "Name of render layer to get pass for");
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_int(fn, "index", 0, 0, INT_MAX, "Index", "Index of pass to get", 0, INT_MAX);
  api_def_param_flags(parm, 0, PARM_REQUIRED);
  parm = api_def_ptr(fn, "render_pass", "RenderPass", "Index", "Index of pass to get");
  api_def_fn_return(fn, parm);

  func = RNA_def_function(srna, "active_view_get", "RE_engine_active_view_get");
  parm = RNA_def_string(func, "view", NULL, 0, "View", "Single view active");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "active_view_set", "RE_engine_active_view_set");
  parm = RNA_def_string(
      func, "view", NULL, 0, "View", "Single view to set as active"); /* NULL ok here */
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "camera_shift_x", "RE_engine_get_camera_shift_x");
  parm = RNA_def_pointer(func, "camera", "Object", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func, "use_spherical_stereo", 0, "Spherical Stereo", "");
  parm = RNA_def_float(func, "shift_x", 0.0f, 0.0f, FLT_MAX, "Shift X", "", 0.0f, FLT_MAX);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "camera_model_matrix", "RE_engine_get_camera_model_matrix");
  parm = RNA_def_pointer(func, "camera", "Object", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_boolean(func, "use_spherical_stereo", 0, "Spherical Stereo", "");
  parm = RNA_def_float_matrix(func,
                              "r_model_matrix",
                              4,
                              4,
                              NULL,
                              0.0f,
                              0.0f,
                              "Model Matrix",
                              "Normalized camera model matrix",
                              0.0f,
                              0.0f);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_function_output(func, parm);

  func = RNA_def_function(srna, "use_spherical_stereo", "RE_engine_get_spherical_stereo");
  parm = RNA_def_pointer(func, "camera", "Object", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "use_spherical_stereo", 0, "Spherical Stereo", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "update_stats", "RE_engine_update_stats");
  RNA_def_function_ui_description(func, "Update and signal to redraw render status text");
  parm = RNA_def_string(func, "stats", NULL, 0, "Stats", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "info", NULL, 0, "Info", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "frame_set", "rna_RenderEngine_engine_frame_set");
  RNA_def_function_ui_description(func, "Evaluate scene at a different frame (for motion blur)");
  parm = RNA_def_int(func, "frame", 0, INT_MIN, INT_MAX, "Frame", "", INT_MIN, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float(func, "subframe", 0.0f, 0.0f, 1.0f, "Subframe", "", 0.0f, 1.0f);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "update_progress", "RE_engine_update_progress");
  RNA_def_function_ui_description(func, "Update progress percentage of render");
  parm = RNA_def_float(
      func, "progress", 0, 0.0f, 1.0f, "", "Percentage of render that's done", 0.0f, 1.0f);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "update_memory_stats", "RE_engine_update_memory_stats");
  RNA_def_function_ui_description(func, "Update memory usage statistics");
  RNA_def_float(func,
                "memory_used",
                0,
                0.0f,
                FLT_MAX,
                "",
                "Current memory usage in megabytes",
                0.0f,
                FLT_MAX);
  RNA_def_float(
      func, "memory_peak", 0, 0.0f, FLT_MAX, "", "Peak memory usage in megabytes", 0.0f, FLT_MAX);

  func = RNA_def_function(srna, "report", "RE_engine_report");
  RNA_def_function_ui_description(func, "Report info, warning or error messages");
  parm = RNA_def_enum_flag(func, "type", rna_enum_wm_report_items, 0, "Type", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "message", NULL, 0, "Report Message", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "error_set", "RE_engine_set_error_message");
  RNA_def_function_ui_description(func,
                                  "Set error message displaying after the render is finished");
  parm = RNA_def_string(func, "message", NULL, 0, "Report Message", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "bind_display_space_shader", "engine_bind_display_space_shader");
  RNA_def_function_ui_description(func,
                                  "Bind GLSL fragment shader that converts linear colors to "
                                  "display space colors using scene color management settings");
  parm = RNA_def_pointer(func, "scene", "Scene", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(
      srna, "unbind_display_space_shader", "engine_unbind_display_space_shader");
  RNA_def_function_ui_description(
      func, "Unbind GLSL display space shader, must always be called after binding the shader");

  func = RNA_def_function(
      srna, "support_display_space_shader", "engine_support_display_space_shader");
  RNA_def_function_ui_description(func,
                                  "Test if GLSL display space shader is supported for the "
                                  "combination of graphics card and scene settings");
  parm = RNA_def_pointer(func, "scene", "Scene", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "supported", 0, "Supported", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "get_preview_pixel_size", "engine_get_preview_pixel_size");
  RNA_def_function_ui_description(func,
                                  "Get the pixel size that should be used for preview rendering");
  parm = RNA_def_pointer(func, "scene", "Scene", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "pixel_size", 0, 1, 8, "Pixel Size", "", 1, 8);
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "free_blender_memory", "RE_engine_free_blender_memory");
  RNA_def_function_ui_description(func, "Free Blender side memory of render engine");

  func = RNA_def_function(srna, "tile_highlight_set", "RE_engine_tile_highlight_set");
  RNA_def_function_ui_description(func, "Set highlighted state of the given tile");
  parm = RNA_def_int(func, "x", 0, 0, INT_MAX, "X", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "y", 0, 0, INT_MAX, "Y", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "width", 0, 0, INT_MAX, "Width", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "height", 0, 0, INT_MAX, "Height", "", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "highlight", 0, "Highlight", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "tile_highlight_clear_all", "RE_engine_tile_highlight_clear_all");
  RNA_def_function_ui_description(func, "Clear highlight from all tiles");

  RNA_define_verify_sdna(0);

  prop = RNA_def_property(srna, "is_animation", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", RE_ENGINE_ANIMATION);

  prop = RNA_def_property(srna, "is_preview", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", RE_ENGINE_PREVIEW);

  prop = RNA_def_property(srna, "camera_override", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_funcs(prop, "rna_RenderEngine_camera_override_get", NULL, NULL, NULL);
  RNA_def_property_struct_type(prop, "Object");

  prop = RNA_def_property(srna, "layer_override", PROP_BOOLEAN, PROP_LAYER_MEMBER);
  RNA_def_property_boolean_sdna(prop, NULL, "layer_override", 1);
  RNA_def_property_array(prop, 20);

  prop = RNA_def_property(srna, "resolution_x", PROP_INT, PROP_PIXEL);
  RNA_def_property_int_sdna(prop, NULL, "resolution_x");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "resolution_y", PROP_INT, PROP_PIXEL);
  RNA_def_property_int_sdna(prop, NULL, "resolution_y");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "temporary_directory", PROP_STRING, PROP_NONE);
  RNA_def_function_ui_description(func, "The temp directory used by Blender");
  RNA_def_property_string_funcs(
      prop, "rna_RenderEngine_tempdir_get", "rna_RenderEngine_tempdir_length", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  /* Render Data */
  prop = RNA_def_property(srna, "render", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "RenderSettings");
  RNA_def_property_pointer_funcs(prop, "rna_RenderEngine_render_get", NULL, NULL, NULL);
  RNA_def_property_ui_text(prop, "Render Data", "");

  prop = RNA_def_property(srna, "use_highlight_tiles", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", RE_ENGINE_HIGHLIGHT_TILES);

  func = RNA_def_function(srna, "register_pass", "RE_engine_register_pass");
  RNA_def_function_ui_description(
      func, "Register a render pass that will be part of the render with the current settings");
  parm = RNA_def_pointer(func, "scene", "Scene", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "view_layer", "ViewLayer", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "name", NULL, MAX_NAME, "Name", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_int(func, "channels", 1, 1, 8, "Channels", "", 1, 4);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "chanid", NULL, 8, "Channel IDs", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_enum(func, "type", render_pass_type_items, SOCK_FLOAT, "Type", "");
  RNA_def_property_enum_native_type(parm, "eNodeSocketDatatype");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  /* registration */

  prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->idname");
  RNA_def_property_flag(prop, PROP_REGISTER);

  prop = RNA_def_property(srna, "bl_label", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->name");
  RNA_def_property_flag(prop, PROP_REGISTER);

  prop = RNA_def_property(srna, "bl_use_preview", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_PREVIEW);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop,
      "Use Preview Render",
      "Render engine supports being used for rendering previews of materials, lights and worlds");

  prop = RNA_def_property(srna, "bl_use_postprocess", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "type->flag", RE_USE_POSTPROCESS);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop, "Use Post Processing", "Apply compositing on render results");

  prop = RNA_def_property(srna, "bl_use_eevee_viewport", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_EEVEE_VIEWPORT);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop, "Use Eevee Viewport", "Uses Eevee for viewport shading in LookDev shading mode");

  prop = RNA_def_property(srna, "bl_use_custom_freestyle", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_CUSTOM_FREESTYLE);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop,
      "Use Custom Freestyle",
      "Handles freestyle rendering on its own, instead of delegating it to EEVEE");

  prop = RNA_def_property(srna, "bl_use_image_save", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "type->flag", RE_USE_NO_IMAGE_SAVE);
  RNA_def_property_boolean_default(prop, true);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop,
      "Use Image Save",
      "Save images/movie to disk while rendering an animation. "
      "Disabling image saving is only supported when bl_use_postprocess is also disabled");

  prop = RNA_def_property(srna, "bl_use_gpu_context", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_GPU_CONTEXT);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop,
      "Use GPU Context",
      "Enable OpenGL context for the render method, for engines that render using OpenGL");

  prop = RNA_def_property(srna, "bl_use_shading_nodes_custom", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_SHADING_NODES_CUSTOM);
  RNA_def_property_boolean_default(prop, true);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop,
                           "Use Custom Shading Nodes",
                           "Don't expose Cycles and Eevee shading nodes in the node editor user "
                           "interface, so own nodes can be used instead");

  prop = RNA_def_property(srna, "bl_use_spherical_stereo", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_SPHERICAL_STEREO);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop, "Use Spherical Stereo", "Support spherical stereo camera models");

  prop = RNA_def_property(srna, "bl_use_stereo_viewport", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_STEREO_VIEWPORT);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop, "Use Stereo Viewport", "Support rendering stereo 3D viewport");

  prop = RNA_def_property(srna, "bl_use_alembic_procedural", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "type->flag", RE_USE_ALEMBIC_PROCEDURAL);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop, "Use Alembic Procedural", "Support loading Alembic data at render time");

  RNA_define_verify_sdna(1);
}

static void rna_def_render_result(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  srna = RNA_def_struct(brna, "RenderResult", NULL);
  RNA_def_struct_ui_text(
      srna, "Render Result", "Result of rendering, including all layers and passes");

  func = RNA_def_function(srna, "load_from_file", "RE_result_load_from_file");
  RNA_def_function_ui_description(func,
                                  "Copies the pixels of this render result from an image file");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string_file_name(
      func,
      "filename",
      NULL,
      FILE_MAX,
      "File Name",
      "Filename to load into this render tile, must be no smaller than "
      "the render result");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  func = RNA_def_function(srna, "stamp_data_add_field", "rna_RenderResult_stamp_data_add_field");
  RNA_def_function_ui_description(func, "Add engine-specific stamp data to the result");
  parm = RNA_def_string(func, "field", NULL, 1024, "Field", "Name of the stamp field to add");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "value", NULL, 0, "Value", "Value of the stamp data");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  RNA_define_verify_sdna(0);

  prop = RNA_def_property(srna, "resolution_x", PROP_INT, PROP_PIXEL);
  RNA_def_property_int_sdna(prop, NULL, "rectx");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "resolution_y", PROP_INT, PROP_PIXEL);
  RNA_def_property_int_sdna(prop, NULL, "recty");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "layers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "RenderLayer");
  RNA_def_property_collection_funcs(prop,
                                    "rna_RenderResult_layers_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);

  prop = RNA_def_property(srna, "views", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "RenderView");
  RNA_def_property_collection_funcs(prop,
                                    "rna_RenderResult_views_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);

  RNA_define_verify_sdna(1);
}

static void rna_def_render_view(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "RenderView", NULL);
  RNA_def_struct_ui_text(srna, "Render View", "");

  RNA_define_verify_sdna(0);

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "name");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_struct_name_property(srna, prop);

  RNA_define_verify_sdna(1);
}

static void rna_def_render_passes(BlenderRNA *brna, PropertyRNA *cprop)
{
  StructRNA *srna;

  FunctionRNA *func;
  PropertyRNA *parm;

  RNA_def_property_srna(cprop, "RenderPasses");
  srna = RNA_def_struct(brna, "RenderPasses", NULL);
  RNA_def_struct_sdna(srna, "RenderLayer");
  RNA_def_struct_ui_text(srna, "Render Passes", "Collection of render passes");

  func = RNA_def_function(srna, "find_by_type", "rna_RenderPass_find_by_type");
  RNA_def_function_ui_description(func, "Get the render pass for a given type and view");
  parm = RNA_def_enum(
      func, "pass_type", rna_enum_render_pass_type_items, SCE_PASS_COMBINED, "Pass", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(
      func, "view", NULL, 0, "View", "Render view to get pass from"); /* NULL ok here */
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "render_pass", "RenderPass", "", "The matching render pass");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "find_by_name", "rna_RenderPass_find_by_name");
  RNA_def_function_ui_description(func, "Get the render pass for a given name and view");
  parm = RNA_def_string(func, "name", RE_PASSNAME_COMBINED, 0, "Pass", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(
      func, "view", NULL, 0, "View", "Render view to get pass from"); /* NULL ok here */
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "render_pass", "RenderPass", "", "The matching render pass");
  RNA_def_function_return(func, parm);
}

static void rna_def_render_layer(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  FunctionRNA *func;
  PropertyRNA *parm;

  srna = RNA_def_struct(brna, "RenderLayer", NULL);
  RNA_def_struct_ui_text(srna, "Render Layer", "");

  func = RNA_def_function(srna, "load_from_file", "RE_layer_load_from_file");
  RNA_def_function_ui_description(func,
                                  "Copies the pixels of this renderlayer from an image file");
  RNA_def_function_flag(func, FUNC_USE_REPORTS);
  parm = RNA_def_string(
      func,
      "filename",
      NULL,
      0,
      "Filename",
      "Filename to load into this render tile, must be no smaller than the renderlayer");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_int(func,
              "x",
              0,
              0,
              INT_MAX,
              "Offset X",
              "Offset the position to copy from if the image is larger than the render layer",
              0,
              INT_MAX);
  RNA_def_int(func,
              "y",
              0,
              0,
              INT_MAX,
              "Offset Y",
              "Offset the position to copy from if the image is larger than the render layer",
              0,
              INT_MAX);

  RNA_define_verify_sdna(0);

  rna_def_view_layer_common(brna, srna, false);

  prop = RNA_def_property(srna, "passes", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, "RenderPass");
  RNA_def_property_collection_funcs(prop,
                                    "rna_RenderLayer_passes_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  rna_def_render_passes(brna, prop);

  RNA_define_verify_sdna(1);
}

static void rna_def_render_pass(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "RenderPass", NULL);
  RNA_def_struct_ui_text(srna, "Render Pass", "");

  RNA_define_verify_sdna(0);

  prop = RNA_def_property(srna, "fullname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "fullname");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "name");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_struct_name_property(srna, prop);

  prop = RNA_def_property(srna, "channel_id", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "chan_id");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "channels", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "channels");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "rect", PROP_FLOAT, PROP_NONE);
  RNA_def_property_flag(prop, PROP_DYNAMIC);
  RNA_def_property_multi_array(prop, 2, NULL);
  RNA_def_property_dynamic_array_funcs(prop, "rna_RenderPass_rect_get_length");
  RNA_def_property_float_funcs(prop, "rna_RenderPass_rect_get", "rna_RenderPass_rect_set", NULL);

  prop = RNA_def_property(srna, "view_id", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "view_id");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  RNA_define_verify_sdna(1);
}

void RNA_def_render(BlenderRNA *brna)
{
  rna_def_render_engine(brna);
  rna_def_render_result(brna);
  rna_def_render_view(brna);
  rna_def_render_layer(brna);
  rna_def_render_pass(brna);
}

#endif /* RNA_RUNTIME */
