/* menu/app/fake/fakesat.c -- --app-fake only (plan D6): libsatiator's s_* API (menu/lib/satiator/satiator.h) over a
 * compiled-in OMFAKEFS blob, plus a RAM write store (4 files x 8 KB) so FAVS/RECENT/SETTINGS writes persist for the
 * run. Honours the real limits the menu relies on (<= S_MAXBUF per call, readdir ends at <= 0; HWF-22). Never linked
 * into the shipping menu (tools/sbuild.py _app_sources). s_reset_to_satiator returns -1: no BIOS jump. BSD-3-Clause. */
#include <string.h>
#include "fakefs.h"
#include "fakesat.h"
#include "satiator.h"
#ifdef OMSAT_FAKECARD                            /* the Saturn fake build; the host tests (test_fakefs) read instantly */
#include "uitext.h"
#endif

#define FD_MAX 6
#define WR_MAX 4
#define WR_CAP 8192u
/* gone: the file was unlinked; kept as a slot only when the blob has the same path, which it hides */
typedef struct { char path[256]; uint32_t len; uint8_t used, gone; uint8_t data[WR_CAP]; } wfile;
typedef struct { uint8_t used, wr; uint32_t pos, size; const uint8_t *src; wfile *w; } ofile;

static om_fakefs s_fs;
static const char *s_xpath;                      /* Task 13c round 2: one extra read-only file (om_fakesat_cd_file) */
static const uint8_t *s_xdata;
static uint32_t s_xsize;
static wfile s_w[WR_MAX];
static ofile s_fd[FD_MAX];
static char s_dir[256];
static uint32_t s_dir_i;
static int s_fd_max;                             /* the most files open at once */

void om_fakesat_mount(const uint8_t *blob, uint32_t len) { om_fakefs_open(&s_fs, blob, len); }

const uint8_t *om_fakesat_data(const char *path)
{
    uint32_t off, size;
    return om_fakefs_find(&s_fs, path, &off, &size) == 1 ? s_fs.base + off : 0;   /* 1 = a file */
}

int om_fakesat_has(const char *path) { return om_fakesat_data(path) != 0; }

/* the working folder (2026-11-25, the popups: svmgr.h names its files from /aether after s_chdir): a name without a
 * leading '/' is resolved from it, as FatFs does with FF_FS_RPATH; "" = the root. Two results alternate (s_rename) */
static char s_cwd[64];
static char s_np[2][320];
static int s_npi;
static const char *norm(const char *p)
{
    char *o;
    size_t n;
    if (*p == '/' || !s_cwd[0]) { while (*p == '/') p++; return p; }
    o = s_np[s_npi ^= 1];
    n = strlen(s_cwd);
    memcpy(o, s_cwd, n);
    o[n] = '/';
    strncpy(o + n + 1, p, sizeof s_np[0] - n - 2);
    o[sizeof s_np[0] - 1] = 0;
    return o;
}

static wfile *wfind(const char *p)
{
    int i;
    for (i = 0; i < WR_MAX; i++)
        if (s_w[i].used && !strcmp(s_w[i].path, p))
            return &s_w[i];
    return NULL;
}

static ofile *fdp(int fd) { return fd >= 0 && fd < FD_MAX && s_fd[fd].used ? &s_fd[fd] : NULL; }

int s_mode(enum satiator_mode mode) { (void)mode; return 0; }

/* boot-gap measurement (fake build only): aether/FAKESCRIPT.SDLAT holds a decimal millisecond cost charged to every
 * path command (s_open, s_stat of a path, s_opendir) -- a model of the Satiator resolving a path through a big exFAT
 * root, which the instant fake card does not have. No marker: no cost. */
#ifdef OMSAT_FAKECARD
static int s_lat_ms = -1;
#endif
static void path_time(void)
{
#ifdef OMSAT_FAKECARD
    uint32_t off, size, i;
    if (s_lat_ms < 0) {
        s_lat_ms = 0;
        if (om_fakefs_find(&s_fs, "aether/FAKESCRIPT.SDLAT", &off, &size) == 1)
            for (i = 0; i < size && s_fs.base[off + i] >= '0' && s_fs.base[off + i] <= '9'; i++)
                s_lat_ms = s_lat_ms * 10 + (s_fs.base[off + i] - '0');
    }
    for (i = 0; i < (uint32_t)s_lat_ms; i++) {
        uint16_t t0 = om_ut_frc();
        while (om_ut_frc_us((uint16_t)(om_ut_frc() - t0)) < 1000u)
            ;
    }
#endif
}

volatile s_diag_t s_diag;   /* satiator.h: the real exec_cmd's counters (audio.c reads ok); never moved here */

