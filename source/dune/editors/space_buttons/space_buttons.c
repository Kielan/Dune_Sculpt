/** spbuttons **/

#include <stdio.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_bitmap.h"
#include "lib_dunelib.h"
#include "lib_utildefines.h"

#include "dune_context.h"
#include "dune_pen_mod.h" /* Types for registering panels. */
#include "dune_lib_remap.h"
#include "dune_modifier.h"
#include "dune_screen.h"
#include "dune_shader_fx.h"

#include "ed_buttons.h"
#include "ed_screen.h"
#include "ed_space_api.h"
#include "ed_view3d.h" /* To draw toolbar UI. */

#include "wm_api.h"
#include "wm_message.h"
#include "wm_types.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "ui_interface.h"
#include "ui_resources.h"

#include "btns_intern.h" /* own include */

/* -------------------------------------------------------------------- */
/** Default Callbacks for Properties Space **/

static SpaceLink *btns_create(const ScrArea *UNUSED(area), const Scene *UNUSED(scene))
{
  ARegion *region;
  SpaceProps *sbtns;

  sbtns = mem_callocn(sizeof(SpaceProps), "initbuts");
  sbtns->spacetype = SPACE_PROPS;

  sbtns->maind = sbtns->mainduser = DCTX_OBJECT;

  /* header */
  region = mem_callocn(sizeof(ARegion), "header for btns");

  lib_addtail(&sbtns->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* navigation bar */
  region = mem_callocn(sizeof(ARegion), "navigation bar for btns");

  lib_addtail(&sbtns->regionbase, region);
  region->regiontype = RGN_TYPE_NAV_BAR;
  region->alignment = RGN_ALIGN_LEFT;

#if 0
  /* context region */
  region = mem_callocn(sizeof(ARegion), "context region for buts");
  lib_addtail(&sbtns->regionbase, region);
  region->regiontype = RGN_TYPE_CHANNELS;
  region->alignment = RGN_ALIGN_TOP;
#endif

  /* main region */
  region = mem_callocn(sizeof(ARegion), "main region for buts");

  lib_addtail(&sbtns->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  return (SpaceLink *)sbtns;
}

/* not spacelink itself */
static void btns_free(SpaceLink *sl)
{
  SpaceProps *sbtns = (SpaceProps *)sl;

  if (sbtns->path) {
    mem_freen(sbtsn->path);
  }

  if (sbtns->texuser) {
    BtnsCtxTexture *ct = sbtns->texuser;
    lib_freelistn(&ct->users);
    mem_freen(ct);
  }

  if (sbtns->runtime != NULL) {
    MEM_SAFE_FREE(sbtns->runtime->tab_search_results);
    mem_freen(sbtns->runtime);
  }
}

/* spacetype; init callback */
static void btns_init(struct wmWindowManager *UNUSED(wm), ScrArea *area)
{
  SpaceProperties *sbtns = (SpaceProps *)area->spacedata.first;

  if (sbtns->runtime == NULL) {
    sbtns->runtime = mem_mallocn(sizeof(SpaceProps_Runtime), __func__);
    sbtns->runtime->search_string[0] = '\0';
    sbtns->runtime->tab_search_results = LIB_BITMAP_NEW(CTX_TOT * 2, __func__);
  }
}

static SpaceLink *btns_duplicate(SpaceLink *sl)
{
  SpaceProps *sfile_old = (SpaceProps *)sl;
  SpaceProps *sbtnsn = mem_dupallocn(sl);

  /* clear or remove stuff from old */
  sbtnsn->path = NULL;
  sbtnsn->texuser = NULL;
  if (sfile_old->runtime != NULL) {
    sbutsn->runtime = mem_dupallocn(sfile_old->runtime);
    sbutsn->runtime->search_string[0] = '\0';
    sbutsn->runtime->tab_search_results = LIB_BITMAP_NEW(DCTX_TOT, __func__);
  }

  return (SpaceLink *)sbutsn;
}

/* add handlers, stuff you only do once or on area/region changes */
static void btns_main_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  ed_region_panels_init(wm, region);

  keymap = wm_keymap_ensure(wm->defaultconf, "Property Editor", SPACE_PROPS, 0);
  wm_event_add_keymap_handler(&region->handlers, keymap);
}

/* -------------------------------------------------------------------- */
/** Property Editor Layout **/

int ed_btns_tabs_list(SpaceProps *sbtns, short *ctx_tabs_array)
{
  int length = 0;
  if (sbtns->pathflag & (1 << DCTX_TOOL)) {
    ctx_tabs_array[length] = DCTX_TOOL;
    length++;
  }
  if (length != 0) {
    ctx_tabs_array[length] = -1;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_RENDER)) {
    ctx_tabs_array[length] = DCTX_RENDER;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_OUTPUT)) {
    ctx_tabs_array[length] = DCTX_OUTPUT;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_VIEW_LAYER)) {
    ctx_tabs_array[length] = DCTX_VIEW_LAYER;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_SCENE)) {
    ctx_tabs_array[length] = DCTX_SCENE;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_WORLD)) {
    ctx_tabs_array[length] = DCTX_WORLD;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_COLLECTION)) {
    if (length != 0) {
      ctx_tabs_array[length] = -1;
      length++;
    }
    ctx_tabs_array[length] = DCTX_COLLECTION;
    length++;
  }
  if (length != 0) {
    ctx_tabs_array[length] = -1;
    length++;
  }
  if (sbtns->pathflag & (1 << DTX_OBJECT)) {
    ctx_tabs_array[length] = DCTX_OBJECT;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_MODIFIER)) {
    ctx_tabs_array[length] = DCTX_MODIFIER;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTX_SHADERFX)) {
    ctx_tabs_array[length] = DCTX_SHADERFX;
    length++;
  }
  if (sbtns->pathflag & (1 << DCXT_PARTICLE)) {
    context_tabs_array[length] = DCTX_PARTICLE;
    length++;
  }
  if (sbuts->pathflag & (1 << CTX_PHYSICS)) {
    ctx_tabs_array[length] = DCTX_PHYSICS;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTXT_CONSTRAINT)) {
    ctx_tabs_array[length] = DCTXT_CONSTRAINT;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTXT_DATA)) {
    ctxt_tabs_array[length] = DCTXT_DATA;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTXT_BONE)) {
    ctxt_tabs_array[length] = DCTXT_BONE;
    length++;
  
  if (sbtns->pathflag & (1 << DCTXT_BONE_CONSTRAINT)) {
    ctxt_tabs_array[length] = DCTXT_BONE_CONSTRAINT;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTXT_MATERIAL)) {
    ctxt_tabs_array[length] = DCTXT_MATERIAL;
    length++;
  }
  if (length != 0) {
    ctxt_tabs_array[length] = -1;
    length++;
  }
  if (sbtns->pathflag & (1 << DCTXT_TEXTURE)) {
    context_tabs_array[length] = DCTXT_TEXTURE;
    length++;
  }

  return length;
}

