/* menu/pop/psetup.c -- POPUP.BIN's console setup run (popabi.h om_pop_hdr.setup, the menu's popup.c om_pop_setup):
 * the readings for /aether/DIAG.TXT (menu/src/setupid.h has the format), the text, and the write when it changed
 * (menu/app/setupw.c). Every probe only reads: the BIOS ROM's header, SGL's SMPC status and peripheral table (filled
 * by its own per-frame INTBACK, HWF-40), VDP2 TVSTAT, the cartridge ID byte (ST-TECH-47 §9 step 1: no init write) and
 * A-bus CS0's first 128 bytes (Kai reads the same header unconditionally, main.c:4078). The backup RAM figures come
 * through bram.c (the BIOS backup library; its reset guard is pmain.c's, through SGL), devices 0 and 1. BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "popabi.h"
#include "bram.h"
#include "setup.h"
#include "setupid.h"
#include "biosid.h"         /* OM_BIOS_ROM_CT */
#include "fwcheck.h"
#include "text.h"           /* om_tv_is_pal */
#include "version.h"
#include "om_fmt.h"

#define LIB_AT  0u
#define WORK_AT (OM_BRAM_LIB_BYTES + OM_BRAM_GUARD)
#define TEXT_AT (WORK_AT + OM_BRAM_WORK_BYTES + OM_BRAM_GUARD)
#define OLD_AT  (TEXT_AT + OM_SU_CAP)
#define NEED    (OLD_AT + 2048u)

int om_psetup(const om_pop_setup_req *q);

static void info(om_su_bram *b)
{
    om_bram_info i;
    int rc;
    memset(&i, 0, sizeof i);
    rc = om_bram_info_get(0, &i);
    b->rc = rc == 0 && i.totalsize ? 0 : rc == OM_BUP_UNFORMAT ? 2 : rc == OM_BUP_NON || rc == 0 ? 1 : -1;
    b->total = i.totalblock;
    b->free = i.freeblock;
    b->kb = i.totalsize >> 10;
}

static void bup(uint8_t *w, om_su_raw *r)
{
    int rc = om_bram_init(w + LIB_AT, w + WORK_AT);
    r->bram.rc = r->cart_bram.rc = -1;
    if (rc == OM_BUP_UNFORMAT) r->bram.rc = 2;
    else if (rc == 0) info(&r->bram);
    else return;                                   /* no library, the reset guard refused, an overrun: UNKNOWN */
    if (!om_bram_units()[1].unit_id) { r->cart_bram.rc = 1; return; }
    rc = om_bram_use(1);
    if (rc == OM_BUP_UNFORMAT) r->cart_bram.rc = 2;
    else if (rc == 0) info(&r->cart_bram);
}

int om_psetup(const om_pop_setup_req *q)
{
    om_su_raw r;
    uint8_t cs0[0x80];
    uint32_t i, p, n;
    uint8_t *w = q->arena;
    if (((uint32_t)w & 3u) || q->arena_cap < NEED) return OM_E_FULL;
    memset(&r, 0, sizeof r);
    for (i = 0; i < sizeof cs0; i++)
        cs0[i] = *(volatile uint8_t *)(0x22000000u + i);   /* A-bus CS0, cache-through */
    r.aether = VIBEMENU_VERSION " " OM_BUILD_ID;
    r.fw = om_fw_rc() < 0 ? 0 : om_fw_string();
    r.bios = (const uint8_t *)(OM_BIOS_ROM_CT + 0x800u);
    r.cs0 = cs0;
    r.area = Smpc_Status->area;
    r.tvstat_pal = (uint8_t)om_tv_is_pal();
    r.cart_id = *(volatile uint8_t *)0x24FFFFFFu;
    r.smpc_cond = Smpc_Status->cond;
    memcpy(r.rtc, &Smpc_Status->rtc, sizeof r.rtc);
    r.nport[0] = Per_Connect1;
    r.nport[1] = Per_Connect2;
    for (p = 0; p < 2u; p++)
        for (i = 0; i < OM_SU_TAPS && i < r.nport[p]; i++)
            r.port[p][i] = Smpc_Peripheral[p * 15u + i].id;   /* port 2 starts at entry 15 (diag.c draw_pads) */
    r.games = q->games;
    r.loader = q->loader;
    bup(w, &r);
    n = om_su_text(&r, (char *)w + TEXT_AT, OM_SU_CAP);
    return om_su_save((const char *)w + TEXT_AT, n, w + OLD_AT, 2048u);
}
