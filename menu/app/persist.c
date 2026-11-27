/* menu/app/persist.c -- see persist.h. Each SETTINGS.INI save reads the file again and merges against it; both 8 KB
 * buffers are the caller's (the menu lends two slots of OM_LW_STAGE), so no copy of the file stays in HWRAM.
 * BSD-3-Clause. */
#include <string.h>
#include "om_fmt.h"
#include "persist.h"
#include "sd.h"

static uint8_t s_ini_clean;      /* the file read at load was unreadable or malformed: the next save starts clean */

static int load_text(const char *path, void *buf, uint32_t cap, uint32_t *len)
{
    int rc = om_sd_read_all(path, buf, cap, len);
    if (rc == OM_SD_NOFILE) { *len = 0; return OM_OK; }
    return rc;
}

static int load_list(om_plist *pl, const om_pathidx *px, const char *path, uint8_t *stage, uint32_t cap)
{
    uint32_t len;
    int rc = load_text(path, stage, cap, &len);
    return rc ? rc : om_plist_parse(pl, px, (const char *)stage, len);
}

void om_persist_load(om_persist *p, const om_libidx *l, uint16_t *order, uint16_t *tmp, uint8_t *stage, uint32_t stage_cap)
{
    om_pathidx px;
    om_settings_default(&p->set);
    om_plist_init(&p->favs, OM_FAVS_MAX);
    om_plist_init(&p->recent, OM_RECENT_MAX);
    uint32_t len;
    p->set_rc = load_text(OM_PATH_SETTINGS, stage, stage_cap < OM_SETTINGS_IN ? stage_cap : OM_SETTINGS_IN, &len);
    if (!p->set_rc)
        p->set_rc = om_settings_parse(&p->set, (const char *)stage, len);
    s_ini_clean = p->set_rc != 0;
    if (p->set_rc)                   /* unreadable or malformed: defaults now, a clean file on the next save */
        om_settings_default(&p->set);
    om_pathidx_build(&px, l, order, tmp);
    p->favs_rc = load_list(&p->favs, &px, OM_PATH_FAVS, stage, stage_cap);
    p->recent_rc = load_list(&p->recent, &px, OM_PATH_RECENT, stage, stage_cap);
}

int om_persist_save_settings(om_persist *p, uint8_t *in, char *out)
{
    uint32_t n, len = 0;
    int rc;
    if (!s_ini_clean && (rc = load_text(OM_PATH_SETTINGS, in, OM_SETTINGS_IN, &len)) != 0)
        return rc;
    if ((rc = om_settings_merge(&p->set, (const char *)in, len, out, OM_SETTINGS_MAX, &n)) != 0)
        return rc;
    if ((rc = om_sd_write_buf(OM_PATH_SETTINGS, out, n)) != 0)
        return rc;
    s_ini_clean = 0;
    return OM_OK;
}

typedef struct { const om_plist *pl; const om_libidx *l; uint32_t next; } emit_src;

static int32_t from_list(void *ctx, uint8_t *dst, uint32_t cap)
{
    emit_src *e = (emit_src *)ctx;
    uint32_t n = om_plist_emit(e->pl, e->l, &e->next, (char *)dst, cap);
    /* 0 with lines left = "no room for the next line" (sdwrite.h's source contract); only with OM_SDW_PIECE_MAX of
     * room is that a line too long for any chunk: impossible for <= 255 B paths, refused anyway */
    if (!n && e->next < e->pl->n && cap >= OM_SDW_PIECE_MAX)
        return OM_E_FULL;
    return (int32_t)n;
}

int om_persist_save_list(const om_plist *pl, const om_libidx *l, const char *path)
{
    emit_src e;
    e.pl = pl;
    e.l = l;
    e.next = 0;
    return om_sd_write_file(path, from_list, &e);
}