static const char *btns_main_region_ctx_string(const short maind)
{
  switch (maind) {
    case DCTXT_SCENE:
      return "scene";
    case DCTXT_RENDER:
      return "render";
    case DCTXT_OUTPUT:
      return "output";
    case DCTXT_VIEW_LAYER:
      return "view_layer";
    case DCTXT_WORLD:
      return "world";
    case DCTXT_COLLECTION:
      return "collection";
    case BCTXT_OBJECT:
      return "object";
    case DCTXT_DATA:
      return "data";
    case CONTEXT_MATERIAL:
      return "material";
    case CONTEXT_TEXTURE:
      return "texture";
    case CONTEXT_PARTICLE:
      return "particle";
    case CONTEXT_PHYSICS:
      return "physics";
    case CONTEXT_BONE:
      return "bone";
    case CONTEXT_MODIFIER:
      return "modifier";
    case CONTEXT_SHADERFX:
      return "shaderfx";
    case CONTEXT_CONSTRAINT:
      return "constraint";
    case CONTEXT_BONE_CONSTRAINT:
      return "bone_constraint";
    case CONTEXT_TOOL:
      return "tool";
  }

  /* All the cases should be handled. */
  lib_assert(false);
  return "";
}

static void btnns_main_region_layout_props(const dContext *C,
                                                  SpaceProps *sbtns,
                                                  ARegion *region)
{
  btns_ctx_compute(C, sbtns);

  const char *contexts[2] = {btns_main_region_ctx_string(sbuts->maind), NULL};

  ed_region_panels_layout_ex(C, region, &region->type->paneltypes, ctxts, NULL);
}

