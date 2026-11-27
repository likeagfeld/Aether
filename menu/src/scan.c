/* menu/src/scan.c -- see scan.h. Mirrors tools/omsat/cardscan.py step for step; contract/fixtures/scan_tree.* gates
 * both implementations. Each directory listing is collected in full before any other filesystem call: libsatiator's
 * readdir state is one global iterator (s_opendir + s_stat(NULL,...), HWF-22). BSD-3-Clause. */
#include <string.h>
#include "match.h"
#include "scan.h"

void om_listing_reset(om_listing *l) { l->n = 0; l->used = 0; l->overflow = 0; }

int om_listing_add(om_listing *l, const char *name, int is_dir)
{
    uint32_t len = (uint32_t)strlen(name) + 1;
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return OM_OK;
    if (l->n >= OM_SCAN_MAXNAMES || l->used + len > OM_SCAN_NAMEBUF) { l->overflow = 1; return OM_E_FULL; }
    memcpy(l->buf + l->used, name, len);
    l->off[l->n] = (uint16_t)l->used;
    l->dir[l->n] = (uint8_t)(is_dir != 0);
    l->used += len;
    l->n++;
    return OM_OK;
}

void om_listing_sort(om_listing *l)
{
    uint32_t i, j;
    for (i = 0; i < l->n; i++) {
        uint16_t v = (uint16_t)i;
        for (j = i; j > 0 && strcmp(l->buf + l->off[v], l->buf + l->off[l->order[j - 1]]) < 0; j--)
            l->order[j] = l->order[j - 1];
        l->order[j] = v;
    }
}

const char *om_listing_name(const om_listing *l, uint32_t i) { return l->buf + l->off[l->order[i]]; }
int om_listing_isdir(const om_listing *l, uint32_t i) { return l->dir[l->order[i]] & 1; }
int om_listing_ref(const om_listing *l, uint32_t i) { return (l->dir[l->order[i]] >> 1) & 1; }

static int ieq(const char *a, const char *b)
{   /* ASCII case-insensitive (FAT names) */
    for (; *a && *b; a++, b++)
        if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b)) return 0;
    return *a == *b;
}

static int is_image(const char *n)
{
    size_t k = strlen(n);
    if (k < 4 || n[k - 4] != '.') return 0;
    return ((n[k - 3] | 32) == 'c' && (n[k - 2] | 32) == 'u' && (n[k - 1] | 32) == 'e')
        || ((n[k - 3] | 32) == 'i' && (n[k - 2] | 32) == 's' && (n[k - 1] | 32) == 'o');
}

static int join(char *dst, const char *a, const char *b);

/* 2026-11-23 (clarification 7): every file a .cue of this folder names in its FILE lines is marked (dir[] bit 1) and is
 * then never an image -- LARRY 6's game.cue + game.iso is one game. The names go into buf (the scan's sysid array,
 * which nothing holds at this point: no new work memory), the cue's path into path. */
static void mark_cue_refs(const om_scan_fs *fs, om_listing *l, const char *dir, int at_root, char *path, char *buf,
                          uint32_t cap)
{
    uint32_t i, j;
    if (!fs->cue_files)
        return;
    for (i = 0; i < l->n; i++) {
        const char *nm = om_listing_name(l, i), *r;
        size_t k = strlen(nm);
        int n;
        if (om_listing_isdir(l, i) || om_is_hidden_name(nm, at_root) || k < 4 || !ieq(nm + k - 4, ".cue")) continue;
        if (join(path, dir, nm) || (n = fs->cue_files(fs->ctx, path, buf, cap)) <= 0) continue;
        for (r = buf; n > 0; n--, r += strlen(r) + 1)
            for (j = 0; j < l->n; j++)
                if (j != i && !om_listing_isdir(l, j) && ieq(om_listing_name(l, j), r))
                    l->dir[l->order[j]] |= 2;
    }
}

