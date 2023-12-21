#pragma once

#include "types_id.h"
#include "types_defs.h"
#include "types_gpu.h"
#include "types_img.h"
#include "types_movieclip.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AnimData;
struct Ipo;
struct Ob;

/* Stereo Settings */
typedef struct CameraStereoSettings {
  float interocular_distance;
  float convergence_distance;
  short convergence_mode;
  short pivot;
  short flag;
  char _pad[2];
  /* Cut-off angle at which interocular distance start to fade down. */
  float pole_merge_angle_from;
  /* Cut-off angle at which interocular distance stops to fade down. */
  float pole_merge_angle_to;
} CameraStereoSettings;

/* Background Picture */
typedef struct CameraBGImg {
  struct CameraBGImg *next, *prev;

  struct Img *im;
  struct ImgUser iuser;
  struct MovieClip *clip;
  struct MovieClipUser cuser;
  float offset[2], scale, rotation;
  float alpha;
  short flag;
  short src;
} CameraBGImg;

/* Props for dof effect. */
typedef struct CameraDOFSettings {
  /* Focal distance for depth of field. */
  struct Ob *focus_ob;
  float focus_distance;
  float aperture_fstop;
  float aperture_rotation;
  float aperture_ratio;
  int aperture_blades;
  short flag;
  char _pad[2];
} CameraDOFSettings;

typedef struct CameraRuntime {
  /* For drw manager. */
  float drw_corners[2][4][2];
  float drw_tria[2][2];
  float drw_depth[2];
  float drw_focusmat[4][4];
  float drw_normalmat[4][4];
} CameraRuntime;

typedef struct Camera {
  Id id;
  /* Anim data (must be immediately after id for utilities to use it). */
  struct AnimData *adt;

  /* CAM_PERSP, CAM_ORTHO or CAM_PANO. */
  char type;
  /* Drw type extra. */
  char dtx;
  short flag;
  float passepartalpha;
  float clip_start, clip_end;
  float lens, ortho_scale, drwsize;
  float sensor_x, sensor_y;
  float shiftx, shifty;
  float dof_distance TYPES_DEPRECATED;

  /* Old anim sys, deprecated for 2.5. */
  struct Ipo *ipo TYPES_DEPRECATED;

  struct Ob *dof_ob TYPES_DEPRECATED;
  struct GPUDOFSettings gpu_dof TYPES_DEPRECATED;
  struct CameraDOFSettings dof;

  /* CameraBGImg ref imgs */
  struct List bg_imgs;

  char sensor_fit;
  char _pad[7];

  /* Stereo settings */
  struct CameraStereoSettings stereo;

  /* Runtime data (keep last). */
  CameraRuntime runtime;
} Camera;

/* CAMERA */
/* type */
enum {
  CAM_PERSP = 0,
  CAM_ORTHO = 1,
  CAM_PANO = 2,
};

/* dtx */
enum {
  CAM_DTX_CENTER = (1 << 0),
  CAM_DTX_CENTER_DIAG = (1 << 1),
  CAM_DTX_THIRDS = (1 << 2),
  CAM_DTX_GOLDEN = (1 << 3),
  CAM_DTX_GOLDEN_TRI_A = (1 << 4),
  CAM_DTX_GOLDEN_TRI_B = (1 << 5),
  CAM_DTX_HARMONY_TRI_A = (1 << 6),
  CAM_DTX_HARMONY_TRI_B = (1 << 7),
};

/* flag */
enum {
  CAM_SHOWLIMITS = (1 << 0),
  CAM_SHOWMIST = (1 << 1),
  CAM_SHOWPASSEPARTOUT = (1 << 2),
  CAM_SHOW_SAFE_MARGINS = (1 << 3),
  CAM_SHOWNAME = (1 << 4),
  CAM_ANGLETOGGLE = (1 << 5),
  CAM_DS_EXPAND = (1 << 6),
#ifdef TYPES_DEPRECATED_ALLOW
  CAM_PANORAMA = (1 << 7), /* deprecated */
#endif
  CAM_SHOWSENSOR = (1 << 8),
  CAM_SHOW_SAFE_CENTER = (1 << 9),
  CAM_SHOW_BG_IMG = (1 << 10),
};

/* Sensor fit */
enum {
  CAMERA_SENSOR_FIT_AUTO = 0,
  CAMERA_SENSOR_FIT_HOR = 1,
  CAMERA_SENSOR_FIT_VERT = 2,
};

#define DEFAULT_SENSOR_WIDTH 36.0f
#define DEFAULT_SENSOR_HEIGHT 24.0f

/* stereo->convergence_mode */
enum {
  CAM_S3D_OFFAXIS = 0,
  CAM_S3D_PARALLEL = 1,
  CAM_S3D_TOE = 2,
};

/* stereo->pivot */
enum {
  CAM_S3D_PIVOT_LEFT = 0,
  CAM_S3D_PIVOT_RIGHT = 1,
  CAM_S3D_PIVOT_CENTER = 2,
};

/* stereo->flag */
enum {
  CAM_S3D_SPHERICAL = (1 << 0),
  CAM_S3D_POLE_MERGE = (1 << 1),
};

/* CameraBGImg->flag */
/* may want to use 1 for sel ? */
enum {
  CAM_BGIMG_FLAG_EXPANDED = (1 << 1),
  CAM_BGIMG_FLAG_CAMERACLIP = (1 << 2),
  CAM_BGIMG_FLAG_DISABLED = (1 << 3),
  CAM_BGIMG_FLAG_FOREGROUND = (1 << 4),

  /* Camera framing options */
  /* Don't stretch to fit the camera view. */
  CAM_BGIMG_FLAG_CAMERA_ASPECT = (1 << 5),
  /* Crop out the img. */
  CAM_BGIMG_FLAG_CAMERA_CROP = (1 << 6),

  /* Axis flip options */
  CAM_BGIMG_FLAG_FLIP_X = (1 << 7),
  CAM_BGIMG_FLAG_FLIP_Y = (1 << 8),
};

/* CameraBGImg->src */
/* may want to use 1 for sel? */
enum {
  CAM_BGIMG_SRC_IMG = 0,
  CAM_BGIMG_SRC_MOVIE = 1,
};

/* CameraDOFSettings->flag */
enum {
  CAM_DOF_ENABLED = (1 << 0),
};

#ifdef __cplusplus
}
#endif
