/* menu/views/saverview.c -- see saverview.h. BSD-3-Clause. */
#include "sgl.h"
#undef pal                     /* SGL.H's old-texture macro: om_logo_img has a pal field (logoblend.h) */
#include "art.h"
#include "biglogo.h"
#include "interstitial.h"
#include "omlayout.h"
#include "saver.h"
#include "saverview.h"
#include "v1.h"

#define Z_SAVER 100                  /* v1.h: bigger is further back; nothing else is drawn */

static om_saver s_sv;
static int s_rc = -1;                 /* not begun yet */

int om_saver_begin(uint32_t seed, int margin)
{
    const uint8_t *tbl;
    uint32_t tlen;
    om_art_invalidate();                                       /* the art job's fd closed: OM_LW_STAGE is free (HWF-12) */
    s_rc = om_istl_logo(&tbl, &tlen);                          /* BIGLOGO.BIN, its CRC checked again */
    if (!s_rc)                                                 /* edges over black, the saver's backdrop (HWF-23) */
        s_rc = om_biglogo_blend(tbl, tlen, (uint8_t *)OM_CT(OM_LW_STAGE), OM_LW_STAGE_MAX, 0, 0, 0, 0,
                                (volatile uint16_t *)(OM_CT(OM_VDP1_VRAM) + OM_VRAM_SAVER));
    om_saver_init(&s_sv, OM_BIGLOGO_W, OM_BIGLOGO_H, margin, margin, 319 - margin, 223 - margin, seed);
    return s_rc;
}

void om_saver_draw(void)
{
    om_saver_pose p;
    om_saver_step(&s_sv, &p);
    om_v1_set_safe(0);                                         /* the pose is already in screen space */
    if (!s_rc)
        om_v1_scaled(p.x0, p.y0, p.x1, p.y1, OM_VRAM_SAVER, OM_BIGLOGO_W, OM_BIGLOGO_H, 0, 0, 0, Z_SAVER);
}

int om_saver_rc(void) { return s_rc; }
