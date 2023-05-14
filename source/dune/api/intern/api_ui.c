#include <stdlib.h>

#include "types_screen.h"
#include "types_space.h"

#include "lang.h"

#include "dune_idprop.h"
#include "dune_screen.h"

#include "lib_list.h"

#include "api_define.h"

#include "api_enum_types.h"
#include "api_internal.h"

#include "ui.h"

#include "wm_toolsystem.h"
#include "wm_types.h"

/* see WM_types.h */
const EnumPropItem api_enum_op_ctx_items[] = {
    {WM_OP_INVOKE_DEFAULT, "INVOKE_DEFAULT", 0, "Invoke Default", ""},
    {WM_OP_INVOKE_REGION_WIN, "INVOKE_REGION_WIN", 0, "Invoke Region Window", ""},
    {WM_OP_INVOKE_REGION_CHANNELS, "INVOKE_REGION_CHANNELS", 0, "Invoke Region Channels", ""},
    {WM_OP_INVOKE_REGION_PREVIEW, "INVOKE_REGION_PREVIEW", 0, "Invoke Region Preview", ""},
    {WM_OP_INVOKE_AREA, "INVOKE_AREA", 0, "Invoke Area", ""},
    {WM_OP_INVOKE_SCREEN, "INVOKE_SCREEN", 0, "Invoke Screen", ""},
    {WM_OP_EX_DEFAULT, "EX_DEFAULT", 0, "Ex Default", ""},
    {WM_OP_EX_REGION_WIN, "EX_REGION_WIN", 0, "Ex Region Window", ""},
    {WM_OP_EX_REGION_CHANNELS, "EX_REGION_CHANNELS", 0, "Ex Region Channels", ""},
    {WM_OP_EX_REGION_PREVIEW, "EX_REGION_PREVIEW", 0, "Ex Region Preview", ""},
    {WM_OP_EX_AREA, "EX_AREA", 0, "Ex Area", ""},
    {WM_OP_EX_SCREEN, "EX_SCREEN", 0, "Ex Screen", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_uilist_layout_type_items[] = {
    {UILST_LAYOUT_DEFAULT, "DEFAULT", 0, "Default Layout", "Use the default, multi-rows layout"},
    {UILST_LAYOUT_COMPACT, "COMPACT", 0, "Compact Layout", "Use the compact, single-row layout"},
    {UILST_LAYOUT_GRID, "GRID", 0, "Grid Layout", "Use the grid-based layout"},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "api_access.h"

#  include "lib_dynstr.h"

#  include "dune_ctx.h"
#  include "dune_report.h"
#  include "dune_screen.h"

#  include "wm_api.h"

static ARegionType *region_type_find(ReportList *reports, int space_type, int region_type)
{
  SpaceType *st;
  ARegionType *art;

  st = dune_spacetype_from_id(space_type);

  for (art = (st) ? st->regiontypes.first : NULL; art; art = art->next) {
    if (art->regionid == region_type) {
      break;
    }
  }

  /* region type not found? abort */
  if (art == NULL) {
    dune_report(reports, RPT_ERROR, "Region not found in space type");
    return NULL;
  }

  return art;
}

/* Panel */

static bool panel_poll(const Ctx *C, PanelType *pt)
{
  extern ApiFn api_Panel_poll_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  bool visible;

  api_ptr_create(NULL, pt->api_ext.sapi, NULL, &ptr); /* dummy */
  fn = &api_Panel_poll_fn; /* api_struct_find_fun(&ptr, "poll"); */

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &C);
  pt->api_ext.call((Ctx *)C, &ptr, fn, &list);

  api_param_get_lookup(&list, "visible", &ret);
  visible = *(bool *)ret;

  api_param_list_free(&list);

  return visible;
}

static void panel_draw(const Ctx *C, Panel *panel)
{
  extern ApiFn api_Panel_draw_func;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, panel->type->api_ext.sapi, panel, &ptr);
  fn = &api_Panel_draw_fn; /* api_struct_find_function(&ptr, "draw"); */

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &C);
  panel->type->api_ext.call((Ctx *)C, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void panel_draw_header(const Ctx *C, Panel *panel)
{
  extern ApiFn api_Panel_draw_header_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, panel->type->api_ext.sapi, panel, &ptr);
  fn = &api_Panel_draw_header_fn; /* RNA_struct_find_function(&ptr, "draw_header"); */

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &C);
  panel->type->api_ext.call((Ctx *)C, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void panel_draw_header_preset(const Ctx *C, Panel *panel)
{
  extern ApiFn api_Panel_draw_header_preset_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, panel->type->api_ext.sapi, panel, &ptr);
  fn = &api_Panel_draw_header_preset_fn;

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &C);
  panel->type->api_ext.call((Ctx *)C, &ptr, fn, &list);

  api_param_list_free(&list);
}

static void panel_type_clear_recursive(Panel *panel, const PanelType *type)
{
  if (panel->type == type) {
    panel->type = NULL;
  }

  LIST_FOREACH (Panel *, child_panel, &panel->children) {
    panel_type_clear_recursive(child_panel, type);
  }
}

static bool api_Panel_unregister(Main *main, ApiStruc *type)
{
  ARegionType *art;
  PanelType *pt = api_struct_dunr_type_get(type);

  if (!pt) {
    return false;
  }
  if (!(art = region_type_find(NULL, pt->space_type, pt->region_type))) {
    return false;
  }

  api_struct_free_extension(type, &pt->api_ext);
  api_struct_free(&DUNE_API, type);

  if (pt->parent) {
    LinkData *link = lib_findptr(&pt->parent->children, pt, offsetof(LinkData, data));
    lib_freelinkn(&pt->parent->children, link);
  }

  wm_paneltype_remove(pt);

  LIST_FOREACH (LinkData *, link, &pt->children) {
    PanelType *child_pt = link->data;
    child_pt->parent = NULL;
  }

  const char space_type = pt->space_type;
  lib_freelistn(&pt->children);
  lib_freelinkn(&art->paneltypes, pt);

  for (Screen *screen = main->screens.first; screen; screen = screen->id.next) {
    LIST_FOREACH (ScrArea *, area, &screen->areabase) {
      LIST_FOREACH (SpaceLink *, sl, &area->spacedata) {
        if (sl->spacetype == space_type) {
          List *regionbase = (sl == area->spacedata.first) ? &area->regionbase :
                                                                 &sl->regionbase;
          LIST_FOREACH (ARegion *, region, regionbase) {
            if (region->type == art) {
              LIST_FOREACH (Panel *, panel, &region->panels) {
                panel_type_clear_recursive(panel, pt);
              }
            }
            /* The unregistered panel might have had a template that added instanced panels,
             * so remove them just in case. They can be re-added on redraw anyway. */
            ui_panels_free_instanced(NULL, region);
          }
        }
      }
    }
  }

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);
  return true;
}

