/* menu/app/cheats.c -- see cheats.h. BSD-3-Clause. */
#include "cheats.h"
#include "om_fmt.h"
#include "satiator.h"
#include "sd.h"

static int load(int fd, const char *key, uint8_t *stage, om_ch_file *f, uint32_t *on, uint32_t *cnt)
{
    om_cp_hdr h;
    uint32_t done = 0, crc = 0, off = 0, len = 0, n;
    const uint8_t *row;
    int rc, found = 0;
    if ((rc = om_sd_read_at(fd, 0, stage, OM_HEADER_SIZE)) < 0 || (rc = om_cp_header(stage, &h)) < 0)
        return rc;
    while (done < h.count) {
        n = h.count - done > OM_CP_PIECE / OM_CP_ROW ? OM_CP_PIECE / OM_CP_ROW : h.count - done;
        if ((rc = om_sd_read_at(fd, OM_HEADER_SIZE + done * OM_CP_ROW, stage, n * OM_CP_ROW)) < 0)
            return rc;
        crc = om_crc32(crc, stage, n * OM_CP_ROW);
        if (!found && (row = om_cp_scan(stage, n, key)) != 0) {
            if ((rc = om_cp_row(&h, row, &off, &len)) < 0)
                return rc;
            found = 1;
        }
        done += n;
    }
    if (crc != h.table_crc)
        return OM_E_CRC;
    if (!found)
        return 0;
    if ((rc = om_sd_read_at(fd, off, stage, len)) < 0 || (rc = f ? om_cp_decode(stage, len, f)
                                                                 : om_cp_count(stage, len, on, cnt)) < 0)
        return rc;
    return 1;
}

static int lookup(const char *key, uint8_t *stage, uint32_t cap, om_ch_file *f, uint32_t *on, uint32_t *n)
{
    int fd, rc;
    if (cap < OM_CP_STAGE || ((uintptr_t)stage & 3))
        return OM_E_FIELD;
    fd = om_sd_open(OM_PATH_CHEATPAK);
    if (fd == OM_SD_NOFILE)
        fd = om_sd_open(OM_PATH_CHEATPAK ".NEW");
    if (fd < 0)
        return fd;
    rc = load(fd, key, stage, f, on, n);
    s_close(fd);
    return rc;
}

int om_cheats_count(const char *key, uint8_t *stage, uint32_t cap, uint32_t *on, uint32_t *n)
{
    return lookup(key, stage, cap, 0, on, n);
}

int om_cheats_load(const char *key, uint8_t *stage, uint32_t cap, om_ch_file *f)
{
    return lookup(key, stage, cap, f, 0, 0);
}
