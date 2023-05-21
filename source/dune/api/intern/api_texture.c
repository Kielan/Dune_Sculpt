#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "types_brush.h"
#include "types_light.h"
#include "types_material.h"
#include "types_node.h"
#include "types_object.h"
#include "types_particle.h"
#include "types_scene.h" /* MAXFRAME only */
#include "types_texture.h"
#include "types_world.h"

#include "lib_utildefines.h"

#include "dune_node.h"
#include "dune_node_tree_update.h"
#include "dune_paint.h"

#include "lang_translation.h"

#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

#ifndef API_RUNTIME
static const EnumPropItem texture_filter_items[] = {
    {TXF_BOX, "BOX", 0, "Box", ""},
    {TXF_EWA, "EWA", 0, "EWA", ""},
    {TXF_FELINE, "FELINE", 0, "FELINE", ""},
    {TXF_AREA, "AREA", 0, "Area", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

const EnumPropItem api_enum_texture_type_items[] = {
    {0, "NONE", 0, "None", ""},
    {TEX_BLEND, "BLEND", ICON_TEXTURE, "Blend", "Procedural - create a ramp texture"},
    {TEX_CLOUDS,
     "CLOUDS",
     ICON_TEXTURE,
     "Clouds",
     "Procedural - create a cloud-like fractal noise texture"},
    {TEX_DISTNOISE,
     "DISTORTED_NOISE",
     ICON_TEXTURE,
     "Distorted Noise",
     "Procedural - noise texture distorted by two noise algorithms"},
    {TEX_IMAGE,
     "IMAGE",
     ICON_IMAGE_DATA,
     "Image or Movie",
     "Allow for images or movies to be used as textures"},
    {TEX_MAGIC,
     "MAGIC",
     ICON_TEXTURE,
     "Magic",
     "Procedural - color texture based on trigonometric functions"},
    {TEX_MARBLE,
     "MARBLE",
     ICON_TEXTURE,
     "Marble",
     "Procedural - marble-like noise texture with wave generated bands"},
    {TEX_MUSGRAVE,
     "MUSGRAVE",
     ICON_TEXTURE,
     "Musgrave",
     "Procedural - highly flexible fractal noise texture"},
    {TEX_NOISE,
     "NOISE",
     ICON_TEXTURE,
     "Noise",
     "Procedural - random noise, gives a different result every time, for every frame, for every "
     "pixel"},
    {TEX_STUCCI, "STUCCI", ICON_TEXTURE, "Stucci", "Procedural - create a fractal noise texture"},
    {TEX_VORONOI,
     "VORONOI",
     ICON_TEXTURE,
     "Voronoi",
     "Procedural - create cell-like patterns based on Worley noise"},
    {TEX_WOOD,
     "WOOD",
     ICON_TEXTURE,
     "Wood",
     "Procedural - wave generated bands or rings, with optional noise"},
    {0, NULL, 0, NULL, NULL},
};

#ifndef API_RUNTIME
static const EnumPropItem blend_type_items[] = {
    {MTEX_BLEND, "MIX", 0, "Mix", ""},
    API_ENUM_ITEM_SEPR,
    {MTEX_DARK, "DARKEN", 0, "Darken", ""},
    {MTEX_MUL, "MULTIPLY", 0, "Multiply", ""},
    API_ENUM_ITEM_SEPR,
    {MTEX_LIGHT, "LIGHTEN", 0, "Lighten", ""},
    {MTEX_SCREEN, "SCREEN", 0, "Screen", ""},
    {MTEX_ADD, "ADD", 0, "Add", ""},
    API_ENUM_ITEM_SEPR,
    {MTEX_OVERLAY, "OVERLAY", 0, "Overlay", ""},
    {MTEX_SOFT_LIGHT, "SOFT_LIGHT", 0, "Soft Light", ""},
    {MTEX_LIN_LIGHT, "LINEAR_LIGHT", 0, "Linear Light", ""},
    API_ENUM_ITEM_SEPR,
    {MTEX_DIFF, "DIFFERENCE", 0, "Difference", ""},
    {MTEX_SUB, "SUBTRACT", 0, "Subtract", ""},
    {MTEX_DIV, "DIVIDE", 0, "Divide", ""},
    API_ENUM_ITEM_SEPR,
    {MTEX_BLEND_HUE, "HUE", 0, "Hue", ""},
    {MTEX_BLEND_SAT, "SATURATION", 0, "Saturation", ""},
    {MTEX_BLEND_COLOR, "COLOR", 0, "Color", ""},
    {MTEX_BLEND_VAL, "VALUE", 0, "Value", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

#ifdef API_RUNTIME

#  include "mem_guardedalloc.h"

#  include "api_access.h"

#  include "dune_colorband.h"
#  include "dune_cxt.h"
#  include "dune_image.h"
#  include "dune_main.h"
#  include "dune_texture.h"

#  include "graph.h"
#  include "graph_build.h"

#  include "ed_node.h"
#  include "ed_render.h"

static ApiStruct *api_Texture_refine(struct PointerRNA *ptr)
{
  Tex *tex = (Tex *)ptr->data;

  switch (tex->type) {
    case TEX_BLEND:
      return &Api_BlendTexture;
    case TEX_CLOUDS:
      return &Api_CloudsTexture;
    case TEX_DISTNOISE:
      return &Api_DistortedNoiseTexture;
    case TEX_IMAGE:
      return &Api_ImageTexture;
    case TEX_MAGIC:
      return &Api_MagicTexture;
    case TEX_MARBLE:
      return &Api_MarbleTexture;
    case TEX_MUSGRAVE:
      return &Api_MusgraveTexture;
    case TEX_NOISE:
      return &Api_NoiseTexture;
    case TEX_STUCCI:
      return &Api_StucciTexture;
    case TEX_VORONOI:
      return &Api_VoronoiTexture;
    case TEX_WOOD:
      return &Api_WoodTexture;
    default:
      return &Api_Texture;
  }
}

static void api_Texture_update(Main *main, Scene *UNUSED(scene), PointerRNA *ptr)
{
  Id *id = ptr->owner_id;

  if (GS(id->name) == ID_TE) {
    Tex *tex = (Tex *)ptr->owner_id;

    graph_id_tag_update(&tex->id, 0);
    graph_id_tag_update(&tex->id, ID_RECALC_EDITORS);
    wm_main_add_notifier(NC_TEXTURE, tex);
    wm_main_add_notifier(NC_MATERIAL | ND_SHADING_DRAW, NULL);
  }
  else if (GS(id->name) == ID_NT) {
    NodeTree *ntree = (NodeTree *)ptr->owner_id;
    ed_node_tree_propagate_change(NULL, main, ntree);
  }
}

static void api_Texture_mapping_update(Main *main, Scene *scene, ApiPtr *ptr)
{
  Id *id = ptr->owner_id;
  TexMapping *texmap = ptr->data;
  dune_texture_mapping_init(texmap);

  if (GS(id->name) == ID_NT) {
    NodeTree *ntree = (NodeTree *)ptr->owner_id;
    /* Try to find and tag the node that this #TexMapping belongs to. */
    LIST_FOREACH (Node *, node, &ntree->nodes) {
      /* This assumes that the #TexMapping is stored at the beginning of the node storage. This is
       * generally true, see #NodeTexBase. If the assumption happens to be false, there might be a
       * missing update. */
      if (node->storage == texmap) {
        dune_ntree_update_tag_node_prop(ntree, node);
      }
    }
  }

  api_Texture_update(main, scene, ptr);
}

static void api_Color_mapping_update(Main *UNUSED(main),
                                     Scene *UNUSED(scene),
                                     ApiPtr *UNUSED(ptr))
{
  /* nothing to do */
}

/* Used for Texture Properties, used (also) for/in Nodes */
static void api_Texture_nodes_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  Tex *tex = (Tex *)ptr->owner_id;

  graph_id_tag_update(&tex->id, 0);
  graph_id_tag_update(&tex->id, ID_RECALC_EDITORS);
  wm_main_add_notifier(NC_TEXTURE | ND_NODES, tex);
}

static void api_Texture_type_set(ApiPtr *ptr, int value)
{
  Tex *tex = (Tex *)ptr->data;

  dune_texture_type_set(tex, value);
}

void api_TextureSlotTexture_update(Cxt *C, ApiPtr *ptr)
{
  graph_relations_tag_update(cxt_data_main(C));
  api_TextureSlot_update(C, ptr);
}

void api_TextureSlot_update(Cxt *C, ApiPtr *ptr)
{
  Id *id = ptr->owner_id;

  graph_id_tag_update(id, 0);

  switch (GS(id->name)) {
    case ID_MA:
      wm_main_add_notifier(NC_MATERIAL | ND_SHADING, id);
      wm_main_add_notifier(NC_MATERIAL | ND_SHADING_DRAW, id);
      break;
    case ID_WO:
      wm_main_add_notifier(NC_WORLD, id);
      break;
    case ID_LA:
      wm_main_add_notifier(NC_LAMP | ND_LIGHTING, id);
      wm_main_add_notifier(NC_LAMP | ND_LIGHTING_DRAW, id);
      break;
    case ID_BR: {
      Scene *scene = cxt_data_scene(C);
      MTex *mtex = ptr->data;
      ViewLayer *view_layer = cxt_data_view_layer(C);
      dune_paint_invalidate_overlay_tex(scene, view_layer, mtex->tex);
      wm_main_add_notifier(NC_BRUSH, id);
      break;
    }
    case ID_LS:
      wm_main_add_notifier(NC_LINESTYLE, id);
      break;
    case ID_PA: {
      MTex *mtex = ptr->data;
      int recalc = ID_RECALC_GEOMETRY;

      if (mtex->mapto & PAMAP_INIT) {
        recalc |= ID_RECALC_PSYS_RESET;
      }
      if (mtex->mapto & PAMAP_CHILD) {
        recalc |= ID_RECALC_PSYS_CHILD;
      }

      graph_id_tag_update(id, recalc);
      wm_main_add_notifier(NC_OBJECT | ND_PARTICLE | NA_EDITED, NULL);
      break;
    }
    default:
      break;
  }
}

char *api_TextureSlot_path(const ApiPtr *ptr)
{
  MTex *mtex = ptr->data;

  /* if there is ID-data, resolve the path using the index instead of by name,
   * since the name used is the name of the texture assigned, but the texture
   * may be used multiple times in the same stack
   */
  if (ptr->owner_id) {
    if (GS(ptr->owner_id->name) == ID_BR) {
      return lib_strdup("texture_slot");
    }
    else {
      ApiPtr id_ptr;
      ApiProp *prop;

      /* find the 'textures' property of the ID-struct */
      api_id_ptr_create(ptr->owner_id, &id_ptr);
      prop = api_struct_find_prop(&id_ptr, "texture_slots");

      /* get an iterator for this property, and try to find the relevant index */
      if (prop) {
        int index = apu_prop_collection_lookup_index(&id_ptr, prop, ptr);

        if (index != -1) {
          return lib_sprintfN("texture_slots[%d]", index);
        }
      }
    }
  }

  /* this is a compromise for the remaining cases... */
  if (mtex->tex) {
    char name_esc[(sizeof(mtex->tex->id.name) - 2) * 2];

    lib_str_escape(name_esc, mtex->tex->id.name + 2, sizeof(name_esc));
    return lib_sprintfN("texture_slots[\"%s\"]", name_esc);
  }
  else {
    return lib_strdup("texture_slots[0]");
  }
}

static int api_TextureSlot_name_length(ApiPtr *ptr)
{
  MTex *mtex = ptr->data;

  if (mtex->tex) {
    return strlen(mtex->tex->id.name + 2);
  }

  return 0;
}

static void api_TextureSlot_name_get(ApiPtr *ptr, char *str)
{
  MTex *mtex = ptr->data;

  if (mtex->tex) {
    strcpy(str, mtex->tex->id.name + 2);
  }
  else {
    str[0] = '\0';
  }
}

static int api_TextureSlot_output_node_get(ApiPtr *ptr)
{
  MTex *mtex = ptr->data;
  Tex *tex = mtex->tex;
  int cur = mtex->which_output;

  if (tex) {
    NodeTree *ntree = tex->nodetree;
    Node *node;
    if (ntree) {
      for (node = ntree->nodes.first; node; node = node->next) {
        if (node->type == TEX_NODE_OUTPUT) {
          if (cur == node->custom1) {
            return cur;
          }
        }
      }
    }
  }

  mtex->which_output = 0;
  return 0;
}

static const EnumPropItem *api_TextureSlot_output_node_itemf(Cxt *UNUSED(C),
                                                             ApiPtr *ptr,
                                                             ApiProp *UNUSED(prop),
                                                             bool *r_free)
{
  MTex *mtex = ptr->data;
  Tex *tex = mtex->tex;
  EnumPropItem *item = NULL;
  int totitem = 0;

  if (tex) {
    NodeTree *ntree = tex->nodetree;
    if (ntree) {
      EnumPropItem tmp = {0, "", 0, "", ""};
      Node *node;

      tmp.value = 0;
      tmp.name = "Not Specified";
      tmp.id = "NOT_SPECIFIED";
      api_enum_item_add(&item, &totitem, &tmp);

      for (node = ntree->nodes.first; node; node = node->next) {
        if (node->type == TEX_NODE_OUTPUT) {
          tmp.value = node->custom1;
          tmp.name = ((TexNodeOutput *)node->storage)->name;
          tmp.id = tmp.name;
          api_enum_item_add(&item, &totitem, &tmp);
        }
      }
    }
  }

  api_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static void api_Texture_use_color_ramp_set(ApiPtr *ptr, bool value)
{
  Tex *tex = (Tex *)ptr->data;

  if (value) {
    tex->flag |= TEX_COLORBAND;
  }
  else {
    tex->flag &= ~TEX_COLORBAND;
  }

  if ((tex->flag & TEX_COLORBAND) && tex->coba == NULL) {
    tex->coba = dune_colorband_add(false);
  }
}

static void api_Texture_use_nodes_update(Cxt *C, ApiPtr *ptr)
{
  Tex *tex = (Tex *)ptr->data;

  if (tex->use_nodes) {
    tex->type = 0;

    if (tex->nodetree == NULL) {
      ed_node_texture_default(C, tex);
    }
  }

  api_Texture_nodes_update(cxt_data_main(C), cxt_data_scene(C), ptr);
}

static void api_ImageTexture_mipmap_set(ApiPtr *ptr, bool value)
{
  Tex *tex = (Tex *)ptr->data;

  if (value) {
    tex->imaflag |= TEX_MIPMAP;
  }
  else {
    tex->imaflag &= ~TEX_MIPMAP;
  }
}

#else

static void api_def_texmapping(DuneApi *dapi)
{
  static const EnumPropItem prop_mapping_items[] = {
      {MTEX_FLAT, "FLAT", 0, "Flat", "Map X and Y coordinates directly"},
      {MTEX_CUBE, "CUBE", 0, "Cube", "Map using the normal vector"},
      {MTEX_TUBE, "TUBE", 0, "Tube", "Map with Z as central axis"},
      {MTEX_SPHERE, "SPHERE", 0, "Sphere", "Map with Z as central axis"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_xyz_mapping_items[] = {
      {0, "NONE", 0, "None", ""},
      {1, "X", 0, "X", ""},
      {2, "Y", 0, "Y", ""},
      {3, "Z", 0, "Z", ""},
      {0, NULL, 0, NULL, NULL},
  };

  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "TexMapping", NULL);
  api_def_struct_ui_text(sapi, "Texture Mapping", "Texture coordinate mapping settings");

  prop = api_def_prop(sapi, "vector_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sdna(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_mapping_type_items);
  api_def_prop_ui_text(prop, "Type", "Type of vector that the mapping transforms");
  api_def_prop_update(prop, 0, "api_Texture_mapping_update");

  prop = api_def_prop(sapi, "translation", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "loc");
  api_def_prop_ui_text(prop, "Location", "");
  api_def_prop_ui_range(prop, -FLT_MAX, FLT_MAX, 1, API_TRANSLATION_PREC_DEFAULT);
  api_def_prop_update(prop, 0, "api_Texture_mapping_update");

  /* Not PROP_XYZ, this is now in radians, no more degrees */
  prop = api_def_prop(sapi, "rotation", PROP_FLOAT, PROP_EULER);
  api_def_prop_float_stype(prop, NULL, "rot");
  api_def_prop_ui_text(prop, "Rotation", "");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "scale", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "size");
  api_def_prop_flag(prop, PROP_PROPORTIONAL);
  api_def_prop_ui_text(prop, "Scale", "");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "min", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_sdna(prop, NULL, "min");
  api_def_prop_ui_text(prop, "Minimum", "Minimum value for clipping");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "max", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stype(prop, NULL, "max");
  api_def_prop_ui_text(prop, "Maximum", "Maximum value for clipping");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "use_min", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", TEXMAP_CLIP_MIN);
  api_def_prop_ui_text(prop, "Has Minimum", "Whether to use minimum clipping value");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "use_max", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_sapi(prop, NULL, "flag", TEXMAP_CLIP_MAX);
  api_def_prop_ui_text(prop, "Has Maximum", "Whether to use maximum clipping value");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "mapping_x", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "projx");
  api_def_prop_enum_items(prop, prop_xyz_mapping_items);
  api_def_prop_ui_text(prop, "X Mapping", "");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_prop(sapi, "mapping_y", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "projy");
  api_def_prop_enum_items(prop, prop_xyz_mapping_items);
  api_def_prop_ui_text(prop, "Y Mapping", "");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = api_def_property(srna, "mapping_z", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sdna(prop, NULL, "projz");
  api_def_prop_enum_items(prop, prop_xyz_mapping_items);
  api_def_prop_ui_text(prop, "Z Mapping", "");
  api_def_prop_update(prop, 0, "rna_Texture_mapping_update");

  prop = RNA_def_property(srna, "mapping", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, prop_mapping_items);
  RNA_def_property_ui_text(prop, "Mapping", "");
  RNA_def_property_update(prop, 0, "rna_Texture_mapping_update");
}

static void rna_def_colormapping(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "ColorMapping", NULL);
  RNA_def_struct_ui_text(srna, "Color Mapping", "Color mapping settings");

  prop = RNA_def_property(srna, "use_color_ramp", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", COLORMAP_USE_RAMP);
  RNA_def_property_ui_text(prop, "Use Color Ramp", "Toggle color ramp operations");
  RNA_def_property_update(prop, 0, "rna_Color_mapping_update");

  prop = RNA_def_property(srna, "color_ramp", PROP_POINTER, PROP_NEVER_NULL);
  RNA_def_property_pointer_sdna(prop, NULL, "coba");
  RNA_def_property_struct_type(prop, "ColorRamp");
  RNA_def_property_ui_text(prop, "Color Ramp", "");
  RNA_def_property_update(prop, 0, "rna_Color_mapping_update");

  prop = api_def_prop(sapi, "brightness", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "bright");
  api_def_prop_range(prop, 0, 2);
  api_def_prop_ui_range(prop, 0, 2, 1, 3);
  api_def_prop_ui_text(prop, "Brightness", "Adjust the brightness of the texture");
  api_def_prop_update(prop, 0, "rna_Color_mapping_update");

  prop = api_def_prop(srna, "contrast", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.0, 5);
  api_def_prop_ui_range(prop, 0, 5, 1, 3);
  api_def_prop_ui_text(prop, "Contrast", "Adjust the contrast of the texture");
  api_def_prop_update(prop, 0, "rna_Color_mapping_update");

  prop = api_def_prop(srna, "saturation", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0, 2);
  api_def_prop_ui_range(prop, 0, 2, 1, 3);
  api_def_prop_ui_text(prop, "Saturation", "Adjust the saturation of colors in the texture");
  api_def_prop_update(prop, 0, "rna_Color_mapping_update");

  prop = api_def_prop(sapi, "blend_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, blend_type_items);
  api_def_prop_ui_text(prop, "Blend Type", "Mode used to mix with texture output color");
  api_def_prop_update(prop, 0, "api_Color_mapping_update");

  prop = api_def_prop(sapi, "blend_color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_array(prop, 3);
  api_def_prop_ui_text(prop, "Color", "Blend color to mix with texture output color");
  api_def_prop_update(prop, 0, "api_Color_mapping_update");

  prop = apk_def_prop(srna, "blend_factor", PROP_FLOAT, PROP_NONE);
  api_def_prop_ui_text(prop, "Blend Factor", "");
  api_def_prop_update(prop, 0, "rna_Color_mapping_update");
}

static void api_def_mtex(DuneApi *api)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropertyItem output_node_items[] = {
      {0, "DUMMY", 0, "Dummy", ""},
      {0, NULL, 0, NULL, NULL},
  };

  srna = api_def_struct(dapi, "TextureSlot", NULL);
  api_def_struct_stype(sapi, "MTex");
  api_def_struct_ui_text(
      sapi, "Texture Slot", "Texture slot defining the mapping and influence of a texture");
  api_def_struct_path_func(sapi, "rna_TextureSlot_path");
  api_def_struct_ui_icon(sapi, ICON_TEXTURE_DATA);

  prop = api_def_prop(sapi, "texture", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "tex");
  api_def_prop_struct_type(prop, "Texture");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_CXT_UPDATE);
  api_def_prop_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIB);
  api_def_prop_ui_text(prop, "Texture", "Texture data-block used by this texture slot");
  api_def_prop_update(prop, NC_MATERIAL | ND_SHADING_LINKS, "rna_TextureSlotTexture_update");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_fns(
      prop, "api_TextureSlot_name_get", "rna_TextureSlot_name_length", NULL);
  api_def_prop_ui_text(prop, "Name", "Texture slot name");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_CXT_UPDATE);
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, 0, "rna_TextureSlot_update");

  /* mapping */
  prop = api_def_prop(sapi, "offset", PROP_FLOAT, PROP_TRANSLATION);
  api_def_prop_float_stype(prop, NULL, "ofs");
  api_def_prop_ui_range(prop, -10, 10, 10, RNA_TRANSLATION_PREC_DEFAULT);
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  api_def_prop_ui_text(
      prop, "Offset", "Fine tune of the texture mapping X, Y and Z locations");
  api_def_prop_update(prop, 0, "rna_TextureSlot_update");

  prop = api_def_prop(sapi, "scale", PROP_FLOAT, PROP_XYZ);
  api_def_prop_float_stypes(prop, NULL, "size");
  api_def_prop_flag(prop, PROP_PROPORTIONAL | PROP_CONTEXT_UPDATE);
  api_def_prop_ui_range(prop, -100, 100, 10, 2);
  api_def_prop_ui_text(prop, "Size", "Set scaling for the texture's X, Y and Z sizes");
  api_def_prop_update(prop, 0, "rna_TextureSlot_update");

  prop = api_def_prop(sapi, "color", PROP_FLOAT, PROP_COLOR);
  api_def_prop_float_stype(prop, NULL, "r");
  api_def_prop_array(prop, 3);
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE
  api_def_prop_ui_text(
      prop,
      "Color",
      "Default color for textures that don't return RGB or when RGB to intensity is enabled");
  api_def_prop_update(prop, 0, "rna_TextureSlot_update");

  prop = api_def_prop(sapi, "blend_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sdna(prop, NULL, "blendtype");
  api_def_prop_enum_items(prop, blend_type_items);
  api_def_prop_flag(prop, PROP_CONTEXT_UPDATE);
  api_def_prop_ui_text(prop, "Blend Type", "Mode used to apply the texture
  api_def_prop_update(prop, 0, "rna_TextureSlot_update");

  prop = RNA_def_property(srna, "default_value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "def_var");
  RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
  RNA_def_property_ui_range(prop, 0, 1, 10, 3);
  RNA_def_property_ui_text(
      prop,
      "Default Value",
      "Value to use for Ref, Spec, Amb, Emit, Alpha, RayMir, TransLu and Hard");
  RNA_def_property_update(prop, 0, "rna_TextureSlot_update");

  prop = RNA_def_property(srna, "output_node", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "which_output");
  RNA_def_property_enum_items(prop, output_node_items);
  RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
  RNA_def_property_enum_funcs(
      prop, "rna_TextureSlot_output_node_get", NULL, "rna_TextureSlot_output_node_itemf");
  RNA_def_property_ui_text(
      prop, "Output Node", "Which output node to use, for node-based textures");
  RNA_def_property_update(prop, 0, "rna_TextureSlot_update");
}

static void rna_def_filter_common(StructRNA *srna)
{
  PropertyRNA *prop;

  prop = RNA_def_property(srna, "use_mipmap", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_MIPMAP);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_ImageTexture_mipmap_set");
  RNA_def_property_ui_text(prop, "MIP Map", "Use auto-generated MIP maps for the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_mipmap_gauss", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_GAUSS_MIP);
  RNA_def_property_ui_text(
      prop, "MIP Map Gaussian filter", "Use Gauss filter to sample down MIP maps");
  api_def_prop_update(prop, 0, "rna_Texture_update");

  prop = api_def_property(srna, "filter_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_sdna(prop, NULL, "texfilter");
  api_def_property_enum_items(prop, texture_filter_items);
  api_def_property_ui_text(prop, "Filter", "Texture filter to use for sampling image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = api_def_prop(sapi, "filter_lightprobes", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "afmax");
  api_def_prop_range(prop, 1, 256);
  api_def_prop_ui_text(
      prop,
      "Filter Probes",
      "Maximum number of samples (higher gives less blur at distant/oblique angles, "
      "but is also slower)");
  api_def_prop_update(prop, 0, "api_Texture_update");

  prop = RNA_def_property(srna, "filter_eccentricity", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "afmax");
  RNA_def_property_range(prop, 1, 256);
  RNA_def_property_ui_text(
      prop,
      "Filter Eccentricity",
      "Maximum eccentricity (higher gives less blur at distant/oblique angles, "
      "but is also slower)");
  RNA_def_property_update(prop, 0, "api_Texture_update");

  prop = api_def_prop(sapi, "use_filter_size_min", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "imaflag", TEX_FILTER_MIN);
  api_def_prop_ui_text(
      prop, "Minimum Filter Size", "Use Filter Size as a minimal filter value in pixels");
  api_def_prop_update(prop, 0, "api_Texture_update");

  prop = api_def_prop(sapi, "filter_size", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "filtersize");
  api_def_prop_range(prop, 0.1, 50.0);
  api_def_prop_ui_range(prop, 0.1, 50.0, 1, 2);
  api_def_prop_ui_text(
      prop, "Filter Size", "Multiply the filter size used by MIP Map and Interpolation");
  api_def_prop_update(prop, 0, "api_Texture_update");
}

static const EnumPropItem prop_noise_basis_items[] = {
    {TEX_DUNE,
     "DUNE_ORIGINAL",
     0,
     "Dune Original",
     "Noise algorithm - Blender original: Smooth interpolated noise"},
    {TEX_STDPERLIN,
     "ORIGINAL_PERLIN",
     0,
     "Original Perlin",
     "Noise algorithm - Original Perlin: Smooth interpolated noise"},
    {TEX_NEWPERLIN,
     "IMPROVED_PERLIN",
     0,
     "Improved Perlin",
     "Noise algorithm - Improved Perlin: Smooth interpolated noise"},
    {TEX_VORONOI_F1,
     "VORONOI_F1",
     0,
     "Voronoi F1",
     "Noise algorithm - Voronoi F1: Returns distance to the closest feature point"},
    {TEX_VORONOI_F2,
     "VORONOI_F2",
     0,
     "Voronoi F2",
     "Noise algorithm - Voronoi F2: Returns distance to the 2nd closest feature point"},
    {TEX_VORONOI_F3,
     "VORONOI_F3",
     0,
     "Voronoi F3",
     "Noise algorithm - Voronoi F3: Returns distance to the 3rd closest feature point"},
    {TEX_VORONOI_F4,
     "VORONOI_F4",
     0,
     "Voronoi F4",
     "Noise algorithm - Voronoi F4: Returns distance to the 4th closest feature point"},
    {TEX_VORONOI_F2F1, "VORONOI_F2_F1", 0, "Voronoi F2-F1", "Noise algorithm - Voronoi F1-F2"},
    {TEX_VORONOI_CRACKLE,
     "VORONOI_CRACKLE",
     0,
     "Voronoi Crackle",
     "Noise algorithm - Voronoi Crackle: Voronoi tessellation with sharp edges"},
    {TEX_CELLNOISE,
     "CELL_NOISE",
     0,
     "Cell Noise",
     "Noise algorithm - Cell Noise: Square cell tessellation"},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem prop_noise_type[] = {
    {TEX_NOISESOFT, "SOFT_NOISE", 0, "Soft", "Generate soft noise (smooth transitions)"},
    {TEX_NOISEPERL, "HARD_NOISE", 0, "Hard", "Generate hard noise (sharp transitions)"},
    {0, NULL, 0, NULL, NULL},
};

static void api_def_texture_clouds(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_clouds_stype[] = {
      {TEX_DEFAULT, "GRAYSCALE", 0, "Grayscale", ""},
      {TEX_COLOR, "COLOR", 0, "Color", ""},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "CloudsTexture", "Texture");
  api_def_struct_ui_text(sapi, "Clouds Texture", "Procedural noise texture");
  api_def_struct_stype(sapi, "Tex");

  prop = api_def_prop(sapi, "noise_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "noisesize");
  api_def_prop_range(prop, 0.0001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0001, 2, 1, 2);
  api_def_prop_ui_text(prop, "Noise Size", "Scaling for noise input");
  api_def_prop_update(prop, 0, "api_Texture_update");

  prop = api_def_prop(sapi, "noise_depth", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "noisedepth");
  api_def_prop_range(prop, 0, 30);
  api_def_prop_ui_range(prop, 0, 24, 1, 2);
  api_def_prop_ui_text(prop, "Noise Depth", "Depth of the cloud calculation");
  api_def_prop_update(prop, 0, "api_Texture_nodes_update");

  prop = api_def_prop(sapi, "noise_basis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "noisebasis");
  api_def_prop_enum_items(prop, prop_noise_basis_items);
  api_def_prop_ui_text(prop, "Noise Basis", "Noise basis used for turbulence");
  api_def_prop_update(prop, 0, "rna_Texture_nodes_update");

  prop = api_def_prop(sapi, "noise_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "noisetype");
  api_def_prop_enum_items(prop, prop_noise_type);
  api_def_prop_ui_text(prop, "Noise Type", "");
  api_def_prop_update(prop, 0, "rna_Texture_nodes_update");

  prop = api_def_prop(sapi, "cloud_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "stype");
  api_def_prop_enum_items(prop, prop_clouds_stype);
  api_def_prop_ui_text(
      prop, "Color", "Determine whether Noise returns grayscale or RGB values");
  api_def_prop_update(prop, 0, "api_Texture_nodes_update");

  prop = api_def_prop(sapi, "nabla", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.001, 0.1);
  api_def_prop_ui_range(prop, 0.001, 0.1, 1, 2);
  api_def_prop_ui_text(prop, "Nabla", "Size of derivative offset used for calculating normal");
  api_def_prop_update(prop, 0, "api_Texture_update");
}

static void api_def_texture_wood(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_wood_stype[] = {
      {TEX_BAND, "BANDS", 0, "Bands", "Use standard wood texture in bands"},
      {TEX_RING, "RINGS", 0, "Rings", "Use wood texture in rings"},
      {TEX_BANDNOISE, "BANDNOISE", 0, "Band Noise", "Add noise to standard wood"},
      {TEX_RINGNOISE, "RINGNOISE", 0, "Ring Noise", "Add noise to rings"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_wood_noisebasis2[] = {
      {TEX_SIN, "SIN", 0, "Sine", "Use a sine wave to produce bands"},
      {TEX_SAW, "SAW", 0, "Saw", "Use a saw wave to produce bands"},
      {TEX_TRI, "TRI", 0, "Tri", "Use a triangle wave to produce bands"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "WoodTexture", "Texture");
  api_def_struct_ui_text(sapi, "Wood Texture", "Procedural noise texture");
  api_def_struct_sdna(sapi, "Tex");

  prop = api_def_prop(sapi, "noise_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_prop_float_stype(prop, NULL, "noisesize");
  RNA_def_prop_range(prop, 0.0001, FLT_MAX);
  RNA_def_prop_ui_range(prop, 0.0001, 2, 1, 2);
  RNA_def_prop_ui_text(prop, "Noise Size", "Scaling for noise input");
  RNA_def_prop_update(prop, 0, "api_Texture_update");

  prop = api_def_prop(sapi, "turbulence", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "turbul");
  api_def_prop_range(prop, 0.0001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0001, 200, 10, 2);
  api_def_prop_ui_text(prop, "Turbulence", "Turbulence of the bandnoise and ringnoise types");
  api_def_prop_update(prop, 0, "api_Texture_update");

  prop = api_def_prop(sapi, "noise_basis", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "noisebasis");
  api_def_prop_enum_items(prop, prop_noise_basis_items);
  api_def_prop_ui_text(prop, "Noise Basis", "Noise basis used for turbulence");
  api_def_prop_update(prop, 0, "api_Texture_nodes_update");

  prop = api_def_prop(sapi, "noise_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "noisetype");
  api_def_prop_enum_items(prop, prop_noise_type);
  api_def_prop_ui_text(prop, "Noise Type", "");
  api_def_prop_update(prop, 0, "api_Texture_nodes_update");

  prop = api_def_prop(sapi, "wood_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "stype");
  api_def_prop_enum_items(prop, prop_wood_stype);
  api_def_prop_ui_text(prop, "Pattern", "");
  api_def_prop_update(prop, 0, "api_Texture_nodes_update");

  prop = api_def_prop(sapi, "noise_basis_2", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "noisebasis2");
  api_def_prop_enum_items(prop, prop_wood_noisebasis2);
  api_def_prop_ui_text(prop, "Noise Basis 2", "");
  api_def_prop_update(prop, 0, "rna_Texture_nodes_update");

  prop = api_def_prop(sapi, "nabla", PROP_FLOAT, PROP_NONE);
  api_def_prop_range(prop, 0.001, 0.1);
  api_def_prop_ui_range(prop, 0.001, 0.1, 1, 2);
  api_def_prop_ui_text(prop, "Nabla", "Size of derivative offset used for calculating normal");
  api_def_prop_update(prop, 0, "rna_Texture_update");
}

static void api_def_texture_marble(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem prop_marble_stype[] = {
      {TEX_SOFT, "SOFT", 0, "Soft", "Use soft marble"},
      {TEX_SHARP, "SHARP", 0, "Sharp", "Use more clearly defined marble"},
      {TEX_SHARPER, "SHARPER", 0, "Sharper", "Use very clearly defined marble"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropItem prop_marble_noisebasis2[] = {
      {TEX_SIN, "SIN", 0, "Sin", "Use a sine wave to produce bands"},
      {TEX_SAW, "SAW", 0, "Saw", "Use a saw wave to produce bands"},
      {TEX_TRI, "TRI", 0, "Tri", "Use a triangle wave to produce bands"},
      {0, NULL, 0, NULL, NULL},
  };

  sapi = api_def_struct(dapi, "MarbleTexture", "Texture");
  api_def_struct_ui_text(sapi, "Marble Texture", "Procedural noise texture");
  api_def_struct_sdna(sapi, "Tex");

  prop = api_def_prop(sapi, "noise_scale", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "noisesize");
  api_def_prop_range(prop, 0.0001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0001, 2, 1, 2);
  api_def_prop_ui_text(prop, "Noise Size", "Scaling for noise input");
  api_def_prop_update(prop, 0, "api_Texture_update");

  prop = api_def_prop(sapi, "turbulence", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_styoe(prop, NULL, "turbul");
  api_def_prop_range(prop, 0.0001, FLT_MAX);
  api_def_prop_ui_range(prop, 0.0001, 200, 10, 2);
  api_def_prop_ui_text(prop, "Turbulence", "Turbulence of the bandnoise and ringnoise types");
  api_def_prop_update(prop, 0, "rna_Texture_update");

  prop = api_def_prop(sapi, "noise_depth", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "noisedepth");
  api_def_prop_range(prop, 0, 30);
  api_def_prop_ui_range(prop, 0, 24, 1, 2);
  api_def_prop_ui_text(prop, "Noise Depth", "Depth of the cloud calculation");
  api_def_prop_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisetype");
  RNA_def_property_enum_items(prop, prop_noise_type);
  RNA_def_property_ui_text(prop, "Noise Type", "");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "marble_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "stype");
  RNA_def_property_enum_items(prop, prop_marble_stype);
  RNA_def_property_ui_text(prop, "Pattern", "");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "noise_basis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisebasis");
  RNA_def_property_enum_items(prop, prop_noise_basis_items);
  RNA_def_property_ui_text(prop, "Noise Basis", "Noise basis used for turbulence");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "noise_basis_2", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisebasis2");
  RNA_def_property_enum_items(prop, prop_marble_noisebasis2);
  RNA_def_property_ui_text(prop, "Noise Basis 2", "");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "nabla", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.001, 0.1);
  RNA_def_property_ui_range(prop, 0.001, 0.1, 1, 2);
  RNA_def_property_ui_text(prop, "Nabla", "Size of derivative offset used for calculating normal");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture_magic(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "MagicTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Magic Texture", "Procedural noise texture");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "turbulence", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "turbul");
  RNA_def_property_range(prop, 0.0001, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001, 200, 10, 2);
  RNA_def_property_ui_text(prop, "Turbulence", "Turbulence of the noise");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_depth", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "noisedepth");
  RNA_def_property_range(prop, 0, 30);
  RNA_def_property_ui_range(prop, 0, 24, 1, 2);
  RNA_def_property_ui_text(prop, "Noise Depth", "Depth of the noise");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture_blend(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_blend_progression[] = {
      {TEX_LIN, "LINEAR", 0, "Linear", "Create a linear progression"},
      {TEX_QUAD, "QUADRATIC", 0, "Quadratic", "Create a quadratic progression"},
      {TEX_EASE, "EASING", 0, "Easing", "Create a progression easing from one step to the next"},
      {TEX_DIAG, "DIAGONAL", 0, "Diagonal", "Create a diagonal progression"},
      {TEX_SPHERE, "SPHERICAL", 0, "Spherical", "Create a spherical progression"},
      {TEX_HALO,
       "QUADRATIC_SPHERE",
       0,
       "Quadratic Sphere",
       "Create a quadratic progression in the shape of a sphere"},
      {TEX_RAD, "RADIAL", 0, "Radial", "Create a radial progression"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_flip_axis_items[] = {
      {0, "HORIZONTAL", 0, "Horizontal", "No flipping"},
      {TEX_FLIPBLEND, "VERTICAL", 0, "Vertical", "Flip the texture's X and Y axis"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "BlendTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Blend Texture", "Procedural color blending texture");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "progression", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "stype");
  RNA_def_property_enum_items(prop, prop_blend_progression);
  RNA_def_property_ui_text(prop, "Progression", "Style of the color blending");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "use_flip_axis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_bitflag_sdna(prop, NULL, "flag");
  RNA_def_property_enum_items(prop, prop_flip_axis_items);
  RNA_def_property_ui_text(prop, "Flip Axis", "Flip the texture's X and Y axis");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");
}

static void rna_def_texture_stucci(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_stucci_stype[] = {
      {TEX_PLASTIC, "PLASTIC", 0, "Plastic", "Use standard stucci"},
      {TEX_WALLIN, "WALL_IN", 0, "Wall In", "Create Dimples"},
      {TEX_WALLOUT, "WALL_OUT", 0, "Wall Out", "Create Ridges"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "StucciTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Stucci Texture", "Procedural noise texture");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "turbulence", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "turbul");
  RNA_def_property_range(prop, 0.0001, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001, 200, 10, 2);
  RNA_def_property_ui_text(prop, "Turbulence", "Turbulence of the noise");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_basis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisebasis");
  RNA_def_property_enum_items(prop, prop_noise_basis_items);
  RNA_def_property_ui_text(prop, "Noise Basis", "Noise basis used for turbulence");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "noisesize");
  RNA_def_property_range(prop, 0.0001, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001, 2, 1, 2);
  RNA_def_property_ui_text(prop, "Noise Size", "Scaling for noise input");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisetype");
  RNA_def_property_enum_items(prop, prop_noise_type);
  RNA_def_property_ui_text(prop, "Noise Type", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "stucci_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "stype");
  RNA_def_property_enum_items(prop, prop_stucci_stype);
  RNA_def_property_ui_text(prop, "Pattern", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture_noise(BlenderRNA *brna)
{
  StructRNA *srna;

  srna = RNA_def_struct(brna, "NoiseTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Noise Texture", "Procedural noise texture");
  RNA_def_struct_sdna(srna, "Tex");
}

static void rna_def_texture_image(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_image_extension[] = {
      {TEX_EXTEND, "EXTEND", 0, "Extend", "Extend by repeating edge pixels of the image"},
      {TEX_CLIP, "CLIP", 0, "Clip", "Clip to image size and set exterior pixels as transparent"},
      {TEX_CLIPCUBE,
       "CLIP_CUBE",
       0,
       "Clip Cube",
       "Clip to cubic-shaped area around the image and set exterior pixels as transparent"},
      {TEX_REPEAT, "REPEAT", 0, "Repeat", "Cause the image to repeat horizontally and vertically"},
      {TEX_CHECKER, "CHECKER", 0, "Checker", "Cause the image to repeat in checker board pattern"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "ImageTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Image Texture", "");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "use_interpolation", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_INTERPOL);
  RNA_def_property_ui_text(prop, "Interpolation", "Interpolate pixels using selected filter");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  /* XXX: I think flip_axis should be a generic Texture property,
   * enabled for all the texture types. */
  prop = RNA_def_property(srna, "use_flip_axis", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_IMAROT);
  RNA_def_property_ui_text(prop, "Flip Axis", "Flip the texture's X and Y axis");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_alpha", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_USEALPHA);
  RNA_def_property_ui_text(prop, "Use Alpha", "Use the alpha channel information in the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_calculate_alpha", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_CALCALPHA);
  RNA_def_property_ui_text(
      prop, "Calculate Alpha", "Calculate an alpha channel based on RGB values in the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "invert_alpha", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_NEGALPHA);
  RNA_def_property_ui_text(prop, "Invert Alpha", "Invert all the alpha values in the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  rna_def_filter_common(srna);

  prop = RNA_def_property(srna, "extension", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "extend");
  RNA_def_property_enum_items(prop, prop_image_extension);
  RNA_def_property_ui_text(
      prop, "Extension", "How the image is extrapolated past its original bounds");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_IMAGE);
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "repeat_x", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "xrepeat");
  RNA_def_property_range(prop, 1, 512);
  RNA_def_property_ui_text(prop, "Repeat X", "Repetition multiplier in the X direction");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "repeat_y", PROP_INT, PROP_NONE);
  RNA_def_property_int_sdna(prop, NULL, "yrepeat");
  RNA_def_property_range(prop, 1, 512);
  RNA_def_property_ui_text(prop, "Repeat Y", "Repetition multiplier in the Y direction");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_mirror_x", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_REPEAT_XMIR);
  RNA_def_property_ui_text(prop, "Mirror X", "Mirror the image repetition on the X direction");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_mirror_y", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_REPEAT_YMIR);
  RNA_def_property_ui_text(prop, "Mirror Y", "Mirror the image repetition on the Y direction");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_checker_odd", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_CHECKER_ODD);
  RNA_def_property_ui_text(prop, "Checker Odd", "Odd checker tiles");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_checker_even", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_CHECKER_EVEN);
  RNA_def_property_ui_text(prop, "Checker Even", "Even checker tiles");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "checker_distance", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "checkerdist");
  RNA_def_property_range(prop, 0.0, 0.99);
  RNA_def_property_ui_range(prop, 0.0, 0.99, 0.1, 2);
  RNA_def_property_ui_text(prop, "Checker Distance", "Distance between checker tiles");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

#  if 0

  /* XXX: did this as an array, but needs better descriptions than "1 2 3 4"
   * perhaps a new sub-type could be added?
   * --I actually used single values for this, maybe change later with a RNA_Rect thing? */
  prop = RNA_def_property(srna, "crop_rectangle", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "cropxmin");
  RNA_def_property_array(prop, 4);
  RNA_def_property_range(prop, -10, 10);
  RNA_def_property_ui_text(prop, "Crop Rectangle", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

#  endif

  prop = RNA_def_property(srna, "crop_min_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "cropxmin");
  RNA_def_property_range(prop, -10.0, 10.0);
  RNA_def_property_ui_range(prop, -10.0, 10.0, 1, 2);
  RNA_def_property_ui_text(prop, "Crop Minimum X", "Minimum X value to crop the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "crop_min_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "cropymin");
  RNA_def_property_range(prop, -10.0, 10.0);
  RNA_def_property_ui_range(prop, -10.0, 10.0, 1, 2);
  RNA_def_property_ui_text(prop, "Crop Minimum Y", "Minimum Y value to crop the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "crop_max_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "cropxmax");
  RNA_def_property_range(prop, -10.0, 10.0);
  RNA_def_property_ui_range(prop, -10.0, 10.0, 1, 2);
  RNA_def_property_ui_text(prop, "Crop Maximum X", "Maximum X value to crop the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "crop_max_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "cropymax");
  RNA_def_property_range(prop, -10.0, 10.0);
  RNA_def_property_ui_range(prop, -10.0, 10.0, 1, 2);
  RNA_def_property_ui_text(prop, "Crop Maximum Y", "Maximum Y value to crop the image");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "image", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "ima");
  RNA_def_property_struct_type(prop, "Image");
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Image", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "image_user", PROP_POINTER, PROP_NEVER_NULL);
  RNA_def_property_pointer_sdna(prop, NULL, "iuser");
  RNA_def_property_ui_text(
      prop,
      "Image User",
      "Parameters defining which layer, pass and frame of the image is displayed");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  /* Normal Map */
  prop = RNA_def_property(srna, "use_normal_map", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "imaflag", TEX_NORMALMAP);
  RNA_def_property_ui_text(prop, "Normal Map", "Use image RGB values for normal mapping");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture_musgrave(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_musgrave_type[] = {
      {TEX_MFRACTAL, "MULTIFRACTAL", 0, "Multifractal", "Use Perlin noise as a basis"},
      {TEX_RIDGEDMF,
       "RIDGED_MULTIFRACTAL",
       0,
       "Ridged Multifractal",
       "Use Perlin noise with inflection as a basis"},
      {TEX_HYBRIDMF,
       "HYBRID_MULTIFRACTAL",
       0,
       "Hybrid Multifractal",
       "Use Perlin noise as a basis, with extended controls"},
      {TEX_FBM, "FBM", 0, "fBM", "Fractal Brownian Motion, use Brownian noise as a basis"},
      {TEX_HTERRAIN, "HETERO_TERRAIN", 0, "Hetero Terrain", "Similar to multifractal"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "MusgraveTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Musgrave", "Procedural musgrave texture");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "musgrave_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "stype");
  RNA_def_property_enum_items(prop, prop_musgrave_type);
  RNA_def_property_ui_text(prop, "Type", "Fractal noise algorithm");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "dimension_max", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "mg_H");
  RNA_def_property_range(prop, 0.0001, 2);
  RNA_def_property_ui_range(prop, 0.0001, 2, 1, 2);
  RNA_def_property_ui_text(prop, "Highest Dimension", "Highest fractal dimension");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "lacunarity", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "mg_lacunarity");
  RNA_def_property_range(prop, 0, 6);
  RNA_def_property_ui_range(prop, 0, 6, 1, 2);
  RNA_def_property_ui_text(prop, "Lacunarity", "Gap between successive frequencies");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "octaves", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "mg_octaves");
  RNA_def_property_range(prop, 0, 8);
  RNA_def_property_ui_range(prop, 0, 8, 1, 2);
  RNA_def_property_ui_text(prop, "Octaves", "Number of frequencies used");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "mg_offset");
  RNA_def_property_range(prop, 0, 6);
  RNA_def_property_ui_range(prop, 0, 6, 1, 2);
  RNA_def_property_ui_text(prop, "Offset", "The fractal offset");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "gain", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "mg_gain");
  RNA_def_property_range(prop, 0, 6);
  RNA_def_property_ui_range(prop, 0, 6, 1, 2);
  RNA_def_property_ui_text(prop, "Gain", "The gain multiplier");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_intensity", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "ns_outscale");
  RNA_def_property_range(prop, 0, 10);
  RNA_def_property_ui_range(prop, 0, 10, 1, 2);
  RNA_def_property_ui_text(prop, "Noise Intensity", "Intensity of the noise");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "noisesize");
  RNA_def_property_range(prop, 0.0001, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001, 2, 1, 2);
  RNA_def_property_ui_text(prop, "Noise Size", "Scaling for noise input");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_basis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisebasis");
  RNA_def_property_enum_items(prop, prop_noise_basis_items);
  RNA_def_property_ui_text(prop, "Noise Basis", "Noise basis used for turbulence");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "nabla", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.001, 0.1);
  RNA_def_property_ui_range(prop, 0.001, 0.1, 1, 2);
  RNA_def_property_ui_text(prop, "Nabla", "Size of derivative offset used for calculating normal");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture_voronoi(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem prop_distance_metric_items[] = {
      {TEX_DISTANCE, "DISTANCE", 0, "Actual Distance", "sqrt(x*x+y*y+z*z)"},
      {TEX_DISTANCE_SQUARED, "DISTANCE_SQUARED", 0, "Distance Squared", "(x*x+y*y+z*z)"},
      {TEX_MANHATTAN,
       "MANHATTAN",
       0,
       "Manhattan",
       "The length of the distance in axial directions"},
      {TEX_CHEBYCHEV, "CHEBYCHEV", 0, "Chebychev", "The length of the longest Axial journey"},
      {TEX_MINKOVSKY_HALF, "MINKOVSKY_HALF", 0, "Minkowski 1/2", "Set Minkowski variable to 0.5"},
      {TEX_MINKOVSKY_FOUR, "MINKOVSKY_FOUR", 0, "Minkowski 4", "Set Minkowski variable to 4"},
      {TEX_MINKOVSKY,
       "MINKOVSKY",
       0,
       "Minkowski",
       "Use the Minkowski function to calculate distance "
       "(exponent value determines the shape of the boundaries)"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem prop_coloring_items[] = {
      /* XXX: OK names / descriptions? */
      {TEX_INTENSITY, "INTENSITY", 0, "Intensity", "Only calculate intensity"},
      {TEX_COL1, "POSITION", 0, "Position", "Color cells by position"},
      {TEX_COL2,
       "POSITION_OUTLINE",
       0,
       "Position and Outline",
       "Use position plus an outline based on F2-F1"},
      {TEX_COL3,
       "POSITION_OUTLINE_INTENSITY",
       0,
       "Position, Outline, and Intensity",
       "Multiply position and outline by intensity"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "VoronoiTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Voronoi", "Procedural voronoi texture");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "weight_1", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "vn_w1");
  RNA_def_property_range(prop, -2, 2);
  RNA_def_property_ui_text(prop, "Weight 1", "Voronoi feature weight 1");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "weight_2", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "vn_w2");
  RNA_def_property_range(prop, -2, 2);
  RNA_def_property_ui_text(prop, "Weight 2", "Voronoi feature weight 2");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "weight_3", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "vn_w3");
  RNA_def_property_range(prop, -2, 2);
  RNA_def_property_ui_text(prop, "Weight 3", "Voronoi feature weight 3");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "weight_4", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "vn_w4");
  RNA_def_property_range(prop, -2, 2);
  RNA_def_property_ui_text(prop, "Weight 4", "Voronoi feature weight 4");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "minkovsky_exponent", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "vn_mexp");
  RNA_def_property_range(prop, 0.01, 10);
  RNA_def_property_ui_text(prop, "Minkowski Exponent", "Minkowski exponent");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "distance_metric", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "vn_distm");
  RNA_def_property_enum_items(prop, prop_distance_metric_items);
  RNA_def_property_ui_text(
      prop,
      "Distance Metric",
      "Algorithm used to calculate distance of sample points to feature points");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "color_mode", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "vn_coltype");
  RNA_def_property_enum_items(prop, prop_coloring_items);
  RNA_def_property_ui_text(prop, "Coloring", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_intensity", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "ns_outscale");
  RNA_def_property_range(prop, 0.01, 10);
  RNA_def_property_ui_text(prop, "Noise Intensity", "Scales the intensity of the noise");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "noisesize");
  RNA_def_property_range(prop, 0.0001, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001, 2, 1, 2);
  RNA_def_property_ui_text(prop, "Noise Size", "Scaling for noise input");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "nabla", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.001, 0.1);
  RNA_def_property_ui_range(prop, 0.001, 0.1, 1, 2);
  RNA_def_property_ui_text(prop, "Nabla", "Size of derivative offset used for calculating normal");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture_distorted_noise(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "DistortedNoiseTexture", "Texture");
  RNA_def_struct_ui_text(srna, "Distorted Noise", "Procedural distorted noise texture");
  RNA_def_struct_sdna(srna, "Tex");

  prop = RNA_def_property(srna, "distortion", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "dist_amount");
  RNA_def_property_range(prop, 0, 10);
  RNA_def_property_ui_text(prop, "Distortion Amount", "Amount of distortion");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "noisesize");
  RNA_def_property_range(prop, 0.0001, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.0001, 2, 1, 2);
  RNA_def_property_ui_text(prop, "Noise Size", "Scaling for noise input");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "noise_basis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisebasis2");
  RNA_def_property_enum_items(prop, prop_noise_basis_items);
  RNA_def_property_ui_text(prop, "Noise Basis", "Noise basis used for turbulence");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "noise_distortion", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "noisebasis");
  RNA_def_property_enum_items(prop, prop_noise_basis_items);
  RNA_def_property_ui_text(prop, "Noise Distortion", "Noise basis for the distortion");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  prop = RNA_def_property(srna, "nabla", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.001, 0.1);
  RNA_def_property_ui_range(prop, 0.001, 0.1, 1, 2);
  RNA_def_property_ui_text(prop, "Nabla", "Size of derivative offset used for calculating normal");
  RNA_def_property_update(prop, 0, "rna_Texture_update");
}

static void rna_def_texture(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "Texture", "ID");
  RNA_def_struct_sdna(srna, "Tex");
  RNA_def_struct_ui_text(
      srna, "Texture", "Texture data-block used by materials, lights, worlds and brushes");
  RNA_def_struct_ui_icon(srna, ICON_TEXTURE_DATA);
  RNA_def_struct_refine_func(srna, "rna_Texture_refine");

  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  // RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_enum_sdna(prop, NULL, "type");
  RNA_def_property_enum_items(prop, rna_enum_texture_type_items);
  RNA_def_property_enum_funcs(prop, NULL, "rna_Texture_type_set", NULL);
  RNA_def_property_ui_text(prop, "Type", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_clamp", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_negative_sdna(prop, NULL, "flag", TEX_NO_CLAMP);
  RNA_def_property_ui_text(prop,
                           "Clamp",
                           "Set negative texture RGB and intensity values to zero, for some uses "
                           "like displacement this option can be disabled to get the full range");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "use_color_ramp", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_COLORBAND);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_Texture_use_color_ramp_set");
  RNA_def_property_ui_text(prop,
                           "Use Color Ramp",
                           "Map the texture intensity to the color ramp. "
                           "Note that the alpha value is used for image textures, "
                           "enable \"Calculate Alpha\" for images without an alpha channel");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "color_ramp", PROP_POINTER, PROP_NEVER_NULL);
  RNA_def_property_pointer_sdna(prop, NULL, "coba");
  RNA_def_property_struct_type(prop, "ColorRamp");
  RNA_def_property_ui_text(prop, "Color Ramp", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "intensity", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "bright");
  RNA_def_property_range(prop, 0, 2);
  RNA_def_property_ui_range(prop, 0, 2, 1, 3);
  RNA_def_property_ui_text(prop, "Brightness", "Adjust the brightness of the texture");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "contrast", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0.0, 5);
  RNA_def_property_ui_range(prop, 0, 5, 1, 3);
  RNA_def_property_ui_text(prop, "Contrast", "Adjust the contrast of the texture");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "saturation", PROP_FLOAT, PROP_NONE);
  RNA_def_property_range(prop, 0, 2);
  RNA_def_property_ui_range(prop, 0, 2, 1, 3);
  RNA_def_property_ui_text(prop, "Saturation", "Adjust the saturation of colors in the texture");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  /* RGB Factor */
  prop = RNA_def_property(srna, "factor_red", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "rfac");
  RNA_def_property_range(prop, 0, 2);
  RNA_def_property_ui_range(prop, 0, 2, 1, 3);
  RNA_def_property_ui_text(prop, "Factor Red", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "factor_green", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "gfac");
  RNA_def_property_range(prop, 0, 2);
  RNA_def_property_ui_range(prop, 0, 2, 1, 3);
  RNA_def_property_ui_text(prop, "Factor Green", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  prop = RNA_def_property(srna, "factor_blue", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "bfac");
  RNA_def_property_range(prop, 0, 2);
  RNA_def_property_ui_range(prop, 0, 2, 1, 3);
  RNA_def_property_ui_text(prop, "Factor Blue", "");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  /* Alpha for preview render */
  prop = RNA_def_property(srna, "use_preview_alpha", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", TEX_PRV_ALPHA);
  RNA_def_property_ui_text(prop, "Show Alpha", "Show Alpha in Preview Render");
  RNA_def_property_update(prop, 0, "rna_Texture_update");

  /* nodetree */
  prop = RNA_def_property(srna, "use_nodes", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "use_nodes", 1);
  RNA_def_property_flag(prop, PROP_CONTEXT_UPDATE);
  RNA_def_property_ui_text(prop, "Use Nodes", "Make this a node-based texture");
  RNA_def_property_update(prop, 0, "rna_Texture_use_nodes_update");

  prop = RNA_def_property(srna, "node_tree", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_sdna(prop, NULL, "nodetree");
  RNA_def_property_clear_flag(prop, PROP_PTR_NO_OWNERSHIP);
  RNA_def_property_override_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Node Tree", "Node tree for node-based textures");
  RNA_def_property_update(prop, 0, "rna_Texture_nodes_update");

  rna_def_animdata_common(srna);

  /* specific types */
  rna_def_texture_clouds(brna);
  rna_def_texture_wood(brna);
  rna_def_texture_marble(brna);
  rna_def_texture_magic(brna);
  rna_def_texture_blend(brna);
  rna_def_texture_stucci(brna);
  rna_def_texture_noise(brna);
  rna_def_texture_image(brna);
  rna_def_texture_musgrave(brna);
  rna_def_texture_voronoi(brna);
  rna_def_texture_distorted_noise(brna);
  /* XXX add more types here. */

  RNA_api_texture(srna);
}

void RNA_def_texture(BlenderRNA *brna)
{
  rna_def_texture(brna);
  rna_def_mtex(brna);
  rna_def_texmapping(brna);
  rna_def_colormapping(brna);
}

#endif
