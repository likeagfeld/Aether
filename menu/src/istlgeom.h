/* menu/src/istlgeom.h -- the start-up interstitial's geometry (boot2menu_mockup_render.py, native 320x224), moved out
 * of menu/views/interstitial.c so host tests can check it: the interstitial shows before SETTINGS.INI is
 * read, so all of it lies inside the largest safe area (OM_SAFE_BOOT, 24 px) whatever overscan= says
 * (menu/host-tests/test_istlgeom.c; its boxes ISTL_ART and ISTL_TEXT are in layout.h). BSD-3-Clause. */
#ifndef OM_ISTLGEOM_H
#define OM_ISTLGEOM_H
#define OM_ISTL_CX         160     /* the logo's and the orbit's center */
#define OM_ISTL_CY         96
#define OM_ISTL_RX         104     /* the comet's orbit radii */
#define OM_ISTL_RY         30
/* the Aether rename (user decision D9, option B): the orbit is tilted -17 degrees, as the logo's own ring (its right
 * end up). Q14 cos and sin of 17 degrees; a point (x, y) of the untilted ellipse is drawn at (TX, TY) from the centre.
 * The logo is line art, so the orbit's far half would show through its open middle: a far-half point within
 * OM_ISTL_DISC_R of the centre (the planet's circle, 40.8 px at 152 x 101, plus its line) is not drawn. */
#define OM_ISTL_TILT_C     15668
#define OM_ISTL_TILT_S     4790
#define OM_ISTL_TX(x, y)   (((x) * OM_ISTL_TILT_C + (y) * OM_ISTL_TILT_S) >> 14)
#define OM_ISTL_TY(x, y)   (((y) * OM_ISTL_TILT_C - (x) * OM_ISTL_TILT_S) >> 14)
#define OM_ISTL_DISC_R     42
#define OM_ISTL_HALF_X     100     /* the tilted orbit's half extents: ceil(sqrt((RX c)^2 + (RY s)^2)), (RX s)^2 + (RY c)^2 */
#define OM_ISTL_HALF_Y     42
#define OM_ISTL_LOGO_W     152     /* the big logo (BIGLOGO.BIN, menu/src/biglogo.h) */
#define OM_ISTL_LOGO_H     101
#define OM_ISTL_LAB_MID    156     /* "LOADING YOUR LIBRARY": the label line's middle */
#define OM_ISTL_LAB_TRACK8 13      /* its letter spacing, 1.6 px in 1/8 px */
#define OM_ISTL_CNT_MID    172     /* the counts line's middle */
#define OM_ISTL_BAR_X0     110     /* the progress bar, 3 px tall from OM_ISTL_BAR_Y */
#define OM_ISTL_BAR_X1     210
#define OM_ISTL_BAR_Y      186
#endif