static ApiStruct *api_Panel_register(Main *main,
                                     ReportList *reports,
                                     void *data,
                                     const char *id,
                                     StructValidateFn validate,
                                     StructCbFn call,
                                     StructFreeFn free)
{
  const char *error_prefix = "Registering panel class:";
  ARegionType *art;
  PanelType *pt, *parent = NULL, dummy_pt = {NULL};
  Panel dummy_panel = {NULL};
  ApiPtr dummy_panel_ptr;
  bool have_fn[4];
  size_t over_alloc = 0; /* Warning, if this becomes a mess, we better do another allocation. */
  char _panel_descr[API_DYN_DESCR_MAX];
  size_t description_size = 0;

  /* setup dummy panel & panel type to store static properties in */
  dummy_panel.type = &dummy_pt;
  _panel_descr[0] = '\0';
  dummy_panel.type->description = _panel_descr;
  api_ptr_create(NULL, &ApiPanel, &dummy_panel, &dummy_panel_ptr);

  /* We have to set default context! Else we get a void string... */
  strcpy(dummy_pt.translation_context, LANG_CTX_DEFAULT_BPYRNA);

  /* validate the python class */
  if (validate(&dummy_panel_ptr, data, have_fn) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummy_pt.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is too long, maximum length is %d",
                error_prefix,
                id,
                (int)sizeof(dummy_pt.idname));
    return NULL;
  }

  if ((1 << dummy_pt.region_type) & RGN_TYPE_HAS_CATEGORY_MASK) {
    if (dummy_pt.category[0] == '\0') {
      /* Use a fallback, otherwise an empty value will draw the panel in every category. */
      strcpy(dummy_pt.category, PNL_CATEGORY_FALLBACK);
#  ifndef NDEBUG
      printf("%s '%s' misses category, please update the script\n", error_prefix, dummy_pt.idname);
#  endif
    }
  }
  else {
    if (dummy_pt.category[0] != '\0') {
      if ((1 << dummy_pt.space_type) & WM_TOOLSYSTEM_SPACE_MASK) {
        dune_reportf(reports,
                    RPT_ERROR,
                    "%s '%s' has category '%s'",
                    error_prefix,
                    dummy_pt.idname,
                    dummy_pt.category);
        return NULL;
      }
    }
  }

  if (!(art = region_type_find(reports, dummy_pt.space_type, dummy_pt.region_type))) {
    return NULL;
  }

  /* check if we have registered this panel type before, and remove it */
  for (pt = art->paneltypes.first; pt; pt = pt->next) {
    if (STREQ(pt->idname, dummy_pt.idname)) {
      PanelType *pt_next = pt->next;
      ApiStruct *srna = pt->api_ext.sapi;
      if (sapi) {
        if (!api_Panel_unregister(main, sapi)) {
          dune_reportf(reports,
                      RPT_ERROR,
                      "%s '%s', bl_idname '%s' could not be unregistered",
                      error_prefix,
                      id,
                      dummy_pt.idname);
        }
      }
      else {
        lib_freelinkn(&art->paneltypes, pt);
      }

      /* The order of panel types will be altered on re-registration. */
      if (dummy_pt.parent_id[0] && (parent == NULL)) {
        for (pt = pt_next; pt; pt = pt->next) {
          if (STREQ(pt->idname, dummy_pt.parent_id)) {
            parent = pt;
            break;
          }
        }
      }

      break;
    }

    if (dummy_pt.parent_id[0] && STREQ(pt->idname, dummy_pt.parent_id)) {
      parent = pt;
    }
  }

  if (!api_struct_available_or_report(reports, dummy_pt.idname)) {
    return NULL;
  }
  if (!api_struct_bl_idname_ok_or_report(reports, dummy_pt.idname, "_PT_")) {
    return NULL;
  }
  if (dummy_pt.parent_id[0] && !parent) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s parent '%s' for '%s' not found",
                error_prefix,
                dummy_pt.parent_id,
                dummy_pt.idname);
    return NULL;
  }

  /* create a new panel type */
  if (_panel_descr[0]) {
    description_size = strlen(_panel_descr) + 1;
    over_alloc += description_size;
  }
  pt = mem_callocn(sizeof(PanelType) + over_alloc, "python buttons panel");
  memcpy(pt, &dummy_pt, sizeof(dummy_pt));

  if (_panel_descr[0]) {
    char *buf = (char *)(pt + 1);
    memcpy(buf, _panel_descr, description_size);
    pt->description = buf;
  }
  else {
    pt->description = NULL;
  }

  pt->api_ext.sapi = api_def_struct_ptr(&DUNE_API, pt->idname, &ApiPanel);
  api_def_struct_translation_ctx(pt->api_ext.sapi, pt->translation_ctx);
  pt->api_ext.data = data;
  pt->api_ext.call = call;
  pt->api_ext.free = free;
  api_struct_dune_type_set(pt->api_ext.sapi, pt);
  api_def_struct_flag(pt->api_ext.sapi, STRUCT_NO_IDPROPS);

  pt->poll = (have_fn[0]) ? panel_poll : NULL;
  pt->draw = (have_fn[1]) ? panel_draw : NULL;
  pt->draw_header = (have_fn[2]) ? panel_draw_header : NULL;
  pt->draw_header_preset = (have_fn[3]) ? panel_draw_header_preset : NULL;

  /* Find position to insert panel based on order. */
  PanelType *pt_iter = art->paneltypes.last;

  for (; pt_iter; pt_iter = pt_iter->prev) {
    /* No header has priority. */
    if ((pt->flag & PANEL_TYPE_NO_HEADER) && !(pt_iter->flag & PANEL_TYPE_NO_HEADER)) {
      continue;
    }
    if (pt_iter->order <= pt->order) {
      break;
    }
  }

  /* Insert into list. */
  lib_insertlinkafter(&art->paneltypes, pt_iter, pt);

  if (parent) {
    pt->parent = parent;
    LinkData *pt_child_iter = parent->children.last;
    for (; pt_child_iter; pt_child_iter = pt_child_iter->prev) {
      PanelType *pt_child = pt_child_iter->data;
      if (pt_child->order <= pt->order) {
        break;
      }
    }
    lib_insertlinkafter(&parent->children, pt_child_iter, lib_genericnoden(pt));
  }

  {
    const char *owner_id = api_struct_state_owner_get();
    if (owner_id) {
      STRNCPY(pt->owner_id, owner_id);
    }
  }

  wm_paneltype_add(pt);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);

  return pt->api_ext.sapi;
}

static ApiStruct *api_Panel_refine(ApiPtr *ptr)
{
  Panel *menu = (Panel *)ptr->data;
  return (menu->type && menu->type->api_ext.sapi) ? menu->type->api_ext.sapi : &ApiPanel;
}

static ApiStruct *api_Panel_custom_data_typef(ApiPtr *ptr)
{
  Panel *panel = (Panel *)ptr->data;

  return ui_panel_custom_data_get(panel)->type;
}

static ApiPtr api_Panel_custom_data_get(ApiPtr *ptr)
{
  Panel *panel = (Panel *)ptr->data;

  /* Because the panel custom data is general we can't refine the pointer type here. */
  return *ui_panel_custom_data_get(panel);
}

/* UIList */
static int api_UIList_filter_const_FILTER_ITEM_get(ApiPtr *UNUSED(ptr))
{
  return UILST_FLT_ITEM;
}

static IdProp **api_UIList_idprops(ApiPtr *ptr)
{
  uiList *ui_list = (uiList *)ptr->data;
  return &ui_list->props;
}

static void api_UIList_list_id_get(ApiPtr *ptr, char *value)
{
  uiList *ui_list = (uiList *)ptr->data;
  if (!ui_list->type) {
    value[0] = '\0';
    return;
  }

  strcpy(value, wm_uilisttype_list_id_get(ui_list->type, ui_list));
}

static int api_UIList_list_id_length(ApiPtr *ptr)
{
  uiList *ui_list = (uiList *)ptr->data;
  if (!ui_list->type) {
    return 0;
  }

  return strlen(wm_uilisttype_list_id_get(ui_list->type, ui_list));
}

static void uilist_draw_item(uiList *ui_list,
                             const Ctx *C,
                             uiLayout *layout,
                             ApiPtr *dataptr,
                             ApiPtr *itemptr,
                             int icon,
                             ApiPtr *active_dataptr,
                             const char *active_propname,
                             int index,
                             int flt_flag)
{
  extern ApiFn api_UIList_draw_item_fn;

  ApiPtr ul_ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, ui_list->type->rna_ext.srna, ui_list, &ul_ptr);
  func = &api_UIList_draw_item_func; /* RNA_struct_find_function(&ul_ptr, "draw_item"); */

  api_param_list_create(&list, &ul_ptr, func);
  api_param_set_lookup(&list, "context", &C);
  api_param_set_lookup(&list, "layout", &layout);
  api_param_set_lookup(&list, "data", dataptr);
  api_param_set_lookup(&list, "item", itemptr);
  api_param_set_lookup(&list, "icon", &icon);
  api_param_set_lookup(&list, "active_data", active_dataptr);
  api_param_set_lookup(&list, "active_property", &active_propname);
  api_param_set_lookup(&list, "index", &index);
  api_param_set_lookup(&list, "flt_flag", &flt_flag);
  ui_list->type->api_ext.call((Ctx *)C, &ul_ptr, fn, &list);

  api_param_list_free(&list);
}