/* -------------------------------------------------------------------- */
/** Property Search Access API **/

const char *ed_btns_search_string_get(SpaceProps *sbtns)
{
  return sbtns->runtime->search_string;
}

int ed_btns_search_string_length(struct SpaceProps *sbtns)
{
  return lib_strnlen(sbtns->runtime->search_string, sizeof(sbtns->runtime->search_string));
}

void ed_btns_search_string_set(SpaceProps *sbtns, const char *value)
{
  lib_strncpy(sbtbs->runtime->search_string, value, sizeof(sbtns->runtime->search_string));
}

bool ED_btns_tab_has_search_result(SpaceProps *sbtns, const int index)
{
  return LIB_BITMAP_TEST(sbtns->runtime->tab_search_results, index);
}

/* -------------------------------------------------------------------- */
/** "Off Screen" Layout Generation for Property Search **/

static bool prop_search_for_ctx(const dContext *C, ARegion *region, SpaceProps *sbtns)
{
  const char *contexts[2] = {bts_main_region_ctx_string(sbtns->maind), NULL};

  if (sbtns->maind == DCTXT_TOOL) {
    return false;
  }

  btns_ctx_compute(C, sbtns);
  return ED_region_prop_search(C, region, &region->type->paneltypes, contexts, NULL);
}

static void prop_search_move_to_next_tab_with_results(SpaceProps *sbtns,
                                                          const short *ctx_tabs_array,
                                                          const int tabs_len)
{
  /* As long as all-tab search in the tool is disabled in the tool context, don't move from it. */
  if (sbtns->maind == DCTX_TOOL) {
    return;
  }

  int current_tab_index = 0;
  for (int i = 0; i < tabs_len; i++) {
    if (sbtns->maind == ctx_tabs_array[i]) {
      current_tab_index = i;
      break;
    }
  }

  /* Try the tabs after the current tab. */
  for (int i = current_tab_index; i < tabs_len; i++) {
    if (LIB_BITMAP_TEST(sbtns->runtime->tab_search_results, i)) {
      sbtns->mainduser = ctx_tabs_array[i];
      return;
    }
  }

  /* Try the tabs before the current tab. */
  for (int i = 0; i < current_tab_index; i++) {
    if (LIB_BITMAP_TEST(sbtns->runtime->tab_search_results, i)) {
      sbtns->maiduser = ctx_tabs_array[i];
      return;
    }
  }
}