int s_open(const char *filename, int flags)
{
    const char *p = norm(filename);
    uint32_t off, size;
    wfile *w = wfind(p);
    int fd, i;
    path_time();
    for (fd = 0; fd < FD_MAX && s_fd[fd].used; fd++) ;
    if (fd == FD_MAX)
        return -FR_TOO_MANY_OPEN_FILES;
    memset(&s_fd[fd], 0, sizeof s_fd[fd]);
    if (w && w->gone && !(flags & FA_WRITE))
        return -FR_NO_FILE;
    if ((flags & FA_WRITE) && !w && (flags & FA_OPEN_ALWAYS) && om_fakefs_find(&s_fs, p, &off, &size) == 1) {
        s_fd[fd].src = s_fs.base + off;      /* an existing blob file opened only to make sure it exists */
        s_fd[fd].size = size;                /* (sdwrite.c's replace step 1): no slot, and no write through it */
    } else if (flags & FA_WRITE) {
        if (!w) {
            for (i = 0; i < WR_MAX && s_w[i].used; i++) ;
            if (i == WR_MAX)
                return -FR_DENIED;
            w = &s_w[i];
            w->used = 1;
            strncpy(w->path, p, sizeof w->path - 1);
            w->path[sizeof w->path - 1] = 0;
            w->len = 0;
        }
        if ((flags & FA_CREATE_ALWAYS) || w->gone)
            w->len = 0;
        w->gone = 0;
        s_fd[fd].wr = 1;
    } else if (!w && s_xpath && !strcmp(p, s_xpath)) {
        s_fd[fd].src = s_xdata;
        s_fd[fd].size = s_xsize;
    } else if (!w) {
        if (om_fakefs_find(&s_fs, p, &off, &size) != 1)
            return -FR_NO_FILE;
        s_fd[fd].src = s_fs.base + off;
        s_fd[fd].size = size;
    }
    if (w) {
        s_fd[fd].w = w;
        s_fd[fd].src = w->data;
        s_fd[fd].size = w->len;
    }
    s_fd[fd].used = 1;
    {
        int i2, n = 0;
        for (i2 = 0; i2 < FD_MAX; i2++) n += s_fd[i2].used;
        if (n > s_fd_max) s_fd_max = n;
    }
    return fd;
}

int om_fakesat_fd_max(void) { return s_fd_max; }

int s_close(int fd) { ofile *o = fdp(fd); if (!o) return -FR_INVALID_OBJECT; o->used = 0; return 0; }

int s_seek(int fd, int offset, int whence)
{
    ofile *o = fdp(fd);
    long np;
    if (!o) return -FR_INVALID_OBJECT;
    np = whence == C_SEEK_SET ? offset : whence == C_SEEK_CUR ? (long)o->pos + offset : (long)o->size + offset;
    if (np < 0) return -FR_INVALID_PARAMETER;
    o->pos = (uint32_t)np;
    return (int)np;
}

/* with the loading script's card (aether/FAKESCRIPT.LOADING) a read takes 1 us a byte (about 1 MB/s), so
 * the loading screen's steps and its background preload take frames as they would on a card. An ASSUMED rate, not a
 * measurement (spike S3 has no Satiator figure yet); the other scripts keep instant reads. */
static int s_slow = -1;            /* microseconds a byte: 0 instant, 1 (the loading script), 4 (--sd-hw) */

static void card_time(uint32_t bytes)
{
#ifdef OMSAT_FAKECARD
    uint16_t t0;
    if (s_slow < 0)                /* --sd-hw: the user's measured card rate, 245-296 KB/s (one 2 KB s_read ~8 ms) */
        s_slow = om_fakesat_has("aether/FAKESCRIPT.SDHW") ? 4 : om_fakesat_has("aether/FAKESCRIPT.LOADING");
    if (!s_slow)
        return;
    t0 = om_ut_frc();
    while (om_ut_frc_us((uint16_t)(om_ut_frc() - t0)) < bytes * (uint32_t)s_slow)
        ;
#else
    (void)bytes;
    (void)s_slow;
#endif
}

int s_read(int fd, void *buf, int len)
{
    ofile *o = fdp(fd);
    uint32_t n;
    if (!o || len < 0 || len > S_MAXBUF) return -FR_INVALID_PARAMETER;
    if (o->w) o->size = o->w->len;
    n = o->pos >= o->size ? 0 : o->size - o->pos;
    if (n > (uint32_t)len) n = (uint32_t)len;
    memcpy(buf, o->src + o->pos, n);
    o->pos += n;
    card_time(n);
    return (int)n;
}

