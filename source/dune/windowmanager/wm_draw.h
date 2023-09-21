#pragma once

struct GPUOffScreen;
struct GPUTexture;
struct GPUViewport;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wmDrawBuffer {
  struct GPUOffScreen *offscreen;
  struct GPUViewport *viewport;
  bool stereo;
  int bound_view;
} wmDrawBuffer;

struct ARegion;
struct ScrArea;
struct Cxt;
struct wmWindow;

/* wm_draw.c */
void wm_draw_update(struct Cxt *C);
void wm_draw_region_clear(struct wmWindow *win, struct ARegion *region);
void wm_draw_region_blend(struct ARegion *region, int view, bool blend);
void wm_draw_region_test(struct Cxt *C, struct ScrArea *area, struct ARegion *region);

struct GPUTexture *wm_draw_region_texture(struct ARegion *region, int view);

#ifdef __cplusplus
}
#endif
