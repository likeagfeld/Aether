/*  Copyright (c) 2015 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */
/* Derived from satiator-menu@75bf6e5 jhloader.c for Aether, 2026-11-17 (menu/launcher/UPSTREAM.md).
 * Changes from upstream:
 *  - every iapetus call (cd_abort_file, cd_end_transfer, cd_reset_selector_all, cd_set_sector_size, cd_auth,
 *    cd_read_sector, cd_put_sector_data, CDB register waits) is replaced by cdblk.c, built on SEGA's CDC library;
 *  - the region comes from the menu's SMPC area code in the hand-off block, not from an SMPC INTBACK here;
 *  - vdp_vsync() became a TVSTAT poll (om_txt_wait_vblank);
 *  - the loadcd_boot retry loop is capped at OM_BOOT_FRAMES and then fails with OM_CD_E_BOOTWAIT;
 *  - om_boot_handover() (lseq.c) runs just before the first loadcd_boot call;
 *  - results are recorded in an om_boot_trace for the error screen;
 *  - M6 Task 20a: with igr set, om_igr_install (igr.c) arms the in-game reset on the BIOS copy vector after the IP
 *    is read and before the first loadcd_boot call, and om_igr_uninstall undoes it if loadcd_boot returns;
 *  - the disabled "#if 0" authentic-boot branch is dropped, block-scope declarations are hoisted, the region string
 *    is passed as a parameter, and om_boot_strerror carries upstream main.c's boot-error strings.
 * BIOS addresses (0x06000278/27C, 0x06000288, 0x06000380/3A0, 0x2174/0x2A04) are kept as upstream: HWF-19. */
#include <string.h>
#include "cdblk.h"
#include "jhboot.h"
#include "vdp2txt.h"
#include "igr.h"
#include "lgfx.h"

static struct region_s {
   uint8_t id;
   const char *key;
} regions[] = {
   {1,   "JAPAN"},
   {2,   "TAIWAN and PHILIPINES"},
   {4,   "USA and CANADA"},
   {5,   "BRAZIL"},
   {6,   "KOREA"},
   {10,  "ASIA PAL area"},
   {12,  "EUROPE"},
   {13,  "LATIN AMERICA"},
   {0,   NULL}
};

const char *om_region_string(uint8_t area_code) {
   struct region_s *r;
   for (r = regions; r->id; r++)
      if (r->id == area_code)
         return r->key;
   return NULL;
}

static int set_image_region(uint8_t *base, const char *region_string)
{
   char *ptr, *end;
   // set region header
   memset(base + 0x40, ' ', 0x10);
   base[0x40] = (uint8_t)region_string[0];

   // set region footer
   ptr = (char*)base + 0xe00;
   memset(ptr, ' ', 0x20);
   *(uint32_t*)ptr = 0xa00e0009;
   strcpy(ptr + 4, "For ");
   strcpy(ptr + 8, region_string);
   end = ptr + 4 + strlen(ptr + 4);
   *end = '.';
   return 0;
}

static int emulate_bios_loadcd_read(const char *region_string, om_boot_trace *tr)
{
   int ret;
   // doesn't matter where
   uint8_t *ptr = (uint8_t*)0x6002000;
   uint16_t *read_flag_a = (uint16_t*)0x06000380;
   uint16_t *read_flag_b = (uint16_t*)0x060003a0;
   uint16_t **read_flag_ptr_a = (uint16_t**)0x2174;
   uint16_t **read_flag_ptr_b = (uint16_t**)0x2a04;

   ret = tr->read_rc = om_cd_read(ptr, 150, 16);
   if (ret < 0)
      return ret;
   memcpy(tr->head, ptr, sizeof tr->head);

   set_image_region(ptr, region_string);

   ret = tr->put_rc = om_cd_put(ptr, 16);
   if (ret < 0)
      return ret;

   // we need to set a flag, but it's in different places in different BIOS versions
   if (read_flag_a == *read_flag_ptr_a)
      *read_flag_a = 1;
   else if (read_flag_b == *read_flag_ptr_b)
      *read_flag_b = 1;
   else
       return BOOT_UNRECOGNISED_BIOS;

   return 0;
}

