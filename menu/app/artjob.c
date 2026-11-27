/* menu/app/artjob.c -- see artjob.h. Every ART.PAK entry is read whole (thumb+hero+jp blobs), so its CRC is always
 * checked (spec §5.1 "never crash"; Global Constraints "every failure visible").
 * Big-card stutter (hardware report 2026-11-23, a 703-name card root): every path handed to the Satiator is resolved
 * from the card's root, which costs O(root names) on the card. So ART.PAK is opened once and kept open across jobs
 * (s_pak, closed by om_artjob_release), a cover held in ART.PAK is read from there first -- the card builder already
 * packs the game's own BOX.TGA as that cover (SPEC §5.6, bdf5540) -- and BOX.TGA is the fallback, opened with no
 * separate stat (its size is where its reads end). One art file at a time: ART.PAK is closed before a BOX.TGA opens.
 * BSD-3-Clause. */
#include <string.h>
#include "artjob.h"
#include "satiator.h"
#include "sd.h"

enum { P_IDLE, P_BOX, P_COVER, P_JP, P_DONE };

static int s_pak = -1;                                              /* ART.PAK, open across jobs */

static void fail(om_artjob *j, int rc, const char *msg) { if (!j->err) { j->err = rc; j->errmsg = msg; } }

void om_artjob_release(void)
{
    if (s_pak >= 0) { s_close(s_pak); s_pak = -1; }
}

static void close_fd(om_artjob *j)
{   /* a BOX.TGA closes; ART.PAK stays open for the next job */
    if (j->fd >= 0 && j->own) s_close(j->fd);
    j->fd = -1;
    j->own = 0;
}

static int plan_entry(om_artjob *j, const om_art_entry *e)
{
    static const uint8_t bit[3] = { OM_ART_THUMB, OM_ART_HERO, OM_ART_JP };
    const uint32_t sec[3] = { e->thumb_sec, e->hero_sec, e->jp_sec };
    uint32_t k, dst = 0;
    j->nseg = j->si = j->pos = 0;
    for (k = 0; k < 3; k++)
        if (e->flags & bit[k]) {
            j->seg[j->nseg].off = om_art_blob_pos(j->art, sec[k]);
            j->seg[j->nseg].len = om_art_blob_len(e, (int)k);
            j->seg[j->nseg].dst = dst;
            dst += j->seg[j->nseg].len;
            j->nseg++;
        }
    if (dst + 4 > j->stage_cap)
        return OM_E_FULL;
    if (s_pak < 0 && (s_pak = om_sd_open(OM_PATH_ARTPAK)) < 0) {
        int rc = s_pak;
        s_pak = -1;
        return rc;
    }
    j->fd = s_pak;
    j->own = 0;
    return OM_OK;
}

static uint32_t blob_off(const om_art_entry *e, int kind)
{
    uint32_t o = 0;
    if (kind > 0 && (e->flags & OM_ART_THUMB)) o += om_art_blob_len(e, 0);
    if (kind > 1 && (e->flags & OM_ART_HERO)) o += om_art_blob_len(e, 1);
    return o;
}

static void next_after_cover(om_artjob *j)
{
    int rc;
    if (!j->want_jp) { j->phase = P_DONE; return; }
    if ((rc = plan_entry(j, &j->jp)) != OM_OK) { fail(j, rc, "ART: ART.PAK open (JP)"); j->phase = P_DONE; return; }
    j->phase = P_JP;
}

static int start_cover_pak(om_artjob *j)
{   /* spec §5.6 steps 1-2; 1 = the job reads the cover from ART.PAK */
    if (j->art && j->key[0] && om_artpak_cover(j->art, j->key, j->disc, &j->cover)) {
        int rc = om_art_check(j->art, &j->cover);
        if (rc == OM_OK) rc = plan_entry(j, &j->cover);
        if (rc == OM_OK) { j->phase = P_COVER; return 1; }
        fail(j, rc, "ART: ART.PAK entry rejected");
    }
    return 0;
}