static void prop_search_all_tabs(const dContext *C,
                                     SpaceProps *sbtns,
                                     ARegion *region_original,
                                     const short *ctx_tabs_array,
                                     const int tabs_len)
{
  /* Use local copies of the area and duplicate the region as a mainly-paranoid protection
   * against changing any of the space / region data while running the search. */
  ScrArea *area_original = ctx_wm_area(C);
  ScrArea area_copy = *area_original;
  ARegion *region_copy = dune_area_region_copy(area_copy.type, region_original);
  /* Set the region visible field. Otherwise some layout code thinks we're drawing in a popup.
   * This likely isn't necessary, but it's nice to emulate a "real" region where possible. */
  region_copy->visible = true;
  ctx_wm_area_set((dContext *)C, &area_copy);
  ctx_wm_region_set((dContext *)C, region_copy);

  SpaceProperties sbtns_copy = *sbtns;
  sbtns_copy.path = NULL;
  sbtns_copy.texuser = NULL;
  sbtns_copy.runtime = MEM_dupallocN(sbtns->runtime);
  sbtns_copy.runtime->tab_search_results = NULL;
  lib_listbase_clear(&area_copy.spacedata);
  lib_addtail(&area_copy.spacedata, &sbtns_copy);

  /* Loop through the tabs added to the properties editor. */
  for (int i = 0; i < tabs_len; i++) {
    /* -1 corresponds to a spacer. */
    if (ctx_tabs_array[i] == -1) {
      continue;
    }

    /* Handle search for the current tab in the normal layout pass. */
    if (ctx_tabs_array[i] == sbtns->maind) {
      continue;
    }

    sbtns_copy.maind = sbtns_copy.maindo = sbtns_copy.mainduser = ctx_tabs_array[i];

    /* Actually do the search and store the result in the bitmap. */
    LIB_BITMAP_SET(sbtns->runtime->tab_search_results,
                   i,
                   props_search_for_ctx(C, region_copy, &sbtns_copy));

    UI_blocklist_free(C, region_copy);
  }

  dune_area_region_free(area_copy.type, region_copy);
  MEM_freeN(region_copy);
  btns_free((SpaceLink *)&sbtns_copy);

  ctx_wm_area_set((dContext *)C, area_original);
  ctx_wm_region_set((dContext *)C, region_original);
}

/**
 * Handle property search for the layout pass, including finding which tabs have
 * search results and switching if the current tab doesn't have a result.
 */
static void btns_main_region_prop_search(const dContext *C,
                                                SpaceProps *sbtns,
                                                ARegion *region)
{
  /* Theoretical maximum of every context shown with a spacer between every tab. */
  short ctx_tabs_array[DCTXT_TOT * 2];
  int tabs_len = ED_btns_tabs_list(sbtns, ctx_tabs_array);

  prop_search_all_tabs(C, sbtns, region, ctx_tabs_array, tabs_len);

  /* Check whether the current tab has a search match. */
  bool current_tab_has_search_match = false;
  LISTBASE_FOREACH (Panel *, panel, &region->panels) {
    if (UI_panel_is_active(panel) && UI_panel_matches_search_filter(panel)) {
      current_tab_has_search_match = true;
    }
  }

  /* Find which index in the list the current tab corresponds to. */
  int current_tab_index = -1;
  for (int i = 0; i < tabs_len; i++) {
    if (context_tabs_array[i] == sbtns->maind) {
      current_tab_index = i;
    }
  }
  lib_assert(current_tab_index != -1);

  /* Update the tab search match flag for the current tab. */
  LIB_BITMAP_SET(
      sbtns->runtime->tab_search_results, current_tab_index, current_tab_has_search_match);

  /* Move to the next tab with a result */
  if (!current_tab_has_search_match) {
    if (region->flag & RGN_FLAG_SEARCH_FILTER_UPDATE) {
      prop_search_move_to_next_tab_with_results(sbtns, ctx_tabs_array, tabs_len);
    }
  }
}

/* -------------------------------------------------------------------- */
/** Main Region Layout and Listener **/

static void btns_main_region_layout(const dContext *C, ARegion *region)
{
  /* draw entirely, view changes should be handled here */
  SpaceProps *sbuts = ctx_wm_space_props(C);

  if (sbtns->maind == DCTX_TOOL) {
    ED_view3d_btns_region_layout_ex(C, region, "Tool");
  }
  else {
    btns_main_region_layout_props(C, sbtns, region);
  }

  if (region->flag & RGN_FLAG_SEARCH_FILTER_ACTIVE) {
    btns_main_region_prop_search(C, sbtns, region);
  }

  sbtns->maindo = sbtns->maind;
}

static void btns_main_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_SCREEN:
      if (ELEM(wmn->data, ND_LAYER)) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

