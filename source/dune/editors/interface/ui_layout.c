#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "TYPES_armature.h"
#include "TYPES_screen.h"
#include "TYPES_userdef.h"

#include "LI_alloca.h"
#include "LI_dynstr.h"
#include "LI_listbase.h"
#include "LI_math.h"
#include "LI_rect.h"
#include "LI_string.h"
#include "LI_utildefines.h"

#include "I18N_translation.h"

#include "DUNE_anim_data.h"
#include "DUNE_armature.h"
#include "DUNE_context.h"
#include "DUNE_global.h"
#include "DUNE_idprop.h"
#include "DUNE_screen.h"

#include "API_access.h"
#include "API_prototypes.h"

#include "UI_interface.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ui_intern.h"

/* Show an icon button after each RNA button to use to quickly set keyframes,
 * this is a way to display animation/driven/override status, see T54951. */
#define UI_PROP_DECORATE
/* Alternate draw mode where some buttons can use single icon width,
 * giving more room for the text at the expense of nicely aligned text. */
#define UI_PROP_SEP_ICON_WIDTH_EXCEPTION

/* -------------------------------------------------------------------- */
/** \name Structs and Defines **/

#define UI_OPERATOR_ERROR_RET(_ot, _opname, return_statement) \
  if (ot == NULL) { \
    ui_item_disabled(layout, _opname); \
    api_warning("'%s' unknown operator", _opname); \
    return_statement; \
  } \
  (void)0

#define UI_ITEM_PROP_SEP_DIVIDE 0.4f

/* uiLayoutRoot */

typedef struct uiLayoutRoot {
  struct uiLayoutRoot *next, *prev;

  int type;
  wmOperatorCallContext opctx;

  int emw, emh;
  int padding;

  uiMenuHandleFn handlefn;
  void *argv;

  const uiStyle *style;
  uiBlock *block;
  uiLayout *layout;
} uiLayoutRoot;

/* Item */

typedef enum uiItemType {
  ITEM_BUTTON,

  ITEM_LAYOUT_ROW,
  ITEM_LAYOUT_COLUMN,
  ITEM_LAYOUT_COLUMN_FLOW,
  ITEM_LAYOUT_ROW_FLOW,
  ITEM_LAYOUT_GRID_FLOW,
  ITEM_LAYOUT_BOX,
  ITEM_LAYOUT_ABSOLUTE,
  ITEM_LAYOUT_SPLIT,
  ITEM_LAYOUT_OVERLAP,
  ITEM_LAYOUT_RADIAL,

  ITEM_LAYOUT_ROOT
#if 0
      TEMPLATE_COLUMN_FLOW,
  TEMPLATE_SPLIT,
  TEMPLATE_BOX,

  TEMPLATE_HEADER,
  TEMPLATE_HEADER_ID,
#endif
} uiItemType;

typedef struct uiItem {
  void *next, *prev;
  uiItemType type;
  int flag;
} uiItem;

enum {
  UI_ITEM_AUTO_FIXED_SIZE = 1 << 0,
  UI_ITEM_FIXED_SIZE = 1 << 1,

  UI_ITEM_BOX_ITEM = 1 << 2, /* The item is "inside" a box item */
  UI_ITEM_PROP_SEP = 1 << 3,
  UI_ITEM_INSIDE_PROP_SEP = 1 << 4,
  /* Show an icon button next to each property (to set keyframes, show status).
   * Enabled by default, depends on 'UI_ITEM_PROP_SEP'. */
  UI_ITEM_PROP_DECORATE = 1 << 5,
  UI_ITEM_PROP_DECORATE_NO_PAD = 1 << 6,
};

typedef struct uiBtnItem {
  uiItem item;
  uiBtn *but;
} uiBtnnItem;

struct uiLayout {
  uiItem item;

  uiLayoutRoot *root;
  duneCtxStore *ctx;
  uiLayout *parent;
  ListBase items;

  char heading[UI_MAX_NAME_STR];

  /** Sub layout to add child items, if not the layout itself. */
  uiLayout *child_items_layout;