void om_artjob_start(om_artjob *j, const om_artpak *art, const om_lib_entry *e, const char *folder_path, uint8_t *stage, uint32_t cap)
{
    memset(j, 0, sizeof *j);
    j->fd = -1;
    j->art = art;
    j->stage = stage;
    j->stage_cap = cap;
    j->hero_src = OM_HERO_PLACEHOLDER;
    if (!e) {                                                       /* folder art: "F" + FNV-1a-32(path), disc 0 */
        om_folder_key(folder_path, j->key);
        j->disc = 0;
        if (art && om_artpak_find(art, j->key, 0, &j->cover) && (j->cover.flags & OM_ART_HERO)) {
            int rc = om_art_check(art, &j->cover);
            if (rc == OM_OK) rc = plan_entry(j, &j->cover);
            if (rc == OM_OK) { j->phase = P_COVER; return; }
            fail(j, rc, "ART: folder art rejected");
        }
        j->phase = P_DONE;
        return;
    }
    strcpy(j->key, e->key);
    j->disc = e->disc;
    if (art && e->key[0] && om_artpak_jp(art, e->key, e->disc, &j->jp) && om_art_check(art, &j->jp) == OM_OK)
        j->want_jp = 1;
    if (start_cover_pak(j))
        return;
    if (e->flags & OM_LIB_BOX) {                                    /* spec §5.6 step 3: no ART.PAK cover */
        const char *slash = strrchr(e->path, '/');
        size_t dl = slash ? (size_t)(slash - e->path) : 0;
        /* coordinator fix round 1, point 3: defence in depth alongside libidx's own 255 B path cap
         * (menu/src/libidx.c:85-86) -- box[] is already sized to that cap with margin (see the
         * DEVIATION comment on om_artjob.box in artjob.h), but this makes the bound self-contained
         * here too, instead of relying only on the caller never handing in a longer path. Named
         * error, same shape as the "too big"/"open failed" BOX.TGA cases below: shown on screen,
         * falls through past the ART.PAK cover attempt to JP/placeholder. */
        if (1 + dl + (dl ? 1u : 0u) + 7 + 1 > sizeof j->box) {
            fail(j, OM_E_FULL, "ART: BOX.TGA path too long");
            next_after_cover(j);
            return;
        }
        j->box[0] = '/';
        memcpy(j->box + 1, e->path, dl);
        j->box[1 + dl] = 0;
        if (dl) strcat(j->box, "/");
        strcat(j->box, "BOX.TGA");
        om_artjob_release();                                        /* one art file at a time */
        if ((j->fd = om_sd_open(j->box)) < 0) {
            if (j->fd != OM_SD_NOFILE && j->fd != om_sd_err(-FR_NO_PATH))   /* absent: the flag is stale, no error */
                fail(j, j->fd, "ART: BOX.TGA open");
            j->fd = -1;
            next_after_cover(j);
            return;
        }
        j->own = 1;
        j->seg[0].off = 0; j->seg[0].len = cap - 4; j->seg[0].dst = 0;   /* a short read ends it (box_end) */
        j->nseg = 1; j->si = j->pos = 0;
        j->phase = P_BOX;
        return;
    }
    next_after_cover(j);                                            /* step 4: placeholder */
}

static int pump(om_artjob *j, uint32_t *budget)
{
    while (*budget && j->si < j->nseg) {
        uint32_t n = j->seg[j->si].len - j->pos;
        int r;
        if (n > S_MAXBUF) n = S_MAXBUF;
        if (j->pos == 0 && (r = s_seek(j->fd, (int)j->seg[j->si].off, C_SEEK_SET)) < 0) return om_sd_err(r);
        r = s_read(j->fd, j->stage + j->seg[j->si].dst + j->pos, (int)n);
        if (r < 0) return om_sd_err(r);
        if (j->phase == P_BOX && (uint32_t)r < n) {                 /* BOX.TGA's end: its size, with no stat */
            if (!r && !j->pos) return OM_E_SHORT;
            j->seg[0].len = j->pos + (uint32_t)r;
            j->si = 1;
            (*budget)--;
            return 1;
        }
        if (r == 0) return OM_E_SHORT;
        j->pos += (uint32_t)r;
        (*budget)--;
        if (j->pos == j->seg[j->si].len) {
            if (j->phase == P_BOX) {                                /* filled to cap - 4: one byte more is too big */
                uint32_t extra;
                r = s_read(j->fd, &extra, 4);                       /* the stage's 4 B of slack (aligned) */
                if (r < 0) return om_sd_err(r);
                if (r > 0) return OM_E_FULL;
            }
            j->si++;
            j->pos = 0;
        }
    }
    return j->si == j->nseg;
}

