/* menu/app/sd.c -- see sd.h. libsatiator's buffer_xfer copies (len+3)/4 32-bit words through the caller's pointer
 * (satiator.c:114-125, read this session), for data and for file names alike. So names are copied to an aligned
 * buffer, every destination must be 4-aligned, and reads keep 4 bytes of slack. libsatiator's negative FRESULT
 * values (-1..-19) would collide with OM_E_* (-1..-9), so they are moved to -101..-119 (om_sd_err), which
 * om_strerror names. BSD-3-Clause. */
#include <string.h>
#include "om_fmt.h"
#include "satiator.h"
#include "sd.h"
#include "yield.h"

static uint32_t s_path[76];      /* 304 B, 4-aligned: paths <= 255 B (spec §2) */
static uint32_t s_stat_buf[76];

int om_sd_err(int fr) { return fr < 0 ? -100 + fr : fr; }

const char *om_sd_path(const char *p)
{
    size_t n = strlen(p);
    if (n > 255) n = 255;
    memcpy(s_path, p, n);
    ((char *)s_path)[n] = 0;
    return (const char *)s_path;
}

int om_sd_size(const char *path, uint32_t *size)
{
    int rc;
    om_yield();                  /* a path command: on a big card's root it is the long one (yield.h) */
    rc = s_stat(om_sd_path(path), (s_stat_t *)s_stat_buf, (int)sizeof s_stat_buf - 4);
    if (rc < 0)
        return om_sd_err(rc);
    *size = ((s_stat_t *)s_stat_buf)->size;
    return 0;
}

int om_sd_open(const char *path)
{
    int fd;
    om_yield();
    fd = s_open(om_sd_path(path), FA_READ | FA_OPEN_EXISTING);
    return fd < 0 ? om_sd_err(fd) : fd;
}

int om_sd_read_at(int fd, uint32_t off, void *dst, uint32_t len)
{
    uint8_t *p = (uint8_t *)dst;
    uint32_t got = 0;
    int r;
    if (((uintptr_t)dst) & 3)
        return OM_E_FIELD;
    if ((r = s_seek(fd, (int)off, C_SEEK_SET)) < 0)
        return om_sd_err(r);
    while (got < len) {
        uint32_t n = len - got > S_MAXBUF ? S_MAXBUF : len - got;
        om_yield();
        r = s_read(fd, p + got, (int)n);
        if (r < 0) return om_sd_err(r);
        if (r == 0) return OM_E_SHORT;
        got += (uint32_t)r;
    }
    return 0;
}

int om_sd_read_all(const char *path, void *dst, uint32_t cap, uint32_t *len)
{
    uint32_t size;
    int fd, rc = om_sd_size(path, &size);
    if (rc < 0)
        return rc;
    if (size + 4 > cap)
        return OM_E_FULL;
    if ((fd = om_sd_open(path)) < 0)
        return fd;
    rc = om_sd_read_at(fd, 0, dst, size);
    s_close(fd);
    if (rc == 0)
        *len = size;
    return rc;
}

static uint32_t s_wbuf[S_MAXBUF / 4];   /* 2 KB, 4-aligned: the helper's scratch2k (chunk staging, names, read-back) */
om_sdw_gap_fn om_sd_gap;                /* main.c's sd_gap (one V-blank, the hook's count); unset in host tests */
uint32_t om_sd_gap_late;                /* gaps that ran out of time (main.c sd_gap: a V-blank hook that never ticked) */

/* every menu write goes through menu/src/sdwrite.c (<= 2,044 B chunks, s_sync each, a V-blank between
 * chunks, one retry per chunk; see sdwrite.h), with this file's 2 KB buffer as its scratch. One file open, so the
 * 2-file rule holds with the music paused by the caller. The first failure is returned, named (om_sd_err's codes),
 * and the caller's toast shows it with its rc -- never "saved". */
int om_sd_write_file(const char *path, om_sd_src_fn src, void *ctx)
{
    return om_sdw_write_src(path, src, ctx, 0, (uint8_t *)s_wbuf, om_sd_gap, 0);
}

int om_sd_write_buf(const char *path, const void *data, uint32_t len)
{
    return om_sdw_write_file(path, (const uint8_t *)data, len, 0, (uint8_t *)s_wbuf, om_sd_gap, 0);
}

int om_sd_replace(const char *path, const void *data, uint32_t len)
{
    return om_sdw_replace(path, (const uint8_t *)data, len, (uint8_t *)s_wbuf, om_sd_gap, 0);
}

int om_sd_replace_in(const char *dir, const char *path, const void *data, uint32_t len)
{
    s_mkdir(om_sd_path(dir));            /* an existing folder is fine (FR_EXIST); a failure shows as the write's rc */
    return om_sd_replace(path, data, len);
}