  int x, y, w, h;
  float scale[2];
  short space;
  bool align;
  bool active;
  bool active_default;
  bool activate_init;
  bool enabled;
  bool redalert;
  bool keepaspect;
  /** For layouts inside grid-flow, they and their items shall never have a fixed maximal size. */
  bool variable_size;
  char alignment;
  eUIEmbossType emboss;
  /** for fixed width or height to avoid UI size changes */
  float units[2];
};

typedef struct uiLayoutItemFlow {
  uiLayout litem;
  int number;
  int totcol;
} uiLayoutItemFlow;

typedef struct uiLayoutItemGridFlow {
  uiLayout litem;

  /* Extra parameters */
  bool row_major;    /* Fill first row first, instead of filling first column first. */
  bool even_columns; /* Same width for all columns. */
  bool even_rows;    /* Same height for all rows. */
  /**
   * - If positive, absolute fixed number of columns.
   * - If 0, fully automatic (based on available width).
   * - If negative, automatic but only generates number of columns/rows
   *   multiple of given (absolute) value.
   */
  int columns_len;

  /* Pure internal runtime storage. */
  int tot_items, tot_columns, tot_rows;
} uiLayoutItemGridFlow;

typedef struct uiLayoutItemBx {
  uiLayout litem;
  uiBtn *roundbox;
} uiLayoutItemBx;

typedef struct uiLayoutItemSplit {
  uiLayout litem;
  float percentage;
} uiLayoutItemSplit;

typedef struct uiLayoutItemRoot {
  uiLayout litem;
} uiLayoutItemRoot;

/* -------------------------------------------------------------------- */
/** Item **/

static const char *ui_item_name_add_colon(const char *name, char namestr[UI_MAX_NAME_STR])
{
  const int len = strlen(name);

  if (len != 0 && len + 1 < UI_MAX_NAME_STR) {
    memcpy(namestr, name, len);
    namestr[len] = ':';
    namestr[len + 1] = '\0';
    return namestr;
  }

  return name;
}

static int ui_item_fit(
    int item, int pos, int all, int available, bool is_last, int alignment, float *extra_pixel)
{
  /* available == 0 is unlimited */
  if (ELEM(0, available, all)) {
    return item;
  }

  if (all > available) {
    /* contents is bigger than available space */
    if (is_last) {
      return available - pos;
    }

    const float width = *extra_pixel + (item * available) / (float)all;
    *extra_pixel = width - (int)width;
    return (int)width;
  }

  /* contents is smaller or equal to available space */
  if (alignment == UI_LAYOUT_ALIGN_EXPAND) {
    if (is_last) {
      return available - pos;
    }

    const float width = *extra_pixel + (item * available) / (float)all;
    *extra_pixel = width - (int)width;
    return (int)width;
  }
  return item;
}

/* variable button size in which direction? */
#define UI_ITEM_VARY_X 1
#define UI_ITEM_VARY_Y 2

static int ui_layout_vary_direction(uiLayout *layout)
{
  return ((ELEM(layout->root->type, UI_LAYOUT_HEADER, UI_LAYOUT_PIEMENU) ||
           (layout->alignment != UI_LAYOUT_ALIGN_EXPAND)) ?
              UI_ITEM_VARY_X :
              UI_ITEM_VARY_Y);
}

static bool ui_layout_variable_size(uiLayout *layout)
{
  /* Note that this code is probably a bit flaky, we'd probably want to know whether it's
   * variable in X and/or Y, etc. But for now it mimics previous one,
   * with addition of variable flag set for children of grid-flow layouts. */
  return ui_layout_vary_direction(layout) == UI_ITEM_VARY_X || layout->variable_size;
}

/**
 * Factors to apply to #UI_UNIT_X when calculating button width.
 * This is used when the layout is a varying size, see #ui_layout_variable_size.
 */
struct uiTextIconPadFactor {
  float text;
  float icon;
  float icon_only;
};

/**
 * This adds over an icons width of padding even when no icon is used,
 * this is done because most buttons need additional space (drop-down chevron for example).
 * menus and labels use much smaller `text` values compared to this default.
 *
 * note It may seem odd that the icon only adds 0.25
 * but taking margins into account its fine,
 * except for #ui_text_pad_compact where a bit more margin is required.
 */