static void uilist_draw_filter(uiList *ui_list, const bContext *C, uiLayout *layout)
{
  extern ApiFn api_UIList_draw_filter_func;

  ApiPtr ul_ptr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, ui_list->type->api_ext.sapi, ui_list, &ul_ptr);
  fn = &api_UIList_draw_filter_fn; /* RNA_struct_find_function(&ul_ptr, "draw_filter"); */

  api_param_list_create(&list, &ul_ptr, func);
  api_param_set_lookup(&list, "context", &C);
  api_param_set_lookup(&list, "layout", &layout);
  ui_list->type->rna_ext.call((Ctx *)C, &ul_ptr, func, &list);

  api_param_list_free(&list);
}

static void uilist_filter_items(uiList *ui_list,
                                const Ctx *C,
                                ApiPtr *dataptr,
                                const char *propname)
{
  extern ApiFn api_UIList_filter_items_fn;

  ApiPtr ul_ptr;
  ParamList list;
  ApiFn *fn;
  ApiProp *parm;

  uiListDyn *flt_data = ui_list->dyn_data;
  int *filter_flags, *filter_neworder;
  void *ret1, *ret2;
  int ret_len;
  int len = flt_data->items_len = api_collection_length(dataptr, propname);

  api_ptr_create(&ctx_wm_screen(C)->id, ui_list->type->api_ext.sapi, ui_list, &ul_ptr);
  fn = &api_UIList_filter_items_fn; /* api_struct_find_fn(&ul_ptr, "filter_items"); */

  api_param_list_create(&list, &ul_ptr, fn);
  api_param_set_lookup(&list, "context", &C);
  api_param_set_lookup(&list, "data", dataptr);
  api_param_set_lookup(&list, "property", &propname);

  ui_list->type->api_ext.call((Ctx *)C, &ul_ptr, fn, &list);

  parm = api_fn_find_param(NULL, fn, "filter_flags");
  ret_len = api_param_dynamic_length_get(&list, parm);
  if (!ELEM(ret_len, len, 0)) {
    printf("%s: Error, py func returned %d items in %s, %d or none were expected.\n",
           __func__,
           api_param_dynamic_length_get(&list, parm),
           "filter_flags",
           len);
    /* NOTE: we cannot return here, we would let flt_data in inconsistent state... see #38356. */
    filter_flags = NULL;
  }
  else {
    api_param_get(&list, parm, &ret1);
    filter_flags = (int *)ret1;
  }

  parm = api_fn_find_param(NULL, func, "filter_neworder");
  ret_len = api_param_dynamic_length_get(&list, parm);
  if (!ELEM(ret_len, len, 0)) {
    printf("%s: Error, py fn returned %d items in %s, %d or none were expected.\n",
           __func__,
           api_param_dynamic_length_get(&list, parm),
           "filter_neworder",
           len);
    /* NOTE: we cannot return here, we would let flt_data in inconsistent state... see #38356. */
    filter_neworder = NULL;
  }
  else {
    api_param_get(&list, parm, &ret2);
    filter_neworder = (int *)ret2;
  }

  /* We have to do some final checks and transforms... */
  {
    int i, filter_exclude = ui_list->filter_flag & UILST_FLT_EXCLUDE;
    if (filter_flags) {
      flt_data->items_filter_flags = MEM_mallocN(sizeof(int) * len, __func__);
      memcpy(flt_data->items_filter_flags, filter_flags, sizeof(int) * len);

      if (filter_neworder) {
        /* For sake of simplicity, py filtering is expected to filter all items,
         * but we actually only want reordering data for shown items!
         */
        int items_shown, shown_idx;
        int t_idx, t_ni, prev_ni;
        flt_data->items_shown = 0;
        for (i = 0, shown_idx = 0; i < len; i++) {
          if ((filter_flags[i] & UILST_FLT_ITEM) ^ filter_exclude) {
            filter_neworder[shown_idx++] = filter_neworder[i];
          }
        }
        items_shown = flt_data->items_shown = shown_idx;
        flt_data->items_filter_neworder = MEM_mallocN(sizeof(int) * items_shown, __func__);
        /* And now, bring back new indices into the [0, items_shown[ range!
         * XXX This is O(N^2). :/
         */
        for (shown_idx = 0, prev_ni = -1; shown_idx < items_shown; shown_idx++) {
          for (i = 0, t_ni = len, t_idx = -1; i < items_shown; i++) {
            int ni = filter_neworder[i];
            if (ni > prev_ni && ni < t_ni) {
              t_idx = i;
              t_ni = ni;
            }
          }
          if (t_idx >= 0) {
            prev_ni = t_ni;
            flt_data->items_filter_neworder[t_idx] = shown_idx;
          }
        }
      }
      else {
        /* we still have to set flt_data->items_shown... */
        flt_data->items_shown = 0;
        for (i = 0; i < len; i++) {
          if ((filter_flags[i] & UILST_FLT_ITEM) ^ filter_exclude) {
            flt_data->items_shown++;
          }
        }
      }
    }
    else {
      flt_data->items_shown = len;

      if (filter_neworder) {
        flt_data->items_filter_neworder = MEM_mallocN(sizeof(int) * len, __func__);
        memcpy(flt_data->items_filter_neworder, filter_neworder, sizeof(int) * len);
      }
    }
  }

  api_param_list_free(&list);
}

static bool api_UIList_unregister(Main *main, ApiStruct *type)
{
  uiListType *ult = api_struct_dune_type_get(type);

  if (!ult) {
    return false;
  }

  api_struct_free_extension(type, &ult->api_ext);
  api_struct_free(&DUNE_API, type);

  wm_uilisttype_remove_ptr(main, ult);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);
  return true;
}

static ApiStruct *api_UIList_register(Main *bmain,
                                      ReportList *reports,
                                      void *data,
                                      const char *identifier,
                                      StructValidateFunc validate,
                                      StructCallbackFunc call,
                                      StructFreeFunc free)
{
  const char *error_prefix = "Registering uilist class:";
  uiListType *ult, dummy_ult = {NULL};
  uiList dummy_uilist = {NULL};
  ApiPtr dummy_ul_ptr;
  bool have_fn[3];
  size_t over_alloc = 0; /* Warning, if this becomes a mess, we better do another allocation. */

  /* setup dummy menu & menu type to store static properties in */
  dummy_uilist.type = &dummy_ult;
  api_ptr_create(NULL, &ApiUIList, &dummy_uilist, &dummy_ul_ptr);

  /* validate the python class */
  if (validate(&dummy_ul_ptr, data, have_fn) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummy_ult.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is too long, maximum length is %d",
                error_prefix,
                id,
                (int)sizeof(dummy_ult.idname));
    return NULL;
  }

  /* Check if we have registered this UI-list type before, and remove it. */
  ult = wm_uilisttype_find(dummy_ult.idname, true);
  if (ult) {
    ApiStruct *sapi = ult->api_ext.sapi;
    if (!(sapi && api_UIList_unregister(main, sapi))) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "%s '%s', bl_idname '%s' %s",
                  error_prefix,
                  id,
                  dummy_ult.idname,
                  sapi ? "is built-in" : "could not be unregistered");
      return NULL;
    }
  }
  if (!api_struct_available_or_report(reports, dummy_ult.idname)) {
    return NULL;
  }
  if (!api_struct_bl_idname_ok_or_report(reports, dummy_ult.idname, "_UL_")) {
    return NULL;
  }

  /* create a new menu type */
  ult = mem_callocn(sizeof(uiListType) + over_alloc, "python uilist");
  memcpy(ult, &dummy_ult, sizeof(dummy_ult));

  ult->api_ext.sapi = api_def_struct_ptr(&DUNE_API, ult->idname, &ApiUIList);
  ult->api_ext.data = data;
  ult->api_ext.call = call;
  ult->api_ext.free = free;
  api_struct_dune_type_set(ult->api_ext.sapi, ult);

  ult->draw_item = (have_fn[0]) ? uilist_draw_item : NULL;
  ult->draw_filter = (have_fn[1]) ? uilist_draw_filter : NULL;
  ult->filter_items = (have_fn[2]) ? uilist_filter_items : NULL;

  wm_uilisttype_add(ult);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);

  return ult->api_ext.sapi;
}