static void btns_operatortypes(void)
{
  WM_operatortype_append(BUTTONS_OT_start_filter);
  WM_operatortype_append(BUTTONS_OT_clear_filter);
  WM_operatortype_append(BUTTONS_OT_toggle_pin);
  WM_operatortype_append(BUTTONS_OT_context_menu);
  WM_operatortype_append(BUTTONS_OT_file_browse);
  WM_operatortype_append(BUTTONS_OT_directory_browse);
}

static void buttons_keymap(struct wmKeyConfig *keyconf)
{
  WM_keymap_ensure(keyconf, "Property Editor", SPACE_PROPERTIES, 0);
}

/* -------------------------------------------------------------------- */
/** Header Region Callbacks **/

/* add handlers, stuff you only do once or on area/region changes */
static void btns_header_region_init(wmWindowManager *UNUSED(wm), ARegion *region)
{
  ED_region_header_init(region);
}

static void btns_header_region_draw(const dContext *C, ARegion *region)
{
  SpaceProps *sbtns = ctx_wm_space_props(C);

  /* Needed for API to get the good values! */
  btns_ctx_compute(C, sbuts);

  ED_region_header(C, region);
}

static void btns_header_region_message_subscribe(const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  ScrArea *area = params->area;
  ARegion *region = params->region;
  SpaceProps *sbtns = area->spacedata.first;

  wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
      .owner = region,
      .user_data = region,
      .notify = ED_region_do_msg_notify_tag_redraw,
  };

  /* Don't check for SpaceProps.maind here, we may toggle between view-layers
   * where one has no active object, so that available contexts changes. */
  wm_msg_subscribe_api_anon_prop(mbus, Window, view_layer, &msg_sub_value_region_tag_redraw);

  if (!ELEM(sbtns->maind, DCTX_RENDER, DCTX_OUTPUT, DCTX_SCENE, DCTX_WORLD)) {
    wm_msg_subscribe_api_anon_prop(mbus, ViewLayer, name, &msg_sub_value_region_tag_redraw);
  }

  if (sbtns->maind == DCTX_TOOL) {
    wm_msg_subscribe_api_anon_prop(mbus, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
  }
}

/* -------------------------------------------------------------------- */
/** Navigation Region Callbacks **/

static void btns_navigation_bar_region_init(wmWindowManager *wm, ARegion *region)
{
  region->flag |= RGN_FLAG_PREFSIZE_OR_HIDDEN;

  ED_region_panels_init(wm, region);
  region->v2d.keepzoom |= V2D_LOCKZOOM_X | V2D_LOCKZOOM_Y;
}

static void btns_navigation_bar_region_draw(const dContext *C, ARegion *region)
{
  LISTBASE_FOREACH (PanelType *, pt, &region->type->paneltypes) {
    pt->flag |= PANEL_TYPE_LAYOUT_VERT_BAR;
  }

  ED_region_panels_layout(C, region);
  /* ED_region_panels_layout adds vertical scrollbars, we don't want them. */
  region->v2d.scroll &= ~V2D_SCROLL_VERTICAL;
  ED_region_panels_draw(C, region);
}

static void btns_navigation_bar_region_message_subscribe(
    const wmRegionMessageSubscribeParams *params)
{
  struct wmMsgBus *mbus = params->message_bus;
  ARegion *region = params->region;

  wmMsgSubscribeValue msg_sub_value_region_tag_redraw = {
      .owner = region,
      .user_data = region,
      .notify = ED_region_do_msg_notify_tag_redraw,
  };

  wm_msg_subscribe_api_anon_prop(mbus, Window, view_layer, &msg_sub_value_region_tag_redraw);
}

/* draw a certain button set only if properties area is currently
 * showing that button set, to reduce unnecessary drawing. */
static void btns_area_redraw(ScrArea *area, short btns)
{
  SpaceProps *sbtns = area->spacedata.first;

  /* if the area's current button set is equal to the one to redraw */
  if (sbtns->maind == btns) {
    ED_area_tag_redraw(area);
  }
}