static int join(char *dst, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la + (la ? 1 : 0) + lb > 255)
        return -1;                     /* spec §2: paths <= 255 bytes; skipped and counted */
    memcpy(dst, a, la);
    if (la) dst[la++] = '/';
    memcpy(dst + la, b, lb + 1);
    return 0;
}

/* DEVIATION (genuine bug, not in the brief's own text as written): a folder's depth equals the number of
 * '/' separators in its path plus one (root's own path is "", depth 0; a root-level child like "Racing" has
 * no '/' and depth 1). Names can never contain '/' (it is the path separator produced only by join() above),
 * so counting it here is equivalent to -- and needs no extra state beyond -- walking the parent chain the way
 * tools/omsat/cardscan.py's queue carries an explicit depth int. Used below to enforce ruling M2-R4 ("depth
 * >= 9 is not entered") before a new folder row is created. */
static uint32_t path_depth(const char *p)
{
    uint32_t d = *p ? 1u : 0u;
    for (; *p; p++)
        if (*p == '/') d++;
    return d;
}

/* 2026-11-23 (Satiator Rings' model, clarification 7): a single-game folder holds exactly one image and no subfolder */
#define is_game_dir(nsub, nimg) (!(nsub) && (nimg) == 1u)

static int add_entry(om_libw *lw, const om_keysrc *ks, const char *path, const om_sysid *s, int ok, uint16_t folder,
                     int box, om_scan_report *rep)
{
    om_lib_entry e;
    om_match_result r;
    size_t n = strlen(path);
    memset(&e, 0, sizeof e);
    e.path = path;
    e.title = "";
    e.folder = folder;
    e.flags = (uint16_t)(box ? OM_LIB_BOX : 0);
    e.image_type = (uint8_t)(((path[n - 3] | 32) == 'c') ? 1 : 2);
    e.disc = e.disc_total = 1;
    if (ok) {
        if (ks->count && om_match(ks, s->product, 10, s->area, &r))
            strcpy(e.key, r.key);
        e.area = s->area_flags;
        e.disc = s->disc;
        e.disc_total = s->disc_total;
    } else
        rep->unreadable++;
    if (e.key[0]) rep->matched++;
    rep->entries++;
    return om_libw_entry(lw, &e);
}