static ApiStruct *api_UIList_refine(ApiPtr *ptr)
{
  uiList *ui_list = (uiList *)ptr->data;
  return (ui_list->type && ui_list->type->api_ext.sapi) ? ui_list->type->api_ext.srna :
                                                          &ApiUIList;
}

/* Header */

static void header_draw(const Ctx *C, Header *hdr)
{
  extern ApiFn api_Header_draw_fn;

  ApiPtr htr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, hdr->type->api_ext.sapi, hdr, &htr);
  func = &api_Header_draw_fn; /* api_struct_find_fn(&htr, "draw"); */

  api_param_list_create(&list, &htr, fn);
  api_param_set_lookup(&list, "context", &C);
  hdr->type->api_ext.call((Ctx *)C, &htr, fn, &list);

  api_param_list_free(&list);
}

static bool api_Header_unregister(Main *UNUSED(main), ApiStruct *type)
{
  ARegionType *art;
  HeaderType *ht = api_struct_blender_type_get(type);

  if (!ht) {
    return false;
  }
  if (!(art = region_type_find(NULL, ht->space_type, ht->region_type))) {
    return false;
  }

  api_struct_free_extension(type, &ht->api_ext);
  api_struct_free(&DUNE_API, type);

  lib_freelinkn(&art->headertypes, ht);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);
  return true;
}

static ApiStruct *api_Header_register(Main *main,
                                      ReportList *reports,
                                      void *data,
                                      const char *id,
                                      StructValidateFn validate,
                                      StructCbF call,
                                      StructFreeFn free)
{
  const char *error_prefix = "Registering header class:";
  ARegionType *art;
  HeaderType *ht, dummy_ht = {NULL};
  Header dummy_header = {NULL};
  ApiPtr dummy_header_ptr;
  bool have_fn[1];

  /* setup dummy header & header type to store static properties in */
  dummy_header.type = &dummy_ht;
  dummy_ht.region_type = RGN_TYPE_HEADER; /* RGN_TYPE_HEADER by default, may be overridden */
  api_ptr_create(NULL, &ApiHeader, &dummy_header, &dummy_header_ptr);

  /* validate the python class */
  if (validate(&dummy_header_ptr, data, have_fn) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummy_ht.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is too long, maximum length is %d",
                error_prefix,
                id,
                (int)sizeof(dummy_ht.idname));
    return NULL;
  }

  if (!(art = region_type_find(reports, dummy_ht.space_type, dummy_ht.region_type))) {
    return NULL;
  }

  /* check if we have registered this header type before, and remove it */
  ht = lib_findstring(&art->headertypes, dummy_ht.idname, offsetof(HeaderType, idname));
  if (ht) {
    ApiStruct *sapi = ht->api_ext.sapi;
    if (!(sapi && api_Header_unregister(main, sapi))) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "%s '%s', bl_idname '%s' %s",
                  error_prefix,
                  id,
                  dummy_ht.idname,
                  sapi ? "is built-in" : "could not be unregistered");
      return NULL;
    }
  }

  if (!api_struct_available_or_report(reports, dummy_ht.idname)) {
    return NULL;
  }
  if (!api_struct_bl_idname_ok_or_report(reports, dummy_ht.idname, "_HT_")) {
    return NULL;
  }

  /* create a new header type */
  ht = mem_mallocn(sizeof(HeaderType), "python buttons header");
  memcpy(ht, &dummy_ht, sizeof(dummy_ht));

  ht->api_ext.sapi = api_def_struct_ptr(&BLENDER_RNA, ht->idname, &RNA_Header);
  ht->api_ext.data = data;
  ht->api_ext.call = call;
  ht->api_ext.free = free;
  api_struct_dune_type_set(ht->api_ext.sapi, ht);

  ht->draw = (have_fn[0]) ? header_draw : NULL
  lib_addtail(&art->headertypes, ht);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);

  return ht->api_ext.sapi;
}

static ApiStruct *api_Header_refine(ApiPtr *htr)
{
  Header *hdr = (Header *)htr->data;
  return (hdr->type && hdr->type->api_ext.sapi) ? hdr->type->rna_ext.srna : &RNA_Header;
}

/* Menu */

static bool menu_poll(const Ctx *C, MenuType *pt)
{
  extern ApiFn api_Menu_poll_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  bool visible;

  api_ptr_create(NULL, pt->api_ext.sapi, NULL, &ptr); /* dummy */
  fn = &api_Menu_poll_fn; /* api_struct_find_fn(&ptr, "poll"); */

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "context", &C);
  pt->api_ext.call((Ctx *)C, &ptr, fn, &list);

  api_param_get_lookup(&list, "visible", &ret);
  visible = *(bool *)ret;

  api_param_list_free(&list);

  return visible;
}

static void menu_draw(const Ctx *C, Menu *menu)
{
  extern ApiFn api_Menu_draw_fn;

  ApiPtr mtr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(&ctx_wm_screen(C)->id, menu->type->rna_ext.srna, menu, &mtr);
  fn = &api_Menu_draw_fn; /* RNA_struct_find_function(&mtr, "draw"); */

  api_param_list_create(&list, &mtr, fn);
  api_param_set_lookup(&list, "context", &C);
  menu->type->api_ext.call((Ctx *)C, &mtr, fx, &list);

  api_param_list_free(&list);
}

static bool api_Menu_unregister(Main *UNUSED(main), ApiStruct *type)
{
  MenuType *mt = api_struct_dune_type_get(type);

  if (!mt) {
    return false;
  }

  api_struct_free_extension(type, &mt->api_ext);
  api_struct_free(&DUNE_API, type);

  wm_menutype_freelink(mt);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);
  return true;
}