/* -------------------------------------------------------------------- */
/** Area-Level Code **/

/* reused! */
static void btns_area_listener(const wmSpaceTypeListenerParams *params)
{
  ScrArea *area = params->area;
  wmNotifier *wmn = params->notifier;
  SpaceProps *sbtns = area->spacedata.first;

  /* context changes */
  switch (wmn->category) {
    case NC_SCENE:
      switch (wmn->data) {
        case ND_RENDER_OPTIONS:
          btns_area_redraw(area, DCTX_RENDER);
          btns_area_redraw(area, DCTX_OUTPUT);
          btns_area_redraw(area, DCTX_VIEW_LAYER);
          break;
        case ND_WORLD:
          btns_area_redraw(area, DCTX_WORLD);
          sbtns->preview = 1;
          break;
        case ND_FRAME:
          /* any buttons area can have animated properties so redraw all */
          ED_area_tag_redraw(area);
          sbtns->preview = 1;
          break;
        case ND_OB_ACTIVE:
          ED_area_tag_redraw(area);
          sbtsn->preview = 1;
          break;
        case ND_KEYINGSET:
          btns_area_redraw(area, BCONTEXT_SCENE);
          break;
        case ND_RENDER_RESULT:
          break;
        case ND_MODE:
        case ND_LAYER:
        default:
          ED_area_tag_redraw(area);
          break;
      }
      break;
    case NC_OBJECT:
      switch (wmn->data) {
        case ND_TRANSFORM:
          btns_area_redraw(area, DCTX_OBJECT);
          btns_area_redraw(area, DCTX_DATA); /* autotexpace flag */
          break;
        case ND_POSE:
        case ND_BONE_ACTIVE:
        case ND_BONE_SELECT:
          btns_area_redraw(area, BCONTEXT_BONE);
          btns_area_redraw(area, BCONTEXT_BONE_CONSTRAINT);
          btns_area_redraw(area, BCONTEXT_DATA);
          break;
        case ND_MODIFIER:
          if (wmn->action == NA_RENAME) {
            ED_area_tag_redraw(area);
          }
          else {
            btns_area_redraw(area, BCONTEXT_MODIFIER);
          }
          btns_area_redraw(area, BCONTEXT_PHYSICS);
          break;
        case ND_CONSTRAINT:
          btns_area_redraw(area, BCONTEXT_CONSTRAINT);
          btns_area_redraw(area, BCONTEXT_BONE_CONSTRAINT);
          break;
        case ND_SHADERFX:
          btns_area_redraw(area, BCONTEXT_SHADERFX);
          break;
        case ND_PARTICLE:
          if (wmn->action == NA_EDITED) {
            btns_area_redraw(area, BCONTEXT_PARTICLE);
          }
          sbtns->preview = 1;
          break;
        case ND_DRAW:
          btns_area_redraw(area, BCONTEXT_OBJECT);
          btns_area_redraw(area, BCONTEXT_DATA);
          btns_area_redraw(area, BCONTEXT_PHYSICS);
          /* Needed to refresh context path when changing active particle system index. */
          btns_area_redraw(area, BCONTEXT_PARTICLE);
          break;
        default:
          /* Not all object API props have a ND_ notifier (yet) */
          ED_area_tag_redraw(area);
          break;
      }
      break;
    case NC_GEOM:
      switch (wmn->data) {
        case ND_SELECT:
        case ND_DATA:
        case ND_VERTEX_GROUP:
          ED_area_tag_redraw(area);
          break;
      }
      break;
    case NC_MATERIAL:
      ED_area_tag_redraw(area);
      switch (wmn->data) {
        case ND_SHADING:
        case ND_SHADING_DRAW:
        case ND_SHADING_LINKS:
        case ND_SHADING_PREVIEW:
        case ND_NODES:
          /* currently works by redraws... if preview is set, it (re)starts job */
          sbtns->preview = 1;
          break;
      }
      break;
    case NC_WORLD:
      btns_area_redraw(area, BCONTEXT_WORLD);
      sbtns->preview = 1;
      break;
    case NC_LAMP:
      btns_area_redraw(area, BCONTEXT_DATA);
      sbtns->preview = 1;
      break;
    case NC_GROUP:
      btns_area_redraw(area, BCONTEXT_OBJECT);
      break;
    case NC_BRUSH:
      btns_area_redraw(area, BCONTEXT_TEXTURE);
      btns_area_redraw(area, BCONTEXT_TOOL);
      sbtsn->preview = 1;
      break;
    case NC_TEXTURE:
    case NC_IMAGE:
      if (wmn->action != NA_PAINTING) {
        ED_area_tag_redraw(area);
        sbtns->preview = 1;
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_PROPERTIES) {
        ED_area_tag_redraw(area);
      }
      else if (wmn->data == ND_SPACE_CHANGED) {
        ED_area_tag_redraw(area);
        sbtns->preview = 1;
      }
      break;
    case NC_ID:
      if (wmn->action == NA_RENAME) {
        ED_area_tag_redraw(area);
      }
      break;
    case NC_ANIMATION:
      switch (wmn->data) {
        case ND_NLA_ACTCHANGE:
          ED_area_tag_redraw(area);
          break;
        case ND_KEYFRAME:
          if (ELEM(wmn->action, NA_EDITED, NA_ADDED, NA_REMOVED)) {
            ED_area_tag_redraw(area);
          }
          break;
      }
      break;
    case NC_GPENCIL:
      switch (wmn->data) {
        case ND_DATA:
          if (ELEM(wmn->action, NA_EDITED, NA_ADDED, NA_REMOVED, NA_SELECTED)) {
            ED_area_tag_redraw(area);
          }
          break;
      }
      break;
    case NC_NODE:
      if (wmn->action == NA_SELECTED) {
        ED_area_tag_redraw(area);
        /* new active node, update texture preview */
        if (sbtns->maind == DCTX_TEXTURE) {
          sbtns->preview = 1;
        }
      }
      break;
    /* Listener for preview render, when doing an global undo. */
    case NC_WM:
      if (wmn->data == ND_UNDO) {
        ED_area_tag_redraw(area);
        sbtns->preview = 1;
      }
      break;
    case NC_SCREEN:
      if (wmn->data == ND_LAYOUTSET) {
        ED_area_tag_redraw(area);
        sbtns->preview = 1;
      }
      break;
#ifdef WITH_FREESTYLE
    case NC_LINESTYLE:
      ED_area_tag_redraw(area);
      sbtns->preview = 1;
      break;
#endif
  }

  if (wmn->data == ND_KEYS) {
    ED_area_tag_redraw(area);
  }
}