static const struct uiTextIconPadFactor ui_text_pad_default = {
    .text = 1.50f,
    .icon = 0.25f,
    .icon_only = 0.0f,
};

/** #ui_text_pad_default scaled down. */
static const struct uiTextIconPadFactor ui_text_pad_compact = {
    .text = 1.25f,
    .icon = 0.35f,
    .icon_only = 0.0f,
};

/** Least amount of padding not to clip the text or icon. */
static const struct uiTextIconPadFactor ui_text_pad_none = {
    .text = 0.25f,
    .icon = 1.50f,
    .icon_only = 0.0f,
};

/**
 * Estimated size of text + icon.
 */
static int ui_text_icon_width_ex(uiLayout *layout,
                                 const char *name,
                                 int icon,
                                 const struct uiTextIconPadFactor *pad_factor)
{
  const int unit_x = UI_UNIT_X * (layout->scale[0] ? layout->scale[0] : 1.0f);

  /* When there is no text, always behave as if this is an icon-only button
   * since it's not useful to return empty space. */
  if (icon && !name[0]) {
    return unit_x * (1.0f + pad_factor->icon_only);
  }

  if (ui_layout_variable_size(layout)) {
    if (!icon && !name[0]) {
      return unit_x * (1.0f + pad_factor->icon_only);
    }

    if (layout->alignment != UI_LAYOUT_ALIGN_EXPAND) {
      layout->item.flag |= UI_ITEM_FIXED_SIZE;
    }

    float margin = pad_factor->text;
    if (icon) {
      margin += pad_factor->icon;
    }

    const float aspect = layout->root->block->aspect;
    const uiFontStyle *fstyle = UI_FSTYLE_WIDGET;
    return UI_fontstyle_string_width_with_block_aspect(fstyle, name, aspect) +
           (int)ceilf(unit_x * margin);
  }
  return unit_x * 10;
}


static int ui_text_icon_width(uiLayout *layout, const char *name, int icon, bool compact)
{
  return ui_text_icon_width_ex(
      layout, name, icon, compact ? &ui_text_pad_compact : &ui_text_pad_default);
}

static void ui_item_size(uiItem *item, int *r_w, int *r_h)
{
  if (item->type == ITEM_BUTTON) {
    uiButtonItem *bitem = (uiButtonItem *)item;

    if (r_w) {
      *r_w = BLI_rctf_size_x(&bitem->but->rect);
    }
    if (r_h) {
      *r_h = BLI_rctf_size_y(&bitem->but->rect);
    }
  }
  else {
    uiLayout *litem = (uiLayout *)item;

    if (r_w) {
      *r_w = litem->w;
    }
    if (r_h) {
      *r_h = litem->h;
    }
  }
}

static void ui_item_offset(uiItem *item, int *r_x, int *r_y)
{
  if (item->type == ITEM_BUTTON) {
    uiButtonItem *bitem = (uiButtonItem *)item;

    if (r_x) {
      *r_x = bitem->but->rect.xmin;
    }
    if (r_y) {
      *r_y = bitem->but->rect.ymin;
    }
  }
  else {
    if (r_x) {
      *r_x = 0;
    }
    if (r_y) {
      *r_y = 0;
    }
  }
}

static void ui_item_position(uiItem *item, int x, int y, int w, int h)
{
  if (item->type == ITEM_BUTTON) {
    uiButtonItem *bitem = (uiButtonItem *)item;

    bitem->but->rect.xmin = x;
    bitem->but->rect.ymin = y;
    bitem->but->rect.xmax = x + w;
    bitem->but->rect.ymax = y + h;

    ui_but_update(bitem->but); /* for strlen */
  }
  else {
    uiLayout *litem = (uiLayout *)item;

    litem->x = x;
    litem->y = y + h;
    litem->w = w;
    litem->h = h;
  }
}

