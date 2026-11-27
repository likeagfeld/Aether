/* menu/src/impscan.h -- M6 Task 24c (user decision 2026-11-23: the import is picked per game). The scan that lists
 * every game with importable saves -- the Settings > Import picker's rows (folders.c) -- and IMPORT.SEL, the picked
 * list the launcher's import mode reads (pgsbak.h om_pgs_import_sel). Shared by the menu and LAUNCHER.BIN; the Card
 * Manager builds its own checklist against the same scan (contract/SPEC.md §5.17 "Import").
 *
 * Rows, one per game (a product id: Kai's folder-name sanitizing, om_pgs_imp_folder) plus "_GENERAL" (Kai's
 * SAVES/LATEST): the count of "*.BUP" files in
 *   /SAVES/BRAM/<folder> and its 01..09 (a folder that is its own sanitized name; '_' folders are not games),
 *   /SAVES/LATEST (row "_GENERAL"),
 *   /satiator-saves/<id> and /satiator-saves/[SLOTnn]/<id> (nn 01..19; <id> a product id, no areas).
 * A game with no save file is not a row. One listing open at a time, each drained in full (satfs.c, HWF-22).
 *
 * IMPORT.SEL (/aether/SAVES/IMPORT.SEL, written with om_sdw_replace): "VMSEL1 <n>\n" then n lines "<key>\n" (keys
 * as the rows'). The hand-off that runs the import carries "SEL<8 hex>" in its gid: the CRC-32 of the whole file. The
 * launcher refuses a file that is absent, damaged, or not that CRC (stale) -- nothing is imported -- and deletes it
 * after a run. BSD-3-Clause. */
#ifndef OM_IMPSCAN_H
#define OM_IMPSCAN_H
#include <stdint.h>
#include "gameid.h"

#define OM_ISC_KEY      (OM_GID_PRODUCT + 1u)          /* a product id, or "_GENERAL" */
#define OM_ISC_GENERAL  "_GENERAL"
#define OM_ISC_SEL_PATH "/aether/SAVES/IMPORT.SEL"
typedef struct { char key[12]; uint16_t n; uint8_t on, pad; } om_isc_row;
typedef char om_isc_name[24];

/* Kai's sanitizing of a folder name (pseudo_bram.c:199-252): "<product>" or "<product>[<A-Z>{1,8}]"; OM_E_FIELD
 * otherwise or for '_' / '[' names */
int om_pgs_imp_folder(const char *name, char out[OM_GID_MAX + 1]);
uint32_t om_pgs_rings_slot(const char *name);              /* "[SLOTnn]" -> nn (0 = not one) */

/* the rows (cap of them; names: ncap scratch names; al: a 4-aligned 256 B path buffer; st: 304 B s_stat buffer).
 * 0, or an SD error. *n rows, every one on */
int om_isc_scan(om_isc_row *rows, uint32_t cap, uint32_t *n, om_isc_name *names, uint32_t ncap, uint32_t *al,
                uint32_t *st);
/* IMPORT.SEL of the rows that are on: its length (out holds cap), *crc its CRC-32; OM_E_FULL */
int om_isel_fmt(const om_isc_row *rows, uint32_t n, char *out, uint32_t cap, uint32_t *crc);
/* the file (len) against the hand-off's gid ("SEL<8 hex>"): OM_OK, OM_E_CRC (stale: another CRC), OM_E_FIELD */
int om_isel_check(const char *t, uint32_t len, const char *gid);
int om_isel_has(const char *t, uint32_t len, const char *key);    /* 1 when key is picked (a checked file) */
#endif