static void btns_id_remap(ScrArea *UNUSED(area),
                             SpaceLink *slink,
                             const struct IDRemapper *mappings)
{
  SpaceProps *sbtns = (SpaceProps *)slink;

  if (dune_id_remapper_apply(mappings, &sbtns->pinid, ID_REMAP_APPLY_DEFAULT) ==
      ID_REMAP_RESULT_SOURCE_UNASSIGNED) {
    sbtns->flag &= ~SB_PIN_CONTEXT;
  }

  if (sbuts->path) {
    BtnsCtxPath *path = sbtns->path;
    for (int i = 0; i < path->len; i++) {
      switch (dune_id_remapper_apply(mappings, &path->ptr[i].owner_id, ID_REMAP_APPLY_DEFAULT)) {
        case ID_REMAP_RESULT_SOURCE_UNASSIGNED: {
          if (i == 0) {
            MEM_SAFE_FREE(sbuts->path);
          }
          else {
            memset(&path->ptr[i], 0, sizeof(path->ptr[i]) * (path->len - i));
            path->len = i;
          }
          break;
        }
        case ID_REMAP_RESULT_SOURCE_REMAPPED: {
          api_id_ptr_create(path->ptr[i].owner_id, &path->ptr[i]);
          /* There is no easy way to check/make path downwards valid, just nullify it.
           * Next redraw will rebuild this anyway. */
          i++;
          memset(&path->ptr[i], 0, sizeof(path->ptr[i]) * (path->len - i));
          path->len = i;
          break;
        }

        case ID_REMAP_RESULT_SOURCE_NOT_MAPPABLE:
        case ID_REMAP_RESULT_SOURCE_UNAVAILABLE: {
          /* Nothing to do. */
          break;
        }
      }
    }
  }

  if (sbtns->texuser) {
    BtnsCtxTexture *ct = sbtns->texuser;
    dune_id_remapper_apply(mappings, (ID **)&ct->texture, ID_REMAP_APPLY_DEFAULT);
    lib_freelistN(&ct->users);
    ct->user = NULL;
  }
}