int s_write(int fd, const void *buf, int len)
{
    ofile *o = fdp(fd);
    if (!o || !o->wr || len < 0 || len > S_MAXBUF) return -FR_INVALID_PARAMETER;
    if (o->pos + (uint32_t)len > WR_CAP) return -FR_DENIED;
    memcpy(o->w->data + o->pos, buf, (uint32_t)len);
    o->pos += (uint32_t)len;
    if (o->pos > o->w->len) o->w->len = o->pos;
    return len;
}

int s_sync(int fd) { return fdp(fd) ? 0 : -FR_INVALID_OBJECT; }
int s_truncate(int fd) { ofile *o = fdp(fd); if (!o || !o->wr) return -FR_DENIED; o->w->len = o->pos; return (int)o->pos; }

static int fill_stat(s_stat_t *st, int statsize, const char *name, uint32_t size, int is_dir)
{
    int n = (int)strlen(name);
    st->size = size;
    st->date = st->time = 0;
    st->attrib = is_dir ? AM_DIR : 0;
    if (n > statsize - 9) n = statsize - 9;          /* s_stat_t header: u32 size, u16 date, u16 time, u8 attrib */
    memcpy(st->name, name, (size_t)n);
    if (n < statsize - 9) st->name[n] = 0;
    return n;
}

int s_stat(const char *filename, s_stat_t *st, int statsize)
{
    char name[256], full[520];
    uint32_t off = 0, size = 0;
    int is_dir, k;
    if (statsize < 9)                  /* fix round 1 (review-9.md): matches satiator.c:291-292; without this,
                                         * fill_stat's `statsize - 9` goes negative and becomes a huge memcpy
                                         * length after the implicit int->size_t cast -- an OOB write. */
        return -FR_INVALID_PARAMETER;
    if (!filename) {
        if (!om_fakefs_child(&s_fs, s_dir, s_dir_i, name, &is_dir))
            return -FR_NO_FILE;                       /* <= 0 ends the readdir loop (HWF-22) */
        s_dir_i++;
        if (!is_dir) {
            strcpy(full, s_dir);
            if (full[0]) strcat(full, "/");
            strcat(full, name);
            om_fakefs_find(&s_fs, full, &off, &size);
        }
        return fill_stat(st, statsize, name, size, is_dir);
    }
    path_time();
    {
        const char *p = norm(filename), *base = strrchr(p, '/');
        wfile *w = wfind(p);
        strncpy(name, base ? base + 1 : p, sizeof name - 1);
        name[sizeof name - 1] = 0;
        if (w)
            return w->gone ? -FR_NO_FILE : fill_stat(st, statsize, name, w->len, 0);
        if (s_xpath && !strcmp(p, s_xpath))
            return fill_stat(st, statsize, name, s_xsize, 0);
        if (!*p)
            return fill_stat(st, statsize, "", 0, 1);
        if (!(k = om_fakefs_find(&s_fs, p, &off, &size)))
            return -FR_NO_FILE;
        return fill_stat(st, statsize, name, k == 2 ? 0 : size, k == 2);
    }
}

int s_opendir(const char *filename)
{
    uint32_t off, size;
    const char *p = norm(filename);
    path_time();
    if (*p && om_fakefs_find(&s_fs, p, &off, &size) != 2)
        return -FR_NO_PATH;
    strncpy(s_dir, p, sizeof s_dir - 1);
    s_dir[sizeof s_dir - 1] = 0;
    while (s_dir[0] && s_dir[strlen(s_dir) - 1] == '/') s_dir[strlen(s_dir) - 1] = 0;
    s_dir_i = 0;
    return 0;
}

/* FatFs semantics -- a written file is renamed; onto a name that exists, -FR_EXIST; a blob file cannot
 * be renamed (-FR_DENIED: no caller does) */