int om_scan(const om_scan_fs *fs, const om_gamesdb *db, om_scan_work *w, uint32_t build_date, uint8_t *out, uint32_t cap,
            uint32_t *out_len, om_scan_report *rep)
{
    om_keysrc ks;
    om_libw lw;
    om_sysid one;
    uint32_t qi, i, k, seen = 0;
    int rc;
    memset(rep, 0, sizeof *rep);
    memset(&ks, 0, sizeof ks);
    if (db) om_keysrc_gamesdb(&ks, db);
    om_libw_init(&lw, w->ent, OM_SCAN_MAXENT, w->order, w->fold, OM_SCAN_MAXFOLD, w->pool, OM_SCAN_POOL);
    om_libw_folder(&lw, "", 0xFFFF);
    if ((rc = fs->list(fs->ctx, "", &w->a)) < 0)
        return rc;
    if (w->a.overflow)
        return OM_E_FULL;              /* 2026-11-23: a folder the listing cannot hold is never scanned partially */
    for (i = 0; i < w->a.n; i++)
        w->names[i] = w->a.buf + w->a.off[i];
    rep->fingerprint = om_fingerprint(w->names, w->a.n, w->scratch);       /* clarification 6 */
    for (qi = 0; qi < lw.nfold; qi++) {
        uint16_t fid = (uint16_t)qi;
        strcpy(w->path, lw.fold[qi].path);
        if (qi && fs->list(fs->ctx, w->path, &w->a) < 0) { rep->list_errors++; continue; }
        if (w->a.overflow) return OM_E_FULL;
        om_listing_sort(&w->a);
        mark_cue_refs(fs, &w->a, w->path, qi == 0, w->img, (char *)w->sid, (uint32_t)sizeof w->sid);
        for (i = 0, k = 0; i < w->a.n; i++)                               /* a loose image's cover falls back to */
            if (!om_listing_isdir(&w->a, i) && !strcmp(om_listing_name(&w->a, i), "BOX.TGA")) k = 1;   /* its folder's */
        for (i = 0; i < w->a.n; i++) {                                    /* loose images in this folder */
            const char *nm = om_listing_name(&w->a, i);
            if (om_listing_isdir(&w->a, i) || om_is_hidden_name(nm, qi == 0) || !is_image(nm) || om_listing_ref(&w->a, i))
                continue;
            if (join(w->img, w->path, nm)) { rep->truncated++; continue; }
            rc = fs->sysid(fs->ctx, w->img, &one);
            if (add_entry(&lw, &ks, w->img, &one, rc == 0, fid, (int)k, rep)) return OM_E_FULL;
        }
        for (i = 0; i < w->a.n; i++) {                                    /* subdirectories */
            const char *d = om_listing_name(&w->a, i);
            uint32_t nsub = 0, nimg = 0;
            int box = 0;
            if (!om_listing_isdir(&w->a, i) || om_is_hidden_name(d, qi == 0)) continue;
            if (join(w->sub, w->path, d)) { rep->truncated++; continue; }
            if (fs->list(fs->ctx, w->sub, &w->b) < 0) { rep->list_errors++; continue; }
            if (w->b.overflow) rep->truncated++;
            om_listing_sort(&w->b);
            mark_cue_refs(fs, &w->b, w->sub, 0, w->img, (char *)w->sid, (uint32_t)sizeof w->sid);
            for (k = 0; k < w->b.n; k++) {
                const char *f = om_listing_name(&w->b, k);
                if (om_is_hidden_name(f, 0)) continue;
                if (om_listing_isdir(&w->b, k)) nsub++;
                else if (is_image(f) && !om_listing_ref(&w->b, k)) nimg++;
                else if (!strcmp(f, "BOX.TGA")) box = 1;
            }
            if (is_game_dir(nsub, nimg)) {
                uint32_t m = 0;
                for (k = 0; k < w->b.n; k++) {
                    const char *f = om_listing_name(&w->b, k);
                    if (om_listing_isdir(&w->b, k) || om_is_hidden_name(f, 0) || !is_image(f) || om_listing_ref(&w->b, k))
                        continue;
                    w->sid_ok[m] = !join(w->img, w->sub, f) && fs->sysid(fs->ctx, w->img, &w->sid[m]) == 0;
                    m++;
                }
            }
            if (is_game_dir(nsub, nimg)) {
                uint32_t m = 0;
                for (k = 0; k < w->b.n; k++) {
                    const char *f = om_listing_name(&w->b, k);
                    if (om_listing_isdir(&w->b, k) || om_is_hidden_name(f, 0) || !is_image(f) || om_listing_ref(&w->b, k))
                        continue;
                    if (join(w->img, w->sub, f)) { rep->truncated++; m++; continue; }
                    if (add_entry(&lw, &ks, w->img, &w->sid[m], w->sid_ok[m], fid, box, rep)) return OM_E_FULL;
                    m++;
                }
            } else if (path_depth(w->sub) > OM_SCAN_MAXDEPTH || lw.nfold >= OM_SCAN_MAXFOLD) {
                rep->skipped_folders++;        /* overflow folder and its contents, not entered */
            } else if (om_libw_folder(&lw, w->sub, fid) < 0)
                return OM_E_FULL;
            if (fs->progress) fs->progress(fs->ctx, ++seen, rep->entries);   /* each subfolder: SD reads, no frame */
        }
        if (fs->progress) fs->progress(fs->ctx, ++seen, rep->entries);
    }
    rep->folders = lw.nfold;
    return om_libw_finish(&lw, 1, rep->fingerprint, build_date, out, cap, out_len);
}