/* hardware 2026-11-25 (Daytona USA, MK-81200, IP size 8000H): under JHL the in-game reset cannot be armed for an IP
 * over 7800H (the staging at OM_IGR_STAGE lies inside it) or a non-standard one, and A+B+C+Start then goes to the
 * BIOS CD player. Say so for 2 s before the boot, on the text screen (the eye's screens are gone by step 9); the CD
 * block is left exactly as the read above left it, and the display goes dark again as the caller set it. */
#define IGR_NOTICE_FRAMES 120
static void igr_off_notice(int why)
{
   int t;
   if (!om_txt_ready || om_lg_used) {
       om_txt_init();
       om_lg_used = 0;
   }
   om_txt_clear();
   om_txt_puts(1, 12, "IN GAME RESET IS OFF FOR THIS GAME", 2);
   om_txt_puts(1, 13, om_igr_strerror(why), 0);
   om_txt_puts(1, 14, "A+B+C+START GOES TO THE CD PLAYER", 0);
   om_txt_display(1);
   for (t = 0; t < IGR_NOTICE_FRAMES; t++)
       om_txt_wait_vblank();
   om_txt_display(0);
}

static om_boot_trace s_last_trace;
const om_boot_trace *om_boot_last(void) { return &s_last_trace; }

static int boot_disc(const char *region_string, om_boot_trace *tr, int igr)
{
   int ret;
   int (**bios_loadcd_boot)(void) = (int (**)(void))0x06000288;
   memset(tr, 0, sizeof *tr);
   tr->igr = OM_IGR_E_OFF;
   // region free boot
   om_cd_init_for_boot(tr->init_rc);
   tr->auth_rc = om_cd_auth(&tr->auth_status);   // upstream ignores the auth result; we record it
   if ((ret = emulate_bios_loadcd_read(region_string, tr)) < 0)
       return ret;
   tr->hirq_in = om_cd_hirq();                    // HWF-55 evidence: DCHG (0x0020) must be clear here
   if (igr) {
       tr->igr = om_igr_install();                // IP at 0x06002000 from the read above
       if (tr->igr != OM_IGR_OK)
           igr_off_notice(tr->igr);
   }
   om_boot_handover();
   do {
       ret = (*bios_loadcd_boot)();
       om_txt_wait_vblank();
       if (++tr->loops > OM_BOOT_FRAMES) {
           om_igr_uninstall();
           return OM_CD_E_BOOTWAIT;
       }
   } while (ret == 1);
   om_igr_uninstall();                            // a successful boot never returns here
   return ret;
}

int om_boot_disc(const char *region_string, om_boot_trace *tr, int igr)
{
   int ret = boot_disc(region_string, tr, igr);
   tr->boot_rc = ret;
   tr->hirq = om_cd_hirq();
   tr->peri = om_cd_peri();
   s_last_trace = *tr;
   return ret;
}

const char *om_boot_strerror(int rc)
{
   switch (rc) {                              // strings as upstream main.c:185-199
   case BOOT_BAD_HEADER: return "Bad disc header. Bad image?";
   case BOOT_BAD_REGION: return "Wrong region.";
   case BOOT_BAD_SECURITY_CODE: return "Bad security code. Bad image?";
   case BOOT_UNRECOGNISED_BIOS: return "Unrecognised BIOS version!";
   case OM_CD_E_SECTORS: return "CD read: sectors never arrived";
   case OM_CD_E_BOOTWAIT: return "BIOS boot still busy after 30 s";
   case OM_CD_E_CDC: return "CD block command failed";
   case OM_CD_E_EHST: return "CD put: EHST never set";
   case OM_CD_E_CMOK: case OM_CD_E_WAIT: return "CD block did not answer";
   default: return "Unknown error";
   }
}
