/* menu/app/launch.c -- see launch.h (portable part). The body load overwrites OM_LW_GAMESDB/ARTTAB, so clobbered=1
 * tells the caller to reload them if the launch is abandoned (CRC failure, cancel). BSD-3-Clause. */
#include <string.h>
#include "launch.h"
#include "omlayout.h"
#include "satiator.h"
#include "sd.h"
#include "discset.h"

int om_launch_split(const char *path, char dir[260], char image[260])
{
    const char *slash = strrchr(path, '/');
    size_t dl = slash ? (size_t)(slash - path) : 0, il = strlen(slash ? slash + 1 : path);
    if (dl + 1 > 255 || il > 255 || !il)
        return OM_E_FIELD;
    dir[0] = '/';
    memcpy(dir + 1, path, dl);
    dir[1 + dl] = 0;
    memcpy(image, slash ? slash + 1 : path, il + 1);
    return OM_OK;
}

int om_launch_begin(const char *path, uint8_t area, uint32_t flags, const om_launch_opts *o, const om_launch_bufs *b,
                    om_launch_info *li)
{
    char dir[260], image[260];
    int rc;
    memset(li, 0, sizeof *li);
    if ((rc = om_launch_split(path, dir, image)) < 0) return rc;
    if ((rc = om_handoff_fill2(b->handoff, dir, image, area, flags, o)) < 0) return rc;
    if (o->set_lib)                                       /* a multi-disc set: its discs for the lid swap (discset.h) */
        (void)om_discset_fill(b->handoff, (const om_libidx *)o->set_lib, o->set_idx);
    return rc;
}

int om_launch_open(om_launch_ld *ld, const om_launch_bufs *b, om_launch_info *li)
{
    int rc;
    (void)b;
    ld->fd = -1;
    ld->done = ld->hdr_got = 0;
    ld->crc = 0;
    li->body_size = 0;
    if ((rc = om_sd_size(OM_PATH_LAUNCHER, &ld->size)) < 0) return rc;
    if (ld->size < OM_LNCH_HDR) return OM_E_SHORT;
    if ((ld->fd = om_sd_open(OM_PATH_LAUNCHER)) < 0) { rc = ld->fd; ld->fd = -1; return rc; }
    return OM_OK;
}

int om_launch_read(om_launch_ld *ld, const om_launch_bufs *b, om_launch_info *li, uint32_t chunk)
{
    uint32_t n;
    int rc;
    if (ld->fd < 0) return OM_E_FIELD;
    if (ld->hdr_got < OM_LNCH_HDR) {                     /* the 4 KB header first, in the same steps */
        n = OM_LNCH_HDR - ld->hdr_got;
        if (n > chunk) n = chunk;
        if ((rc = om_sd_read_at(ld->fd, ld->hdr_got, b->hdr + ld->hdr_got, n)) != 0) {
            om_launch_abort(ld);
            return rc;
        }
        ld->hdr_got += n;
        if (ld->hdr_got < OM_LNCH_HDR) return 1;
        rc = om_lnch_parse(b->hdr, ld->size, &ld->inf);
        if (!rc && ld->inf.body_size + 4 > b->body_cap) rc = OM_E_RANGE;
        if (rc) {
            om_launch_abort(ld);
            return rc;
        }
        return ld->inf.body_size ? 1 : om_launch_read(ld, b, li, chunk);
    }
    n = ld->inf.body_size - ld->done;
    if (n > chunk) n = chunk;
    if (n) {
        li->clobbered = 1;
        if ((rc = om_sd_read_at(ld->fd, OM_LNCH_HDR + ld->done, b->body + ld->done, n)) != 0) {
            om_launch_abort(ld);
            return rc;
        }
        ld->crc = om_crc32(ld->crc, b->body + ld->done, n);
        ld->done += n;
    }
    if (ld->done < ld->inf.body_size) return 1;
    om_launch_abort(ld);
    if (ld->crc != ld->inf.body_crc) return OM_E_CRC;
    li->body_size = ld->inf.body_size;
    return 0;
}

void om_launch_abort(om_launch_ld *ld)
{
    if (ld->fd >= 0) s_close(ld->fd);
    ld->fd = -1;
}

int om_launch_prepare(const char *path, uint8_t area, uint32_t flags, const om_launch_opts *o, const om_launch_bufs *b,
                      om_launch_info *li)
{
    om_launch_ld ld;
    int rc;
    if ((rc = om_launch_begin(path, area, flags, o, b, li)) < 0) return rc;
    if ((rc = om_launch_open(&ld, b, li)) < 0) return rc;
    while ((rc = om_launch_read(&ld, b, li, 0xFFFFFFFFu)) > 0)
        ;
    return rc;
}
