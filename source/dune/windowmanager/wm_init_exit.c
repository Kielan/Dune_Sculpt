/** Manage initializing resources and correctly shutting down. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* only called once, for startup */
void wm_init(DContext *C, int argc, const char **argv)
{

  if (!G.background) {
    wm_ghost_init(C); /* note: it assigns C to ghost! */
    wm_init_cursor_data();
    //dune_sound_jack_sync_cb_set(sound_jack_sync_callback);
  }
  
  GHOST_CreateSystemPaths();
  
  dune_addon_pref_type_init();
  dune_keyconfig_pref_type_init();
  
  wm_optype_init();
  wm_optypes_register();
  
  wm_paneltype_init(); /* Lookup table only. */
  wm_menutype_init();
  wm_uilisttype_init();
  wm_gizmotype_init();
  wm_gizmogrouptype_init();
  

  ed_undosys_type_init();

  dune_lib_cb_free_notifier_ref_set(
      wm_main_remove_notifier_ref);                    /* lib_id.c */
  dune_region_cb_free_gizmomap_set(wm_gizmomap_remove); /* screen.c */
  dune_region_cb_refresh_tag_gizmomap_set(wm_gizmomap_tag_refresh);
  dune_lib_cb_remap_editor_id_ref_set(
      wm_main_remap_editor_id_ref);                     /* lib_id.c */
  dune_spacedata_cb_id_remap_set(ED_spacedata_id_remap); /* screen.c */

}