static ApiStruct *api_Menu_register(Main *main,
                                    ReportList *reports,
                                    void *data,
                                    const char *id,
                                    StructValidateFn validate,
                                    StructCbFn call,
                                    StructFreeFn free)
{
  const char *error_prefix = "Registering menu class:";
  MenuType *mt, dummy_mt = {NULL};
  Menu dummy_menu = {NULL};
  ApiPtr dummy_menu_ptr;
  bool have_fn[2];
  size_t over_alloc = 0; /* Warning, if this becomes a mess, we better do another allocation. */
  size_t description_size = 0;
  char _menu_descr[API_DYN_DESCR_MAX];

  /* setup dummy menu & menu type to store static properties in */
  dummy_menu.type = &dummy_mt;
  _menu_descr[0] = '\0';
  dummy_menu.type->description = _menu_descr;
  api_ptr_create(NULL, &ApiMenu, &dummy_menu, &dummy_menu_ptr);

  /* We have to set default context! Else we get a void string... */
  strcpy(dummy_mt.translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  /* validate the python class */
  if (validate(&dummy_menu_ptr, data, have_function) != 0) {
    return NULL;
  }

  if (strlen(id) >= sizeof(dummy_mt.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is too long, maximum length is %d",
                error_prefix,
                id,
                (int)sizeof(dummy_mt.idname));
    return NULL;
  }

  /* check if we have registered this menu type before, and remove it */
  mt = wm_menutype_find(dummy_mt.idname, true);
  if (mt) {
    ApiStruct *sapi = mt->api_ext.sapi;
    if (!(sapi && api_Menu_unregisterbmain, sapi))) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "%s '%s', bl_idname '%s' %s",
                  error_prefix,
                  id,
                  dummy_mt.idname,
                  srna ? "is built-in" : "could not be unregistered");
      return NULL;
    }
  }
  if (!api_struct_available_or_report(reports, dummy_mt.idname)) {
    return NULL;
  }
  if (!api_struct_bl_idname_ok_or_report(reports, dummy_mt.idname, "_MT_")) {
    return NULL;
  }

  /* create a new menu type */
  if (_menu_descr[0]) {
    description_size = strlen(_menu_descr) + 1;
    over_alloc += description_size;
  }

  mt = mem_callocn(sizeof(MenuType) + over_alloc, "python buttons menu");
  memcpy(mt, &dummy_mt, sizeof(dummy_mt));

  if (_menu_descr[0]) {
    char *buf = (char *)(mt + 1);
    memcpy(buf, _menu_descr, description_size);
    mt->description = buf;
  }
  else {
    mt->description = NULL;
  }

  mt->api_ext.sapi = api_def_struct_ptr(&DUNE_API, mt->idname, &RNA_Menu);
  api_def_struct_translation_ctx(mt->api_ext.sapi, mt->translation_context);
  mt->api_ext.data = data;
  mt->api_ext.call = call;
  mt->api_ext.free = free;
  api_struct_dune_type_set(mt->api_ext.sapi, mt);
  api_def_struct_flag(mt->api_ext.sapi, STRUCT_NO_IDPROPERTIES);

  mt->poll = (have_fn[0]) ? menu_poll : NULL;
  mt->draw = (have_fn[1]) ? menu_draw : NULL;

  {
    const char *owner_id = api_struct_state_owner_get();
    if (owner_id) {
      STRNCPY(mt->owner_id, owner_id);
    }
  }

  wm_menutype_add(mt);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);

  return mt->api_ext.sapi;
}

static ApiStruct *api_Menu_refine(ApiPtr *mtr)
{
  Menu *menu = (Menu *)mtr->data;
  return (menu->type && menu->type->api_ext.sapi) ? menu->type->api_ext.sapi : &ApiMenu;
}

static void api_Panel_bl_description_set(ApiPtr *ptr, const char *value)
{
  Panel *data = (Panel *)(ptr->data);
  char *str = (char *)data->type->description;
  if (!str[0]) {
    lib_strncpy(str, value, API_DYN_DESCR_MAX); /* utf8 already ensured */
  }
  else {
    lib_assert_msg(0, "setting the bl_description on a non-builtin panel");
  }
}

static void api_Menu_bl_description_set(ApiPtr *ptr, const char *value)
{
  Menu *data = (Menu *)(ptr->data);
  char *str = (char *)data->type->description;
  if (!str[0]) {
    lib_strncpy(str, value, API_DYN_DESCR_MAX); /* utf8 already ensured */
  }
  else {
    lib_assert_msg(0, "setting the bl_description on a non-builtin menu");
  }
}

/* UILayout */

static bool api_UILayout_active_get(ApiPtr *ptr)
{
  return uiLayoutGetActive(ptr->data);
}

static void api_UILayout_active_set(ApiPtr *ptr, bool value)
{
  uiLayoutSetActive(ptr->data, value);
}

static bool api_UILayout_active_default_get(ApiPtr *ptr)
{
  return uiLayoutGetActiveDefault(ptr->data);
}

static void api_UILayout_active_default_set(PointerRNA *ptr, bool value)
{
  uiLayoutSetActiveDefault(ptr->data, value);
}

static bool api_UILayout_activate_init_get(PointerRNA *ptr)
{
  return uiLayoutGetActivateInit(ptr->data);
}

static void api_UILayout_activate_init_set(PointerRNA *ptr, bool value)
{
  uiLayoutSetActivateInit(ptr->data, value);
}

static bool api_UILayout_alert_get(ApiPtr *ptr)
{
  return uiLayoutGetRedAlert(ptr->data);
}

static vna_UILayout_alert_set(ApiPtr *ptr, bool value)
{
  uiLayoutSetRedAlert(ptr->data, value);
}

static void api_UILayout_op_ctx_set(ApiPtr *ptr, int value)
{
  uiLayoutSetOpCtx(ptr->data, value);
}

static int api_UILayout_op_ctx_get(ApiPtr *ptr)
{
  return uiLayoutGetOpCtx(ptr->data);
}

static bool api_UILayout_enabled_get(PointerRNA *ptr)
{
  return uiLayoutGetEnabled(ptr->data);
}

static void api_UILayout_enabled_set(ApiPtr *ptr, bool value)
{
  uiLayoutSetEnabled(ptr->data, value);
}

#  if 0
static int api_UILayout_red_alert_get(ApiPtr *ptr)
{
  return uiLayoutGetRedAlert(ptr->data);
}

static void api_UILayout_red_alert_set(ApiPtr *ptr, bool value)
{
  uiLayoutSetRedAlert(ptr->data, value);
}

static bool api_UILayout_keep_aspect_get(ApiPtr *ptr)
{
  return uiLayoutGetKeepAspect(ptr->data);
}

static void api_UILayout_keep_aspect_set(ApiPtr *ptr, int value)
{
  uiLayoutSetKeepAspect(ptr->data, value);
}
#  endif

static int api_UILayout_alignment_get(ApiPtr *ptr)
{
  return uiLayoutGetAlignment(ptr->data);
}

static void api_UILayout_alignment_set(ApiPtr *ptr, int value)
{
  uiLayoutSetAlignment(ptr->data, value);
}

static int api_UILayout_direction_get(ApiPtr *ptr)
{
  return uiLayoutGetLocalDir(ptr->data);
}

static float api_UILayout_scale_x_get(ApiPtr *ptr)
{
  return uiLayoutGetScaleX(ptr->data);
}

static void api_UILayout_scale_x_set(ApiPtr *ptr, float value)
{
  uiLayoutSetScaleX(ptr->data, value);
}

static float api_UILayout_scale_y_get(ApiPtr *ptr)
{
  return uiLayoutGetScaleY(ptr->data);
}

static void api_UILayout_scale_y_set(ApiPtr *ptr, float value)
{
  uiLayoutSetScaleY(ptr->data, value);
}

static float rna_UILayout_units_x_get(ApiPtr *ptr)
{
  return uiLayoutGetUnitsX(ptr->data);
}

static void api_UILayout_units_x_set(ApiPtr *ptr, float value)
{
  uiLayoutSetUnitsX(ptr->data, value);
}

static float api_UILayout_units_y_get(ApiPtr *ptr)
{
  return uiLayoutGetUnitsY(ptr->data);
}

static void api_UILayout_units_y_set(ApiPtr *ptr, float value)
{
  uiLayoutSetUnitsY(ptr->data, value);
}

static int api_UILayout_emboss_get(ApiPtr *ptr)
{
  return uiLayoutGetEmboss(ptr->data);
}

static void rna_UILayout_emboss_set(ApiPtr *ptr, int value)
{
  uiLayoutSetEmboss(ptr->data, value);
}

static bool rna_UILayout_property_split_get(PointerRNA *ptr)
{
  return uiLayoutGetPropSep(ptr->data);
}

static void rna_UILayout_property_split_set(PointerRNA *ptr, bool value)
{
  uiLayoutSetPropSep(ptr->data, value);
}

static bool rna_UILayout_property_decorate_get(ApiPtr *ptr)
{
  return uiLayoutGetPropDecorate(ptr->data);
}

static void api_UILayout_prop_decorate_set(ApiPtr *ptr, bool value)
{
  uiLayoutSetPropDecorate(ptr->data, value);
}

#else /* API_RUNTIME */

