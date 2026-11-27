/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */
/* Derived from satiator-menu@75bf6e5 jhloader.h for Aether, 2026-11-17 (menu/launcher/UPSTREAM.md). */
#ifndef OM_JHBOOT_H
#define OM_JHBOOT_H
#include <stdint.h>

// return codes for boot_disc
#define BOOT_BAD_HEADER         -1
#define BOOT_BAD_SECURITY_CODE  -4
#define BOOT_BAD_REGION         -8

#define BOOT_UNRECOGNISED_BIOS  -1024

#define OM_BOOT_FRAMES          1800   /* loadcd_boot "busy" (1) cap: 30 s at 60 Hz (HWF-11) */

/* head: the first 32 bytes read from FAD 150, before the region patch (expect "SEGA SEGASATURN ", then the maker ID);
 * hirq_in: CD HIRQ just before the first loadcd_boot call; hirq: after the last; peri: the CD status code (CR1 high byte) then. */
typedef struct {
    int init_rc[4]; int auth_rc; uint16_t auth_status; int read_rc; int put_rc; int loops; int boot_rc;
    uint16_t hirq, peri, hirq_in; uint8_t head[32];
    int igr;                                  /* OM_IGR_* (igr.h), OM_IGR_OK = armed */
    /* CWX only (om_cwx_boot; 0 for JHL): which LIBCD call failed (cdblk.h om_cd_last_error, step * 100 - CdcErr), HIRQ
     * and CR1-CR4 just before it and at its failure, and the longest EHST wait after a take (cdblk.h om_cd_diag) */
    int32_t cdc; uint16_t cd_pre[5], cd_post[5]; uint32_t ehst_max;
    /* CWX only: a read that timed out (cdblk.h om_cd_diag): most sectors seen, Get CD Status rc / status / FAD */
    int32_t rd_max, rd_cur_rc, rd_fad; uint8_t rd_status;
} om_boot_trace;
const om_boot_trace *om_boot_last(void);      /* the last om_boot_disc's trace (the launch error screen) */

const char *om_region_string(uint8_t area_code);
int om_boot_disc(const char *region_string, om_boot_trace *tr, int igr);   /* igr: arm the in-game reset */
const char *om_boot_strerror(int rc);
/* Supplied by the caller (lseq.c): called once, just before the first loadcd_boot call, to hand the machine to
 * the BIOS (VBR and SR as the official menu leaves them). Until then exceptions still reach the launcher's table. */
void om_boot_handover(void);
#endif
