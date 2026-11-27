/* menu/app/satfs.c -- see satfs.h. Directory iteration = s_opendir + s_stat(NULL, ...) until it returns <= 0
 * (satiator-menu gui/filelist.c:20; HWF-22). Each listing is read in full before any other call (scan.c). BSD-3-Clause. */
#include <string.h>
#include "om_fmt.h"
#include "satiator.h"
#include "satfs.h"
#include "sd.h"
#include "carddir.h"
#include "yield.h"

int om_card_old_folder;                        /* the Aether rename: the root still holds /vibemenu (satfs.h) */

static int old_folder_name(const char *n)
{   /* OM_CARD_OLD_NAME, ASCII case-insensitively (FAT names) */
    const char *p = OM_CARD_OLD_NAME;
    for (; *p; p++, n++)
        if ((*n >= 'A' && *n <= 'Z' ? *n + 32 : *n) != *p) return 0;
    return !*n;
}

static uint32_t s_dirent[76];                  /* 4-aligned s_stat_t buffer */
/* Task 24 reclaim (task-18b-review focus 6): one buffer for the cue text and then the first raw sector. om_satfs_sysid
 * copies the cue's FILE name into a local before the sector read reuses the buffer; nothing else holds a pointer. */
static uint32_t s_io32[(4096 + 4) / 4];
static char s_abs[300], s_track[300];

static const char *abs_path(const char *rel)
{
    size_t n = strlen(rel);
    if (n > 254) n = 254;
    s_abs[0] = '/';
    memcpy(s_abs + 1, rel, n);
    s_abs[n + 1] = 0;
    return s_abs;
}

/* sat_list/om_satfs_root_fp used to collapse every s_opendir/s_stat failure to a
 * flat -1 (sat_list) or OM_E_FIELD (om_satfs_root_fp), which hid the specific error -- including the new
 * OM_SD_TIMEOUT (sd.h) from exec_cmd's bounded wait (menu/lib/satiator/satiator.c, UPSTREAM.md "Local
 * changes"). A root-listing timeout must read as "SATIATOR: NO RESPONSE" in main.c, not a generic failure,
 * so both functions now propagate the real, om_sd_err()-mapped code instead. */
static int sat_list(void *ctx, const char *path, om_listing *l)
{
    s_stat_t *st = (s_stat_t *)s_dirent;
    int len;
    (void)ctx;
    om_listing_reset(l);
    if ((len = s_opendir(om_sd_path(abs_path(path)))) < 0)
        return om_sd_err(len);
    while ((len = s_stat(NULL, st, (int)sizeof s_dirent - 4)) > 0) {
        st->name[len] = 0;
        om_listing_add(l, st->name, (st->attrib & AM_DIR) != 0);
    }
    if (len == SAT_ETIMEDOUT)   /* satiator.h: a timeout is never a legitimate "no more entries" result */
        return om_sd_err(len);
    return 0;
}

static int read_prefix(const char *abs, void *dst, uint32_t want, uint32_t *got)
{
    uint32_t size;
    int fd, rc = om_sd_size(abs, &size);
    if (rc < 0) return rc;
    if (size < want) want = size;
    if ((fd = om_sd_open(abs)) < 0) return fd;
    rc = om_sd_read_at(fd, 0, dst, want);
    s_close(fd);
    *got = want;
    return rc;
}

int om_satfs_sysid(const char *path, om_sysid *out)
{
    uint32_t got;
    size_t n = strlen(path);
    const char *img = abs_path(path);
    int rc;
    if (n > 4 && (path[n - 3] | 32) == 'c' && (path[n - 2] | 32) == 'u' && (path[n - 1] | 32) == 'e') {
        char file[256];
        const char *base, *slash;
        size_t dl;
        if ((rc = read_prefix(img, s_io32, 4096, &got)) < 0) return rc;
        if ((rc = om_cue_first_file((const char *)s_io32, got, file, sizeof file)) < 0) return rc;
        base = file;
        for (slash = file; *slash; slash++)                 /* cue2desc strips FILE path components */
            if (*slash == '/' || *slash == '\\') base = slash + 1;
        slash = strrchr(path, '/');
        dl = slash ? (size_t)(slash - path) : 0;
        if (dl + 2 + strlen(base) > 255) return OM_E_FIELD;
        s_track[0] = '/';
        memcpy(s_track + 1, path, dl);
        s_track[1 + dl] = 0;
        if (dl) strcat(s_track, "/");
        strcat(s_track, base);
        img = s_track;
    }
    if ((rc = read_prefix(img, s_io32, 2352, &got)) < 0) return rc;
    return om_sysid_parse((const uint8_t *)s_io32, got, out);
}

static int sat_sysid(void *ctx, const char *path, om_sysid *out) { (void)ctx; return om_satfs_sysid(path, out); }

static int sat_cue_files(void *ctx, const char *path, char *out, uint32_t cap)
{   /* 2026-11-23 (clarification 7): the cue's first 4,096 B, as om_satfs_sysid reads it */
    uint32_t got;
    (void)ctx;
    if (read_prefix(abs_path(path), s_io32, 4096, &got) < 0) return 0;
    return om_cue_files((const char *)s_io32, got, out, cap);
}

static om_scan_fs s_fs = { NULL, sat_list, sat_sysid, NULL, sat_cue_files };
const om_scan_fs *om_satfs(void) { return &s_fs; }
void om_satfs_set_progress(void (*fn)(void *ctx, uint32_t dirs, uint32_t images)) { s_fs.progress = fn; }

int om_satfs_root_fp(om_scan_work *w, uint32_t *fp)
{   /* 2026-11-23 hardware report: the root is listed into the whole card-scan work area (free during om_card_load,
     * docs/ledger.md), not a 512-name om_listing -- a 698-name root kept 482 names there, never matched the PC's
     * fingerprint and forced a card scan on every boot. A root too big even for this (> 5,459 names) is OM_E_FULL. */
    s_stat_t *st = (s_stat_t *)s_dirent;
    om_fpacc a;
    int len;
    om_fpacc_init(&a, w, (uint32_t)sizeof *w);
    om_card_old_folder = 0;
    om_yield();
    if ((len = s_opendir(om_sd_path(abs_path("")))) < 0)
        return om_sd_err(len);
    for (;;) {                                  /* one Satiator command an entry: 702 on a 703-name root (sd.h) */
        om_yield();
        if ((len = s_stat(NULL, st, (int)sizeof s_dirent - 4)) <= 0)
            break;
        st->name[len] = 0;
        if ((st->attrib & AM_DIR) && old_folder_name(st->name)) om_card_old_folder = 1;   /* a folder, not a file */
        if (om_fpacc_add(&a, st->name) != OM_OK)
            return OM_E_FULL;
    }
    if (len == SAT_ETIMEDOUT)
        return om_sd_err(len);
    *fp = om_fpacc_finish(&a);
    return OM_OK;
}