static void api_def_ui_layout(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem alignment_items[] = {
      {UI_LAYOUT_ALIGN_EXPAND, "EXPAND", 0, "Expand", ""},
      {UI_LAYOUT_ALIGN_LEFT, "LEFT", 0, "Left", ""},
      {UI_LAYOUT_ALIGN_CENTER, "CENTER", 0, "Center", ""},
      {UI_LAYOUT_ALIGN_RIGHT, "RIGHT", 0, "Right", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem direction_items[] = {
      {UI_LAYOUT_HORIZONTAL, "HORIZONTAL", 0, "Horizontal", ""},
      {UI_LAYOUT_VERTICAL, "VERTICAL", 0, "Vertical", ""},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem emboss_items[] = {
      {UI_EMBOSS, "NORMAL", 0, "Regular", "Draw standard button emboss style"},
      {UI_EMBOSS_NONE, "NONE", 0, "None", "Draw only text and icons"},
      {UI_EMBOSS_PULLDOWN, "PULLDOWN_MENU", 0, "Pulldown Menu", "Draw pulldown menu style"},
      {UI_EMBOSS_RADIAL, "RADIAL_MENU", 0, "Radial Menu", "Draw radial menu style"},
      {UI_EMBOSS_NONE_OR_STATUS,
       "NONE_OR_STATUS",
       0,
       "None or Status",
       "Draw with no emboss unless the button has a coloring status like an animation state"},
      {0, NULL, 0, NULL, NULL},
  };

  /* layout */

  sapi = api_def_struct(dapi, "UILayout", NULL);
  api_def_struct_stype(sapi, "uiLayout");
  api_def_struct_ui_text(sapi, "UI Layout", "User interface layout in a panel or header");

  prop = api_def_prop(sapi, "active", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_fns(prop, "rna_UILayout_active_get", "rna_UILayout_active_set");

  prop = api_def_prop(sapi, "active_default", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "rna_UILayout_active_default_get", "rna_UILayout_active_default_set");
  api_def_prop_ui_text(
      prop,
      "Active Default",
      "When true, an operator button defined after this will be activated when pressing return"
      "(use with popup dialogs)");

  prop = api_def_prop(sapi, "activate_init", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "rna_UILayout_activate_init_get", "rna_UILayout_activate_init_set");
  api_def_prop_ui_text(
      prop,
      "Activate on Init",
      "When true, buttons defined in popups will be activated on first display "
      "(use so you can type into a field without having to click on it first)");

  prop = api_def_prop(sapi, "op_ctx", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_op_ctx_items);
  api_def_prop_enum_fns(
      prop, "rna_UILayout_op_context_get", "api_UILayout_op_ctx_set", NULL);

  prop = api_def_prop(sapi, "enabled", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_UILayout_enabled_get", "api_UILayout_enabled_set");
  api_def_prop_ui_text(prop, "Enabled", "When false, this (sub)layout is grayed out");

  prop = api_def_prop(sapi, "alert", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_UILayout_alert_get", "api_UILayout_alert_set");

  prop = api_def_prop(sapi, "alignment", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, alignment_items);
  api_def_prop_enum_fns(
      prop, "api_UILayout_alignment_get", "rna_UILayout_alignment_set", NULL);

  prop = api_def_prop(sapi, "direction", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, direction_items);
  api_def_prop_enum_fns(prop, "rna_UILayout_direction_get", NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

#  if 0
  prop = api_def_prop(sapi, "keep_aspect", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(
      prop, "api_UILayout_keep_aspect_get", "api_UILayout_keep_aspect_set");
#  endif

  prop = api_def_prop(sapi, "scale_x", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_float_fns(prop, "rna_UILayout_scale_x_get", "rna_UILayout_scale_x_set", NULL);
  api_def_prop_ui_text(
      prop, "Scale X", "Scale factor along the X for items in this (sub)layout");

  prop = api_def_prop(sapi, "scale_y", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_float_fns(prop, "rna_UILayout_scale_y_get", "rna_UILayout_scale_y_set", NULL);
  api_def_prop_ui_text(
      prop, "Scale Y", "Scale factor along the Y for items in this (sub)layout");

  prop = api_def_prop(sapi, "ui_units_x", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_float_fns(prop, "api_UILayout_units_x_get", "rna_UILayout_units_x_set", NULL);
  api_def_prop_ui_text(
      prop, "Units X", "Fixed size along the X for items in this (sub)layout");

  prop = api_def_prop(sapi, "ui_units_y", PROP_FLOAT, PROP_UNSIGNED);
  api_def_prop_float_fns(prop, "rna_UILayout_units_y_get", "rna_UILayout_units_y_set", NULL);
  api_def_prop_ui_text(
      prop, "Units Y", "Fixed size along the Y for items in this (sub)layout");
  api_api_ui_layout(srna);

  prop = api_def_prop(sapi, "emboss", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, emboss_items);
  RNA_def_property_enum_funcs(prop, "rna_UILayout_emboss_get", "rna_UILayout_emboss_set", NULL);

  prop = RNA_def_property(srna, "use_property_split", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_UILayout_property_split_get", "rna_UILayout_property_split_set");

  prop = RNA_def_property(srna, "use_property_decorate", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(
      prop, "rna_UILayout_property_decorate_get", "rna_UILayout_property_decorate_set");
}

static void rna_def_panel(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  PropertyRNA *parm;
  FunctionRNA *func;

  static const EnumPropertyItem panel_flag_items[] = {
      {PANEL_TYPE_DEFAULT_CLOSED,
       "DEFAULT_CLOSED",
       0,
       "Default Closed",
       "Defines if the panel has to be open or collapsed at the time of its creation"},
      {PANEL_TYPE_NO_HEADER,
       "HIDE_HEADER",
       0,
       "Hide Header",
       "If set to False, the panel shows a header, which contains a clickable "
       "arrow to collapse the panel and the label (see bl_label)"},
      {PANEL_TYPE_INSTANCED,
       "INSTANCED",
       0,
       "Instanced Panel",
       "Multiple panels with this type can be used as part of a list depending on data external "
       "to the UI. Used to create panels for the modifiers and other stacks"},
      {PANEL_TYPE_HEADER_EXPAND,
       "HEADER_LAYOUT_EXPAND",
       0,
       "Expand Header Layout",
       "Allow buttons in the header to stretch and shrink to fill the entire layout width"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "Panel", NULL);
  RNA_def_struct_ui_text(srna, "Panel", "Panel containing UI elements");
  RNA_def_struct_sdna(srna, "Panel");
  RNA_def_struct_refine_func(srna, "rna_Panel_refine");
  RNA_def_struct_register_funcs(srna, "rna_Panel_register", "rna_Panel_unregister", NULL);
  RNA_def_struct_translation_context(srna, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  RNA_def_struct_flag(srna, STRUCT_PUBLIC_NAMESPACE_INHERIT);

  /* poll */
  func = RNA_def_function(srna, "poll", NULL);
  RNA_def_function_ui_description(
      func, "If this method returns a non-null output, then the panel can be drawn");
  RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_REGISTER_OPTIONAL);
  RNA_def_function_return(func, RNA_def_boolean(func, "visible", 1, "", ""));
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* draw */
  func = RNA_def_function(srna, "draw", NULL);
  RNA_def_function_ui_description(func, "Draw UI elements into the panel UI layout");
  RNA_def_function_flag(func, FUNC_REGISTER);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  func = RNA_def_function(srna, "draw_header", NULL);
  RNA_def_function_ui_description(func, "Draw UI elements into the panel's header UI layout");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  func = RNA_def_function(srna, "draw_header_preset", NULL);
  RNA_def_function_ui_description(func, "Draw UI elements for presets in the panel's header");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  prop = RNA_def_property(srna, "layout", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "UILayout");
  RNA_def_property_ui_text(prop, "Layout", "Defines the structure of the panel in the UI");

  prop = RNA_def_property(srna, "text", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "drawname");
  RNA_def_property_ui_text(prop, "Text", "XXX todo");

  prop = RNA_def_property(srna, "custom_data", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "Constraint");
  RNA_def_property_pointer_sdna(prop, NULL, "runtime.custom_data_ptr");
  RNA_def_property_pointer_funcs(
      prop, "rna_Panel_custom_data_get", NULL, "rna_Panel_custom_data_typef", NULL);
  RNA_def_property_ui_text(prop, "Custom Data", "Panel data");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  /* registration */
  prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->idname");
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop,
                           "ID Name",
                           "If this is set, the panel gets a custom ID, otherwise it takes the "
                           "name of the class used to define the panel. For example, if the "
                           "class name is \"OBJECT_PT_hello\", and bl_idname is not set by the "
                           "script, then bl_idname = \"OBJECT_PT_hello\"");

  prop = RNA_def_property(srna, "bl_label", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->label");
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop,
                           "Label",
                           "The panel label, shows up in the panel header at the right of the "
                           "triangle used to collapse the panel");

  prop = RNA_def_property(srna, "bl_translation_context", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->translation_context");
  RNA_def_property_string_default(prop, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop,
                           "",
                           "Specific translation context, only define when the label needs to be "
                           "disambiguated from others using the exact same label");

  RNA_define_verify_sdna(true);

  prop = RNA_def_property(srna, "bl_description", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->description");
  RNA_def_property_string_maxlength(prop, RNA_DYN_DESCR_MAX); /* else it uses the pointer size! */
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_Panel_bl_description_set");
  // RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_clear_flag(prop, PROP_NEVER_NULL); /* check for NULL */
  RNA_def_property_ui_text(prop, "", "The panel tooltip");

  prop = RNA_def_property(srna, "bl_category", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->category");
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop, "", "The category (tab) in which the panel will be displayed, when applicable");

  prop = RNA_def_property(srna, "bl_owner_id", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->owner_id");
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop, "", "The ID owning the data displayed in the panel, if any");

  prop = RNA_def_property(srna, "bl_space_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "type->space_type");
  RNA_def_property_enum_items(prop, rna_enum_space_type_items);
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop, "Space Type", "The space where the panel is going to be used in");

  prop = RNA_def_property(srna, "bl_region_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "type->region_type");
  RNA_def_property_enum_items(prop, rna_enum_region_type_items);
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(
      prop, "Region Type", "The region where the panel is going to be used in");

  prop = RNA_def_property(srna, "bl_context", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->context");
  RNA_def_property_flag(
      prop, PROP_REGISTER_OPTIONAL); /* Only used in Properties Editor and 3D View - Thomas */
  RNA_def_property_ui_text(prop,
                           "Context",
                           "The context in which the panel belongs to. (TODO: explain the "
                           "possible combinations bl_context/bl_region_type/bl_space_type)");

  prop = RNA_def_property(srna, "bl_options", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "type->flag");
  RNA_def_property_enum_items(prop, panel_flag_items);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL | PROP_ENUM_FLAG);
  RNA_def_property_ui_text(prop, "Options", "Options for this panel type");

  prop = RNA_def_property(srna, "bl_parent_id", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->parent_id");
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop, "Parent ID Name", "If this is set, the panel becomes a sub-panel");

  prop = RNA_def_property(srna, "bl_ui_units_x", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "type->ui_units_x");
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop, "Units X", "When set, defines popup panel width");

  prop = RNA_def_property(srna, "bl_order", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_int_sdna(prop, NULL, "type->order");
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(
      prop,
      "Order",
      "Panels with lower numbers are default ordered before panels with higher numbers");

  prop = RNA_def_property(srna, "use_pin", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", PNL_PIN);
  RNA_def_property_ui_text(prop, "Pin", "Show the panel on all tabs");
  /* XXX, should only tag region for redraw */
  RNA_def_property_update(prop, NC_WINDOW, NULL);

  prop = RNA_def_property(srna, "is_popover", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", PNL_POPOVER);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Popover", "");
}

static void rna_def_uilist(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  PropertyRNA *parm;
  FunctionRNA *func;

  srna = RNA_def_struct(brna, "UIList", NULL);
  RNA_def_struct_ui_text(srna, "UIList", "UI list containing the elements of a collection");
  RNA_def_struct_sdna(srna, "uiList");
  RNA_def_struct_refine_func(srna, "rna_UIList_refine");
  RNA_def_struct_register_funcs(srna, "rna_UIList_register", "rna_UIList_unregister", NULL);
  RNA_def_struct_idprops_func(srna, "rna_UIList_idprops");
  RNA_def_struct_flag(srna, STRUCT_NO_DATABLOCK_IDPROPERTIES | STRUCT_PUBLIC_NAMESPACE_INHERIT);

  /* Registration */
  prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->idname");
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop,
                           "ID Name",
                           "If this is set, the uilist gets a custom ID, otherwise it takes the "
                           "name of the class used to define the uilist (for example, if the "
                           "class name is \"OBJECT_UL_vgroups\", and bl_idname is not set by the "
                           "script, then bl_idname = \"OBJECT_UL_vgroups\")");

  /* Data */
  /* Note that this is the "non-full" list-ID as obtained through #WM_uilisttype_list_id_get(),
   * which differs from the (internal) `uiList.list_id`. */
  prop = RNA_def_property(srna, "list_id", PROP_STRING, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_string_funcs(prop, "rna_UIList_list_id_get", "rna_UIList_list_id_length", NULL);
  RNA_def_property_ui_text(prop,
                           "List Name",
                           "Identifier of the list, if any was passed to the \"list_id\" "
                           "parameter of \"template_list()\"");

  prop = RNA_def_property(srna, "layout_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_uilist_layout_type_items);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  /* Filter options */
  prop = RNA_def_property(srna, "use_filter_show", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "filter_flag", UILST_FLT_SHOW);
  RNA_def_property_ui_text(prop, "Show Filter", "Show filtering options");

  prop = RNA_def_property(srna, "filter_name", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "filter_byname");
  RNA_def_property_flag(prop, PROP_TEXTEDIT_UPDATE);
  RNA_def_property_ui_text(
      prop, "Filter by Name", "Only show items matching this name (use '*' as wildcard)");

  prop = RNA_def_property(srna, "use_filter_invert", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "filter_flag", UILST_FLT_EXCLUDE);
  RNA_def_property_ui_text(prop, "Invert", "Invert filtering (show hidden items, and vice versa)");

  /* WARNING: This is sort of an abuse, sort-by-alpha is actually a value,
   * should even be an enum in full logic (of two values, sort by index and sort by name).
   * But for default UIList, it's nicer (better UI-wise) to show this as a boolean bit-flag option,
   * avoids having to define custom setters/getters using UILST_FLT_SORT_MASK to mask out
   * actual bitflags on same var, etc.
   */
  prop = RNA_def_property(srna, "use_filter_sort_alpha", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "filter_sort_flag", UILST_FLT_SORT_ALPHA);
  RNA_def_property_ui_icon(prop, ICON_SORTALPHA, 0);
  RNA_def_property_ui_text(prop, "Sort by Name", "Sort items by their name");

  prop = RNA_def_property(srna, "use_filter_sort_reverse", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "filter_sort_flag", UILST_FLT_SORT_REVERSE);
  RNA_def_property_ui_text(prop, "Reverse", "Reverse the order of shown items");

  prop = RNA_def_property(srna, "use_filter_sort_lock", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "filter_sort_flag", UILST_FLT_SORT_LOCK);
  RNA_def_property_ui_text(
      prop, "Lock Order", "Lock the order of shown items (user cannot change it)");

  /* draw_item */
  func = RNA_def_function(srna, "draw_item", NULL);
  RNA_def_function_ui_description(
      func,
      "Draw an item in the list (NOTE: when you define your own draw_item "
      "function, you may want to check given 'item' is of the right type...)");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "layout", "UILayout", "", "Layout to draw the item");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_pointer(
      func, "data", "AnyType", "", "Data from which to take Collection property");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED | PARM_RNAPTR);
  parm = RNA_def_pointer(func, "item", "AnyType", "", "Item of the collection property");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED | PARM_RNAPTR);
  parm = RNA_def_int(
      func, "icon", 0, 0, INT_MAX, "", "Icon of the item in the collection", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func,
                         "active_data",
                         "AnyType",
                         "",
                         "Data from which to take property for the active element");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  parm = RNA_def_string(func,
                        "active_property",
                        NULL,
                        0,
                        "",
                        "Identifier of property in active_data, for the active element");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  RNA_def_int(func, "index", 0, 0, INT_MAX, "", "Index of the item in the collection", 0, INT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED | PARM_PYFUNC_OPTIONAL);
  prop = RNA_def_property(func, "flt_flag", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(prop, "", "The filter-flag result for this item");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED | PARM_PYFUNC_OPTIONAL);

  /* draw_filter */
  func = RNA_def_function(srna, "draw_filter", NULL);
  RNA_def_function_ui_description(func, "Draw filtering options");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "layout", "UILayout", "", "Layout to draw the item");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  /* filter */
  func = RNA_def_function(srna, "filter_items", NULL);
  RNA_def_function_ui_description(
      func,
      "Filter and/or re-order items of the collection (output filter results in "
      "filter_flags, and reorder results in filter_neworder arrays)");
  RNA_def_function_flag(func, FUNC_REGISTER_OPTIONAL);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(
      func, "data", "AnyType", "", "Data from which to take Collection property");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED | PARM_RNAPTR);
  parm = RNA_def_string(
      func, "property", NULL, 0, "", "Identifier of property in data, for the collection");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  prop = RNA_def_property(func, "filter_flags", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_flag(prop, PARM_REQUIRED | PROP_DYNAMIC);
  RNA_def_property_array(prop, 1); /* XXX Dummy value, default 0 does not work */
  RNA_def_property_ui_text(
      prop,
      "",
      "An array of filter flags, one for each item in the collection (NOTE: "
      "FILTER_ITEM bit is reserved, it defines whether the item is shown or not)");
  RNA_def_function_output(func, prop);
  prop = RNA_def_property(func, "filter_neworder", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_flag(prop, PARM_REQUIRED | PROP_DYNAMIC);
  RNA_def_property_array(prop, 1); /* XXX Dummy value, default 0 does not work */
  RNA_def_property_ui_text(
      prop,
      "",
      "An array of indices, one for each item in the collection, mapping the org "
      "index to the new one");
  RNA_def_function_output(func, prop);

  /* "Constants"! */
  RNA_define_verify_sdna(0); /* not in sdna */

  prop = RNA_def_property(srna, "bitflag_filter_item", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(
      prop,
      "FILTER_ITEM",
      "The value of the reserved bitflag 'FILTER_ITEM' (in filter_flags values)");
  RNA_def_property_int_funcs(prop, "rna_UIList_filter_const_FILTER_ITEM_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
}

static void rna_def_header(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  PropertyRNA *parm;
  FunctionRNA *func;

  srna = RNA_def_struct(brna, "Header", NULL);
  RNA_def_struct_ui_text(srna, "Header", "Editor header containing UI elements");
  RNA_def_struct_sdna(srna, "Header");
  RNA_def_struct_refine_func(srna, "rna_Header_refine");
  RNA_def_struct_register_funcs(srna, "rna_Header_register", "rna_Header_unregister", NULL);
  RNA_def_struct_flag(srna, STRUCT_PUBLIC_NAMESPACE_INHERIT);

  /* draw */
  func = RNA_def_function(srna, "draw", NULL);
  RNA_def_function_ui_description(func, "Draw UI elements into the header UI layout");
  RNA_def_function_flag(func, FUNC_REGISTER);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  RNA_define_verify_sdna(0); /* not in sdna */

  prop = RNA_def_property(srna, "layout", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "layout");
  RNA_def_property_struct_type(prop, "UILayout");
  RNA_def_property_ui_text(prop, "Layout", "Structure of the header in the UI");

  /* registration */
  prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->idname");
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop,
                           "ID Name",
                           "If this is set, the header gets a custom ID, otherwise it takes the "
                           "name of the class used to define the panel; for example, if the "
                           "class name is \"OBJECT_HT_hello\", and bl_idname is not set by the "
                           "script, then bl_idname = \"OBJECT_HT_hello\"");

  prop = RNA_def_property(srna, "bl_space_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "type->space_type");
  RNA_def_property_enum_items(prop, rna_enum_space_type_items);
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(
      prop, "Space Type", "The space where the header is going to be used in");

  prop = RNA_def_property(srna, "bl_region_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "type->region_type");
  RNA_def_property_enum_default(prop, RGN_TYPE_HEADER);
  RNA_def_property_enum_items(prop, rna_enum_region_type_items);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_ui_text(prop,
                           "Region Type",
                           "The region where the header is going to be used in "
                           "(defaults to header region)");

  RNA_define_verify_sdna(1);
}

static void rna_def_menu(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  PropertyRNA *parm;
  FunctionRNA *func;

  srna = RNA_def_struct(brna, "Menu", NULL);
  RNA_def_struct_ui_text(srna, "Menu", "Editor menu containing buttons");
  RNA_def_struct_sdna(srna, "Menu");
  RNA_def_struct_refine_func(srna, "rna_Menu_refine");
  RNA_def_struct_register_funcs(srna, "rna_Menu_register", "rna_Menu_unregister", NULL);
  RNA_def_struct_translation_context(srna, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  RNA_def_struct_flag(srna, STRUCT_PUBLIC_NAMESPACE_INHERIT);

  /* poll */
  func = RNA_def_function(srna, "poll", NULL);
  RNA_def_function_ui_description(
      func, "If this method returns a non-null output, then the menu can be drawn");
  RNA_def_function_flag(func, FUNC_NO_SELF | FUNC_REGISTER_OPTIONAL);
  RNA_def_function_return(func, RNA_def_boolean(func, "visible", 1, "", ""));
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  /* draw */
  func = RNA_def_function(srna, "draw", NULL);
  RNA_def_function_ui_description(func, "Draw UI elements into the menu UI layout");
  RNA_def_function_flag(func, FUNC_REGISTER);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);

  RNA_define_verify_sdna(false); /* not in sdna */

  prop = RNA_def_property(srna, "layout", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "layout");
  RNA_def_property_struct_type(prop, "UILayout");
  RNA_def_property_ui_text(prop, "Layout", "Defines the structure of the menu in the UI");

  /* registration */
  prop = RNA_def_property(srna, "bl_idname", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->idname");
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop,
                           "ID Name",
                           "If this is set, the menu gets a custom ID, otherwise it takes the "
                           "name of the class used to define the menu (for example, if the "
                           "class name is \"OBJECT_MT_hello\", and bl_idname is not set by the "
                           "script, then bl_idname = \"OBJECT_MT_hello\")");

  prop = RNA_def_property(srna, "bl_label", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->label");
  RNA_def_property_flag(prop, PROP_REGISTER);
  RNA_def_property_ui_text(prop, "Label", "The menu label");

  prop = RNA_def_property(srna, "bl_translation_context", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->translation_context");
  RNA_def_property_string_default(prop, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);

  prop = RNA_def_property(srna, "bl_description", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->description");
  RNA_def_property_string_maxlength(prop, RNA_DYN_DESCR_MAX); /* else it uses the pointer size! */
  RNA_def_property_string_funcs(prop, NULL, NULL, "rna_Menu_bl_description_set");
  // RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);
  RNA_def_property_clear_flag(prop, PROP_NEVER_NULL); /* check for NULL */

  prop = RNA_def_property(srna, "bl_owner_id", PROP_STRING, PROP_NONE);
  RNA_def_property_string_sdna(prop, NULL, "type->owner_id");
  RNA_def_property_flag(prop, PROP_REGISTER_OPTIONAL);

  RNA_define_verify_sdna(1);
}

void RNA_def_ui(BlenderRNA *brna)
{
  rna_def_ui_layout(brna);
  rna_def_panel(brna);
  rna_def_uilist(brna);
  rna_def_header(brna);
  rna_def_menu(brna);
}

#endif /* RNA_RUNTIME */