int s_rename(const char *old, const char *new)
{
    uint32_t off, size;
    const char *n = norm(new);
    wfile *w = wfind(norm(old)), *t = wfind(n);
    if (!w || w->gone)
        return om_fakefs_find(&s_fs, norm(old), &off, &size) == 1 ? -FR_DENIED : -FR_NO_FILE;
    if (t ? !t->gone : om_fakefs_find(&s_fs, n, &off, &size) == 1)
        return -FR_EXIST;
    if (t) t->used = 0;                      /* a gone slot for the target: the renamed file hides the blob now */
    strncpy(w->path, n, sizeof w->path - 1);
    w->path[sizeof w->path - 1] = 0;
    return 0;
}
int s_mkdir(const char *filename) { (void)filename; return 0; }
int s_unlink(const char *filename)
{   /* a blob file is hidden by a gone slot (the blob is read-only); -FR_NO_FILE when absent */
    const char *p = norm(filename);
    uint32_t off, size;
    int i, blob = om_fakefs_find(&s_fs, p, &off, &size) == 1;
    wfile *w = wfind(p);
    if (w ? w->gone : !blob)
        return -FR_NO_FILE;
    if (!w) {
        for (i = 0; i < WR_MAX && s_w[i].used; i++) ;
        if (i == WR_MAX)
            return -FR_DENIED;
        w = &s_w[i];
        w->used = 1;
        strncpy(w->path, p, sizeof w->path - 1);
        w->path[sizeof w->path - 1] = 0;
    }
    w->len = 0;
    w->gone = (uint8_t)blob;
    w->used = (uint8_t)blob;
    return 0;
}
int s_chdir(const char *filename)
{
    while (*filename == '/') filename++;
    strncpy(s_cwd, filename, sizeof s_cwd - 1);
    s_cwd[sizeof s_cwd - 1] = 0;
    return 0;
}
int s_getcwd(char *filename, int buflen)
{
    if (buflen > 1) { filename[0] = '/'; strncpy(filename + 1, s_cwd, (size_t)buflen - 2); filename[buflen - 1] = 0; }
    return 0;
}
int s_settime(uint32_t fattime) { (void)fattime; return 0; }
int s_emulate(const char *filename) { (void)filename; return -FR_DENIED; }
int s_get_fw_version(char *buf, int buflen)
{   /* aether/FAKESCRIPT.FW's bytes (fakecard --fw); "READFAIL" there = the read-failure path ("FAKE",
     * rc -1); no marker = a passing "v163", so every other capture stays free of firmware messages */
    uint32_t off, size;
    if (buflen < 5) return -1;
    if (om_fakefs_find(&s_fs, "aether/FAKESCRIPT.FW", &off, &size) != 1) { memcpy(buf, "v163", 5); return 4; }
    if (size == 8 && !memcmp(s_fs.base + off, "READFAIL", 8)) { memcpy(buf, "FAKE", 5); return -1; }
    if (size > (uint32_t)buflen - 1) size = (uint32_t)buflen - 1;
    memcpy(buf, s_fs.base + off, size);
    buf[size] = 0;
    return (int)size;
}
int s_get_bootloader_version(uint32_t *version) { *version = 0; return -1; }
int s_get_serial_number(uint32_t *serial) { *serial = 0; return -1; }
int s_get_sd_latency(uint16_t latency_us[], int *errors, int count) { (void)latency_us; (void)count; *errors = 0; return -1; }
int s_format_sd_card(int flags) { (void)flags; return -1; }
int s_reset_to_satiator(void) { return -1; }

#ifdef OMSAT_FAKECARD
#include "cdblk.h"
#include "isodir.h"
#include "om_fmt.h"

/* Task 13c round 2: the fake card is compiled into HWRAM and cannot hold the ~470 KB LOADBG.BIN, so the fake build
 * reads it from its own disc (tools/sbuild.py puts it in the fake ISO's root) before API mode -- as the brief allows,
 * fake build only -- into dst (LWRAM's VDP1-snapshot range: the fake build never hands off), and serves it as the
 * card file `card_path`. The CD block is read with cdblk.c's own calls (32 sectors a read, its frame-capped waits). */
int om_fakesat_cd_file(const char *iso_name, const char *card_path, uint8_t *dst, uint32_t cap)
{
    uint32_t lba, size, got, n;
    int rc, r4[4];
    om_cd_spin_cap(100000u);
    om_cd_reset(r4);
    if ((rc = om_cd_read(dst, (int32_t)(OM_ISO_PVD_LBA + 150u), 1)) != 0) goto out;
    if ((rc = om_iso_root(dst, &lba, &size)) != 0) goto out;
    n = (size + 2047u) >> 11;
    if (n > 8u) n = 8u;
    if ((rc = om_cd_read(dst, (int32_t)(lba + 150u), (int32_t)n)) != 0) goto out;
    if ((rc = om_iso_find(dst, n << 11, iso_name, &lba, &size)) != 0) goto out;
    if (size > cap) { rc = OM_E_RANGE; goto out; }
    for (got = 0; got < size; got += n << 11) {
        n = (size - got + 2047u) >> 11;
        if (n > 32u) n = 32u;
        if ((rc = om_cd_read(dst + got, (int32_t)(lba + 150u + (got >> 11)), (int32_t)n)) != 0) goto out;
    }
    s_xpath = norm(card_path);
    s_xdata = dst;
    s_xsize = size;
out:
    om_cd_reset(r4);
    om_cd_spin_cap(0);
    return rc;
}
#endif