static void ui_item_move(uiItem *item, int delta_xmin, int delta_xmax)
{
  if (item->type == ITEM_BUTTON) {
    uiButtonItem *bitem = (uiButtonItem *)item;

    bitem->but->rect.xmin += delta_xmin;
    bitem->but->rect.xmax += delta_xmax;

    ui_but_update(bitem->but); /* for strlen */
  }
  else {
    uiLayout *litem = (uiLayout *)item;

    if (delta_xmin > 0) {
      litem->x += delta_xmin;
    }
    else {
      litem->w += delta_xmax;
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Special RNA Items
 * \{ */

int uiLayoutGetLocalDir(const uiLayout *layout)
{
  switch (layout->item.type) {
    case ITEM_LAYOUT_ROW:
    case ITEM_LAYOUT_ROOT:
    case ITEM_LAYOUT_OVERLAP:
      return UI_LAYOUT_HORIZONTAL;
    case ITEM_LAYOUT_COLUMN:
    case ITEM_LAYOUT_COLUMN_FLOW:
    case ITEM_LAYOUT_GRID_FLOW:
    case ITEM_LAYOUT_SPLIT:
    case ITEM_LAYOUT_ABSOLUTE:
    case ITEM_LAYOUT_BOX:
    default:
      return UI_LAYOUT_VERTICAL;
  }
}

static uiLayout *ui_item_local_sublayout(uiLayout *test, uiLayout *layout, bool align)
{
  uiLayout *sub;
  if (uiLayoutGetLocalDir(test) == UI_LAYOUT_HORIZONTAL) {
    sub = uiLayoutRow(layout, align);
  }
  else {
    sub = uiLayoutColumn(layout, align);
  }

  sub->space = 0;
  return sub;
}

static void ui_layer_but_cb(bContext *C, void *arg_but, void *arg_index)
{
  wmWindow *win = CTX_wm_window(C);
  uiBut *but = arg_but;
  PointerRNA *ptr = &but->rnapoin;
  PropertyRNA *prop = but->rnaprop;
  const int index = POINTER_AS_INT(arg_index);
  const bool shift = win->eventstate->modifier & KM_SHIFT;
  const int len = RNA_property_array_length(ptr, prop);

  if (!shift) {
    RNA_property_boolean_set_index(ptr, prop, index, true);

    for (int i = 0; i < len; i++) {
      if (i != index) {
        RNA_property_boolean_set_index(ptr, prop, i, 0);
      }
    }

    RNA_property_update(C, ptr, prop);

    LISTBASE_FOREACH (uiBut *, cbut, &but->block->buttons) {
      ui_but_update(cbut);
    }
  }
}

/* create buttons for an item with an RNA array */
static void ui_item_array(uiLayout *layout,
                          uiBlock *block,
                          const char *name,
                          int icon,
                          PointerRNA *ptr,
                          PropertyRNA *prop,
                          int len,
                          int x,
                          int y,
                          int w,
                          int UNUSED(h),
                          bool expand,
                          bool slider,
                          int toggle,
                          bool icon_only,
                          bool compact,
                          bool show_text)
{
  const uiStyle *style = layout->root->style;

  /* retrieve type and subtype */
  const PropertyType type = RNA_property_type(prop);
  const PropertySubType subtype = RNA_property_subtype(prop);

  uiLayout *sub = ui_item_local_sublayout(layout, layout, 1);
  UI_block_layout_set_current(block, sub);

  /* create label */
  if (name[0] && show_text) {
    uiDefBut(block, UI_BTYPE_LABEL, 0, name, 0, 0, w, UI_UNIT_Y, NULL, 0.0, 0.0, 0, 0, "");
  }

  /* create buttons */
  if (type == PROP_BOOLEAN && ELEM(subtype, PROP_LAYER, PROP_LAYER_MEMBER)) {
    /* special check for layer layout */
    const int cols = (len >= 20) ? 2 : 1;
    const int colbuts = len / (2 * cols);
    uint layer_used = 0;
    uint layer_active = 0;

    UI_block_layout_set_current(block, uiLayoutAbsolute(layout, false));

    const int butw = UI_UNIT_X * 0.75;
    const int buth = UI_UNIT_X * 0.75;

    if (ptr->type == &RNA_Armature) {
      bArmature *arm = ptr->data;

      layer_used = arm->layer_used;

      if (arm->edbo) {
        if (arm->act_edbone) {
          layer_active |= arm->act_edbone->layer;
        }
      }
      else {
        if (arm->act_bone) {
          layer_active |= arm->act_bone->layer;
        }
      }
    }

    for (int b = 0; b < cols; b++) {
      UI_block_align_begin(block);

      for (int a = 0; a < colbuts; a++) {
        const int layer_num = a + b * colbuts;
        const uint layer_flag = (1u << layer_num);

        if (layer_used & layer_flag) {
          if (layer_active & layer_flag) {
            icon = ICON_LAYER_ACTIVE;
          }
          else {
            icon = ICON_LAYER_USED;
          }
        }
        else {
          icon = ICON_BLANK1;
        }

        uiBut *but = uiDefAutoButR(
            block, ptr, prop, layer_num, "", icon, x + butw * a, y + buth, butw, buth);
        if (subtype == PROP_LAYER_MEMBER) {
          UI_but_func_set(but, ui_layer_but_cb, but, POINTER_FROM_INT(layer_num));
        }
      }
      for (int a = 0; a < colbuts; a++) {
        const int layer_num = a + len / 2 + b * colbuts;
        const uint layer_flag = (1u << layer_num);

        if (layer_used & layer_flag) {
          if (layer_active & layer_flag) {
            icon = ICON_LAYER_ACTIVE;
          }
          else {
            icon = ICON_LAYER_USED;
          }
        }
        else {
          icon = ICON_BLANK1;
        }

        uiBut *but = uiDefAutoButR(
            block, ptr, prop, layer_num, "", icon, x + butw * a, y, butw, buth);
        if (subtype == PROP_LAYER_MEMBER) {
          UI_but_func_set(but, ui_layer_but_cb, but, POINTER_FROM_INT(layer_num));
        }
      }
      UI_block_align_end(block);

      x += colbuts * butw + style->buttonspacex;
    }
  }
  else if (subtype == PROP_MATRIX) {
    int totdim, dim_size[3]; /* 3 == RNA_MAX_ARRAY_DIMENSION */
    int row, col;

    UI_block_layout_set_current(block, uiLayoutAbsolute(layout, true));

    totdim = RNA_property_array_dimension(ptr, prop, dim_size);
    if (totdim != 2) {
      /* Only 2D matrices supported in UI so far. */
      return;
    }

    w /= dim_size[0];
    /* h /= dim_size[1]; */ /* UNUSED */

    for (int a = 0; a < len; a++) {
      col = a % dim_size[0];
      row = a / dim_size[0];

      uiBut *but = uiDefAutoButR(block,
                                 ptr,
                                 prop,
                                 a,
                                 "",
                                 ICON_NONE,
                                 x + w * col,
                                 y + (dim_size[1] * UI_UNIT_Y) - (row * UI_UNIT_Y),
                                 w,
                                 UI_UNIT_Y);
      if (slider && but->type == UI_BTYPE_NUM) {
        uiButNumber *number_but = (uiButNumber *)but;

        but->a1 = number_but->step_size;
        but = ui_but_change_type(but, UI_BTYPE_NUM_SLIDER);
      }
    }
  }
  else if (subtype == PROP_DIRECTION && !expand) {
    uiDefButR_prop(block,
                   UI_BTYPE_UNITVEC,
                   0,
                   name,
                   x,
                   y,
                   UI_UNIT_X * 3,
                   UI_UNIT_Y * 3,
                   ptr,
                   prop,
                   -1,
                   0,
                   0,
                   -1,
                   -1,
                   NULL);
  }
  else {
    /* NOTE: this block of code is a bit arbitrary and has just been made
     * to work with common cases, but may need to be re-worked */

    /* special case, boolean array in a menu, this could be used in a more generic way too */
    if (ELEM(subtype, PROP_COLOR, PROP_COLOR_GAMMA) && !expand && ELEM(len, 3, 4)) {
      uiDefAutoButR(block, ptr, prop, -1, "", ICON_NONE, 0, 0, w, UI_UNIT_Y);
    }
    else {
      /* Even if 'expand' is false, we expand anyway. */

      /* layout for known array subtypes */
      char str[3] = {'\0'};

      if (!icon_only && show_text) {
        if (type != PROP_BOOLEAN) {
          str[1] = ':';
        }
      }

      /* Show check-boxes for rna on a non-emboss block (menu for eg). */
      bool *boolarr = NULL;
      if (type == PROP_BOOLEAN &&
          ELEM(layout->root->block->emboss, UI_EMBOSS_NONE, UI_EMBOSS_PULLDOWN)) {
        boolarr = MEM_callocN(sizeof(bool) * len, __func__);
        RNA_property_boolean_get_array(ptr, prop, boolarr);
      }

      const char *str_buf = show_text ? str : "";
      for (int a = 0; a < len; a++) {
        if (!icon_only && show_text) {
          str[0] = RNA_property_array_item_char(prop, a);
        }
        if (boolarr) {
          icon = boolarr[a] ? ICON_CHECKBOX_HLT : ICON_CHECKBOX_DEHLT;
        }

        const int width_item = ((compact && type == PROP_BOOLEAN) ?
                                    min_ii(w, ui_text_icon_width(layout, str_buf, icon, false)) :
                                    w);

        uiBut *but = uiDefAutoButR(
            block, ptr, prop, a, str_buf, icon, 0, 0, width_item, UI_UNIT_Y);
        if (slider && but->type == UI_BTYPE_NUM) {
          uiButNumber *number_but = (uiButNumber *)but;

          but->a1 = number_but->step_size;
          but = ui_but_change_type(but, UI_BTYPE_NUM_SLIDER);
        }
        if ((toggle == 1) && but->type == UI_BTYPE_CHECKBOX) {
          but->type = UI_BTYPE_TOGGLE;
        }
        if ((a == 0) && (subtype == PROP_AXISANGLE)) {
          UI_but_unit_type_set(but, PROP_UNIT_ROTATION);
        }
      }

      if (boolarr) {
        MEM_freeN(boolarr);
      }
    }
  }

  UI_block_layout_set_current(block, layout);
}

static void ui_item_enum_expand_handle(bContext *C, void *arg1, void *arg2)
{
  wmWindow *win = CTX_wm_window(C);

  if ((win->eventstate->modifier & KM_SHIFT) == 0) {
    uiBtn *btn = (uiBtn *)arg1;
    const int enum_value = POINTER_AS_INT(arg2);

    int current_value = RNA_property_enum_get(&btn->apipoin, btn->apiprop);
    if (!(current_value & enum_value)) {
      current_value = enum_value;
    }
    else {
      current_value &= enum_value;
    }
    api_prop_enum_set(&btn->apipoin, btn->apiprop, current_value);
  }
}

/** Draw a single enum button, a utility for #ui_item_enum_expand_ex */
static void ui_item_enum_expand_elem_exec(uiLayout *layout,
                                          uiBlock *block,
                                          ApiPtr *ptr,
                                          ApiProp *prop,
                                          const char *uiname,
                                          const int h,
                                          const eButType but_type,
                                          const bool icon_only,
                                          const EnumPropItem *item,
                                          const bool is_first)
{
  const char *name = (!uiname || uiname[0]) ? item->name : "";
  const int icon = item->icon;
  const int value = item->value;
  const int itemw = ui_text_icon_width(block->curlayout, icon_only ? "" : name, icon, 0);

  uiBtn *btn;
  if (icon && name[0] && !icon_only) {
    btn = uiDefIconTextBtnR_prop(
        block, btn_type, 0, icon, name, 0, 0, itemw, h, ptr, prop, -1, 0, value, -1, -1, NULL);
  }
  else if (icon) {
    const int w = (is_first) ? itemw : ceilf(itemw - U.pixelsize);
    but = uiDefIconButR_prop(
        block, btn_type, 0, icon, 0, 0, w, h, ptr, prop, -1, 0, value, -1, -1, NULL);
  }
  else {
    btn = uiDefBtnR_prop(
        block, btn_type, 0, name, 0, 0, itemw, h, ptr, prop, -1, 0, value, -1, -1, NULL);
  }

  if (API_prop_flag(prop) & PROP_ENUM_FLAG) {
    /* If this is set, assert since we're clobbering someone elses callback. */
    /* Buttons get their block's fn by default, so we cannot assert in that case either. */
    LIB_assert(ELEM(but->fn, NULL, block->fn));
    ui_btn_fn_set(but, ui_item_enum_expand_handle, btn, POINTER_FROM_INT(value));
  }

  if (uiLayoutGetLocalDir(layout) != UI_LAYOUT_HORIZONTAL) {
    btn->drawflag |= UI_BUT_TEXT_LEFT;
  }

  /* Allow quick, inaccurate swipe motions to switch tabs
   * (no need to keep cursor over them). */
  if (btn_type == UI_BTYPE_TAB) {
    btn->flag |= UI_BUT_DRAG_LOCK;
  }
}

static void ui_item_enum_expand_ex(uiLayout *layout,
                                     uiBlock *block,
                                     ApiPtr *ptr,
                                     ApiProp *prop,
                                     const char *uiname,
                                     const int h,
                                     const eButType but_type,
                                     const bool icon_only)
{
  /* XXX: The way this function currently handles uiname parameter
   * is insane and inconsistent with general UI API:
   *
   * - uiname is the *enum property* label.
   * - when it is NULL or empty, we do not draw *enum items* labels,
   *   this doubles the icon_only parameter.
   * - we *never* draw (i.e. really use) the enum label uiname, it is just used as a mere flag!
   *
   * Unfortunately, fixing this implies an API "soft break", so better to defer it for later... :/
   * - mont29
   */

  lib_assert(API_prop_type(prop) == PROP_ENUM);

  const bool radial = (layout->root->type == UI_LAYOUT_PIEMENU);

  bool free;
  const EnumPropItem *item_array;
  if (radial) {
    api_prop_enum_items_gettexted_all(block->evil_C, ptr, prop, &item_array, NULL, &free);
  }
  else {
    api_prop_enum_items_gettexted(block->evil_C, ptr, prop, &item_array, NULL, &free);
  }

  /* We don't want nested rows, cols in menus. */
  uiLayout *layout_radial = NULL;
  if (radial) {
    if (layout->root->layout == layout) {
      layout_radial = uiLayoutRadial(layout);
      UI_block_layout_set_current(block, layout_radial);
    }
    else {
      if (layout->item.type == ITEM_LAYOUT_RADIAL) {
        layout_radial = layout;
      }
      UI_block_layout_set_current(block, layout);
    }
  }
  else if (ELEM(layout->item.type, ITEM_LAYOUT_GRID_FLOW, ITEM_LAYOUT_COLUMN_FLOW) ||
           layout->root->type == UI_LAYOUT_MENU) {
    ui_block_layout_set_current(block, layout);
  }
  else {
    ui_block_layout_set_current(block, ui_item_local_sublayout(layout, layout, 1));
  }

  for (const EnumPropItem *item = item_array; item->identifier; item++) {
    const bool is_first = item == item_array;

    if (!item->identifier[0]) {
      const EnumPropItem *next_item = item + 1;

      /* Separate items, potentially with a label. */
      if (next_item->identifier) {
        /* Item without identifier but with name:
         * Add group label for the following items. */
        if (item->name) {
          if (!is_first) {
            uiItemS(block->curlayout);
          }
          uiItemL(block->curlayout, item->name, item->icon);
        }
        else if (radial && layout_radial) {
          uiItemS(layout_radial);
        }
        else {
          uiItemS(block->curlayout);
        }
      }
      continue;
    }

    ui_item_enum_expand_elem_exec(
        layout, block, ptr, prop, uiname, h, but_type, icon_only, item, is_first);
  }

  ui_block_layout_set_current(block, layout);

  if (free) {
    MEM_freeN((void *)item_array);
  }
}
