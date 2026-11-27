/* menu/src/scan.h -- card scanner core (contract/SPEC.md v1 clarification 7). The filesystem is a callback table:
 * menu/app/satfs.c on the Saturn, a fixture tree in host tests. BSD-3-Clause. */
#ifndef SCAN_H
#define SCAN_H
#include <stdint.h>
#include "gamesdb.h"
#include "libidx.h"
#include "sysid.h"

#define OM_SCAN_MAXNAMES 512u
#define OM_SCAN_NAMEBUF  16384u
#define OM_SCAN_MAXENT   2048u
#define OM_SCAN_MAXFOLD  1024u      /* spec §7.4 / ruling M2-R4: <= 1,024 folders, root included */
#define OM_SCAN_MAXDEPTH 8u         /* root is depth 0; depth >= 9 is not entered */
#define OM_SCAN_POOL     (128u * 1024u)
#define OM_SCAN_DIRIMG   16u

typedef struct {
    char buf[OM_SCAN_NAMEBUF];
    uint16_t off[OM_SCAN_MAXNAMES], order[OM_SCAN_MAXNAMES];
    uint8_t dir[OM_SCAN_MAXNAMES];
    uint32_t n, used;
    int overflow;
} om_listing;

/* cue_files (2026-11-23, clarification 7's cue dedupe): a .cue's FILE names, path components stripped, NUL-separated
 * into out (sysid.h om_cue_files); returns the count, <= 0 for none. NULL: no dedupe. */
typedef struct {
    void *ctx;
    int (*list)(void *ctx, const char *path, om_listing *out);
    int (*sysid)(void *ctx, const char *path, om_sysid *out);
    void (*progress)(void *ctx, uint32_t dirs, uint32_t images);
    int (*cue_files)(void *ctx, const char *path, char *out, uint32_t cap);
} om_scan_fs;

/* skipped_folders: ruling M2-R4 -- an overflow folder (depth >= 9, or the 1,024-folder cap reached) and
 * everything inside it are skipped, not scanned, and counted here (never stored in the .idx itself).
 * truncated: unrelated over-long (> 255 byte) paths and a game folder's listing overflow (OM_SCAN_MAXNAMES/NAMEBUF).
 * A library folder's own listing overflowing ends the scan with OM_E_FULL (2026-11-23: a 698-name root was scanned as
 * its first 482 names, silently). progress: after each subfolder and each folder, (directories so far, images so far). */
typedef struct {
    uint32_t entries, matched, folders, unreadable, list_errors, truncated, skipped_folders, fingerprint;
} om_scan_report;

typedef struct {
    om_listing a, b;
    om_lib_entry ent[OM_SCAN_MAXENT];
    uint16_t order[OM_SCAN_MAXENT];
    om_lib_folder fold[OM_SCAN_MAXFOLD];
    char pool[OM_SCAN_POOL];
    om_sysid sid[OM_SCAN_DIRIMG];
    int sid_ok[OM_SCAN_DIRIMG];
    const char *names[OM_SCAN_MAXNAMES];
    uint16_t scratch[OM_SCAN_MAXNAMES];
    char path[260], sub[260], img[260];
} om_scan_work;

void om_listing_reset(om_listing *l);
int om_listing_add(om_listing *l, const char *name, int is_dir);
void om_listing_sort(om_listing *l);
const char *om_listing_name(const om_listing *l, uint32_t i);
int om_listing_isdir(const om_listing *l, uint32_t i);
/* 2026-11-23: name i (sorted order) is a file a sibling .cue's FILE lines name: that cue's data, never an image */
int om_listing_ref(const om_listing *l, uint32_t i);
int om_scan(const om_scan_fs *fs, const om_gamesdb *db, om_scan_work *w, uint32_t build_date, uint8_t *out, uint32_t cap,
            uint32_t *out_len, om_scan_report *rep);
#endif