/* -------------------------------------------------------------------- */
/** Space Type Initialization **/

void ED_spacetype_btns(void)
{
  SpaceType *st = MEM_callocN(sizeof(SpaceType), "spacetype buttons");
  ARegionType *art;

  st->spaceid = SPACE_PROPERTIES;
  strncpy(st->name, "Buttons", DUNE_ST_MAXNAME);

  st->create = btns_create;
  st->free = btns_free;
  st->init = btns_init;
  st->duplicate = btns_duplicate;
  st->operatortypes = btns_operatortypes;
  st->keymap = btns_keymap;
  st->listener = btns_area_listener;
  st->context = btns_context;
  st->id_remap = btns_id_remap;

  /* regions: main window */
  art = MEM_callocN(sizeof(ARegionType), "spacetype buttons region");
  art->regionid = RGN_TYPE_WINDOW;
  art->init = btns_main_region_init;
  art->layout = btns_main_region_layout;
  art->draw = ED_region_panels_draw;
  art->listener = btns_main_region_listener;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  btns_context_register(art);
  lib_addhead(&st->regiontypes, art);

  /* Register the panel types from modifiers. The actual panels are built per modifier rather
   * than per modifier type. */
  for (ModifierType i = 0; i < NUM_MODIFIER_TYPES; i++) {
    const ModifierTypeInfo *mti = dune_modifier_get_info(i);
    if (mti != NULL && mti->panelRegister != NULL) {
      mti->panelRegister(art);
    }
  }
  for (int i = 0; i < NUM_GREASEPENCIL_MODIFIER_TYPES; i++) {
    const GpencilModifierTypeInfo *mti = dune_gpencil_modifier_get_info(i);
    if (mti != NULL && mti->panelRegister != NULL) {
      mti->panelRegister(art);
    }
  }
  for (int i = 0; i < NUM_SHADER_FX_TYPES; i++) {
    if (i == eShaderFxType_Light_deprecated) {
      continue;
    }
    const ShaderFxTypeInfo *fxti = dune_shaderfx_get_info(i);
    if (fxti != NULL && fxti->panelRegister != NULL) {
      fxti->panelRegister(art);
    }
  }

  /* regions: header */
  art = MEM_callocN(sizeof(ARegionType), "spacetype buttons region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;

  art->init = btns_header_region_init;
  art->draw = btns_header_region_draw;
  art->message_subscribe = btns_header_region_message_subscribe;
  lib_addhead(&st->regiontypes, art);

  /* regions: navigation bar */
  art = MEM_callocN(sizeof(ARegionType), "spacetype nav buttons region");
  art->regionid = RGN_TYPE_NAV_BAR;
  art->prefsizex = AREAMINX - 3; /* XXX Works and looks best,
                                  * should we update AREAMINX accordingly? */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES | ED_KEYMAP_NAVBAR;
  art->init = buttons_navigation_bar_region_init;
  art->draw = buttons_navigation_bar_region_draw;
  art->message_subscribe = buttons_navigation_bar_region_message_subscribe;
  lib_addhead(&st->regiontypes, art);

  dune_spacetype_register(st);
}