static void copy_hero(om_art_out *out, const uint8_t *src)
{
    uint32_t i;
    for (i = 0; i < OM_HERO_PIXELS; i++)
        out->hero[i] = om_be16(src + 2 * i);                        /* blob is big-endian RGB555 (spec §2, HWF-01) */
}

static void copy_jp(om_art_out *out, const om_art_entry *e, const uint8_t *src)
{
    uint32_t y, row = e->jp_w / 2u;
    memset(out->jp, 0, sizeof out->jp);                             /* 0 = transparent (HWF-02) */
    for (y = 0; y < e->jp_h; y++)
        memcpy(out->jp + y * (OM_JP_CANVAS_W / 2), src + y * row, row);
}

int om_artjob_step(om_artjob *j, om_art_out *out, uint32_t max_reads)
{
    uint32_t budget = max_reads;
    om_tga_info ti;
    int r;
    while (j->phase != P_DONE && j->phase != P_IDLE) {
        r = pump(j, &budget);
        if (r < 0) {
            fail(j, r, j->phase != P_BOX ? "ART: ART.PAK read" : r == OM_E_FULL ? "ART: BOX.TGA too big" : "ART: BOX.TGA read");
            if (j->phase != P_BOX) { om_artjob_release(); close_fd(j); j->phase = P_DONE; break; }   /* reopened next job */
            close_fd(j);
            next_after_cover(j);                                    /* a bad BOX.TGA: the JP title may still come */
            if (!budget) break;
            continue;
        }
        if (r == 0)
            break;                                                  /* this frame's read budget is spent */
        if (j->phase == P_BOX) {
            close_fd(j);
            r = om_tga_decode(j->stage, j->seg[0].len, out->hero, &ti);
            if (r) fail(j, r, om_tga_strerror(r));
            else { j->hero_src = OM_HERO_BOX; j->tall = ti.layout == OM_TGA_TALL; }
            next_after_cover(j);
        } else if (j->phase == P_COVER) {
            if (!om_art_staged_crc_ok(&j->cover, j->stage))
                fail(j, OM_E_CRC, "ART: ART.PAK CRC");
            else {
                copy_hero(out, j->stage + blob_off(&j->cover, 1));
                j->hero_src = OM_HERO_PAK;
                j->tall = j->cover.hero_w == 128;
                if (j->want_jp && (j->cover.flags & OM_ART_JP) && j->jp.disc == j->cover.disc && !strcmp(j->jp.key, j->cover.key)) {
                    copy_jp(out, &j->cover, j->stage + blob_off(&j->cover, 2));
                    j->has_jp = 1;
                    j->want_jp = 0;
                }
            }
            next_after_cover(j);
        } else {                                                    /* P_JP */
            if (!om_art_staged_crc_ok(&j->jp, j->stage))
                fail(j, OM_E_CRC, "ART: ART.PAK CRC (JP)");
            else {
                copy_jp(out, &j->jp, j->stage + blob_off(&j->jp, 2));
                j->has_jp = 1;
            }
            j->phase = P_DONE;
        }
        if (!budget)
            break;
    }
    if (j->phase == P_DONE || j->phase == P_IDLE) {
        close_fd(j);
        return 1;
    }
    return 0;
}

void om_artjob_cancel(om_artjob *j)
{
    close_fd(j);
    j->phase = P_DONE;
}
