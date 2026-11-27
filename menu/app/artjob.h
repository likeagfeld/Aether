/* menu/app/artjob.h -- the incremental art loader for one highlighted item (spec §5.6, §7.4). Portable: reads
 * through sd.c, decodes into an om_art_out canvas; menu/ui/art.c copies the canvas into VDP1 VRAM. BSD-3-Clause. */
#ifndef OM_ARTJOB_H
#define OM_ARTJOB_H
#include <stdint.h>
#include "artpak.h"
#include "libidx.h"
#include "omlayout.h"
#include "tga.h"

#define OM_AJ_READS_PER_STEP 1   /* floor(8 ms / 8.2 ms) at 245 KB/s (user's card, FW163), at least 1 */
#define OM_HERO_PLACEHOLDER  0
#define OM_HERO_BOX          1
#define OM_HERO_PAK          2

typedef struct {
    uint16_t hero[OM_HERO_PIXELS];
    uint8_t jp[OM_JP_CANVAS_W / 2 * OM_JP_CANVAS_H];
} om_art_out;

typedef struct {
    const om_artpak *art;
    uint8_t *stage;
    uint32_t stage_cap;
    int phase, fd;
    uint8_t own;                 /* fd is this job's BOX.TGA (closed with the job), else the shared ART.PAK */
    struct { uint32_t off, len, dst; } seg[3];
    uint32_t nseg, si, pos;
    om_art_entry cover, jp;
    int want_jp;
    char key[17];
    uint8_t disc;
    /* DEVIATION (safety, this session): the brief's listed size was box[260]. e->path may be up to 255 B
     * (spec §2; menu/src/libidx.c:85-86, menu/src/scan.h:50), and this buffer holds "/" + dirname + "/" +
     * "BOX.TGA" + NUL, which for a near-255-byte path's directory component overflows a 260-byte buffer by
     * several bytes into the fields below it (hero_src etc.) -- not exercised by the brief's host test
     * (whose paths are short), so it would have shipped silently. Bumped to 272 B: worst case is
     * 1 + 254 + 1 + 8 + 1 = 265 B (dl <= 254 when the path's final component is at least 1 B), leaving
     * margin. No other field, no test, and no interface changed. */
    char box[272];
    int hero_src, has_jp, err;
    uint8_t tall;
    const char *errmsg;
} om_artjob;

void om_artjob_start(om_artjob *j, const om_artpak *art, const om_lib_entry *e, const char *folder_path, uint8_t *stage, uint32_t cap);
int om_artjob_step(om_artjob *j, om_art_out *out, uint32_t max_reads);
void om_artjob_cancel(om_artjob *j);
/* big-card stutter (2026-11-23): ART.PAK stays open across jobs (one path resolution per card load, not per cover);
 * this closes it -- before any other SD file opens (om_art_invalidate), and before a BOX.TGA fallback opens */
void om_artjob_release(void);
#endif
