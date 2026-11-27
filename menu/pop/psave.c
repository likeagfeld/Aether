/* menu/pop/psave.c -- see psave.h. BSD-3-Clause. */
#include <string.h>
#include "psave.h"

static const char *const k_act[OM_PSA_N] = { "Copy to the card", "Copy to the console", "Copy to the cartridge",
                                             "Back up now", "Delete\xE2\x80\xA6" };

/* a list cursor: a fresh press wraps, a held repeat stops at the ends; top keeps the row in the vis rows shown */
static int step(uint16_t *row, uint16_t *top, uint16_t n, uint16_t vis, int dir, int rep)
{
    uint16_t r = *row;
    if (!n) return 0;
    if (dir < 0) r = r ? (uint16_t)(r - 1u) : rep ? r : (uint16_t)(n - 1u);
    else r = r + 1u < n ? (uint16_t)(r + 1u) : rep ? r : 0;
    if (r == *row) return 0;
    *row = r;
    if (vis) {
        if (*row < *top) *top = *row;
        if (*row >= *top + vis) *top = (uint16_t)(*row - vis + 1u);
    }
    return 1;
}

static void clamp(om_ps *p)
{
    if (p->row >= p->nrow) p->row = p->nrow ? (uint16_t)(p->nrow - 1u) : 0;
    if (p->top > p->row) p->top = p->row;
    if (p->vis && p->row >= p->top + p->vis) p->top = (uint16_t)(p->row - p->vis + 1u);
    if (p->nrow <= p->vis) p->top = 0;
}

static void build_sets(om_ps *p)
{
    om_sv *v = p->v;
    uint32_t i, k;
    p->nsets = 0;
    for (i = 0; i < v->nset && p->nsets < 2u * OM_SV_SETS; i++) {    /* every main set, even an empty one */
        om_ps_set *s = &p->sets[p->nsets++];
        memset(s, 0, sizeof *s);
        s->set = (uint8_t)i;
        s->loaded = (uint8_t)(om_sv_set_loaded(v, i) != 0);
    }
    for (i = 0; i < v->n; i++) {
        const om_sv_item *x = &v->it[i];
        om_ps_set *s = 0;
        for (k = 0; k < p->nsets; k++)
            if (p->sets[k].set == x->set && p->sets[k].alt == x->alt) { s = &p->sets[k]; break; }
        if (!s) {
            if (p->nsets >= 2u * OM_SV_SETS) continue;
            s = &p->sets[p->nsets++];
            memset(s, 0, sizeof *s);
            s->set = x->set;
            s->alt = x->alt;
        }
        s->n++;
        if (x->d.date > s->newest) s->newest = x->d.date;
    }
    for (i = 1; i < p->nsets; i++) {                              /* newest first; equal: the set order */
        om_ps_set t = p->sets[i];
        for (k = i; k > 0; k--) {
            const om_ps_set *a = &p->sets[k - 1];
            int before = t.newest > a->newest ||
                         (t.newest == a->newest && (t.set < a->set || (t.set == a->set && t.alt < a->alt)));
            if (!before) break;
            p->sets[k] = p->sets[k - 1];
        }
        p->sets[k] = t;
    }
}

static void relist(om_ps *p)
{
    om_sv *v = p->v;
    uint32_t i;
    uint8_t cs = 0, ca = 0;
    if (p->sdlev && p->cur_set < p->nsets) { cs = p->sets[p->cur_set].set; ca = p->sets[p->cur_set].alt; }
    p->nrow = 0;
    p->list_rc = om_sv_list(v, p->tab);
    if (p->list_rc) v->n = 0;
    if (p->tab != OM_SV_SD) {
        for (i = 0; i < v->n && i < OM_PS_ROWS; i++) p->map[p->nrow++] = (uint16_t)i;
    } else {
        build_sets(p);
        if (!p->sdlev) {
            for (i = 0; i < p->nsets; i++) p->map[p->nrow++] = (uint16_t)i;
        } else {
            p->cur_set = 0xFFFFu;
            for (i = 0; i < p->nsets; i++)
                if (p->sets[i].set == cs && p->sets[i].alt == ca) p->cur_set = (uint16_t)i;
            if (p->cur_set == 0xFFFFu) { p->sdlev = 0; p->cur_set = 0; relist(p); return; }   /* the set is gone */
            for (i = 0; i < v->n && p->nrow < OM_PS_ROWS; i++)
                if (v->it[i].set == cs && v->it[i].alt == ca) p->map[p->nrow++] = (uint16_t)i;
        }
    }
    clamp(p);
}

static void say(om_ps *p, uint8_t k, const char *a, const char *b)
{
    p->msgk = k;
    p->msg[0] = 0;
    strncat(p->msg, a, OM_PS_MSG - 1);
    if (b) strncat(p->msg, b, OM_PS_MSG - 1 - strlen(p->msg));
}

void om_ps_open(om_ps *p, om_sv *v, int global, char (*fold)[OM_PGS_DIR], uint32_t fcap, uint16_t vis)
{
    memset(p, 0, sizeof *p);
    p->v = v;
    p->vis = vis;
    p->global = (uint8_t)(global != 0);
    p->fold = fold;
    p->fcap = fcap;
    p->tab = OM_SV_CON;
    if (p->global) {
        p->page = OM_PS_GAMES;
        if (om_sv_folders(v, fold, fcap, &p->nfold)) p->nfold = 0;
        if (p->nfold > fcap) p->nfold = fcap;
        return;
    }
    relist(p);
}

void om_ps_row(const om_ps *p, uint16_t r, const om_sv_item **it, const om_ps_set **st)
{
    *it = 0;
    *st = 0;
    if (r >= p->nrow) return;
    if (p->tab == OM_SV_SD && !p->sdlev) *st = &p->sets[p->map[r]];
    else *it = &p->v->it[p->map[r]];
}

const char *om_ps_folder(const om_ps *p, uint16_t i) { return i < p->nfold ? p->fold[i] : 0; }

const char *om_ps_act_label(const om_ps *p, uint16_t i)
{
    if (i >= p->nact) return "";
    if (i == p->act && p->armed) return p->acts[i] == OM_PSA_DELETE ? "Press A again to delete" : "A again replaces it";
    return k_act[p->acts[i]];
}

static void open_acts(om_ps *p, const om_sv_item *x)
{
    p->sel = *x;
    p->nact = 0;
    if (x->dev != OM_SV_SD) p->acts[p->nact++] = OM_PSA_TO_CARD;
    if (x->dev != OM_SV_CON) p->acts[p->nact++] = OM_PSA_TO_CON;
    if (x->dev != OM_SV_CART) p->acts[p->nact++] = OM_PSA_TO_CART;
    if (x->dev == OM_SV_CON) p->acts[p->nact++] = OM_PSA_BACKUP_ALL;
    p->acts[p->nact++] = OM_PSA_DELETE;
    p->act = 0;
    p->armed = 0;
    p->page = OM_PS_ACT;
}

int om_ps_pending(const om_ps *p) { return p->pending; }

static const char *dev_name(int d) { return d == OM_SV_CON ? "the console" : d == OM_SV_CART ? "the cartridge" : "the card"; }

void om_ps_run(om_ps *p)
{
    om_sv *v = p->v;
    uint32_t seq = 0;
    int rc, same = 0, op;
    if (!p->pending) return;
    p->pending = 0;
    op = p->acts[p->act];
    switch (op) {
    case OM_PSA_TO_CARD:
        rc = om_sv_backup(v, &p->sel, &seq);
        if (!rc) say(p, OM_PSM_OK, "Copied to the card (SAVES/_MANAGER)", 0);
        break;
    case OM_PSA_TO_CON:
    case OM_PSA_TO_CART:
        rc = om_sv_copy(v, &p->sel, op == OM_PSA_TO_CON ? OM_SV_CON : OM_SV_CART, p->armed);
        if (rc == OM_SV_ASK) {                                    /* that name is there: ask, then back up first */
            p->armed = 1;
            say(p, OM_PSM_ASK, op == OM_PSA_TO_CON ? "The console has it: A again replaces it" :
                                                     "The cartridge has it: A again replaces it", 0);
            return;
        }
        if (!rc) say(p, OM_PSM_OK, "Copied to ", op == OM_PSA_TO_CON ? dev_name(OM_SV_CON) : dev_name(OM_SV_CART));
        break;
    case OM_PSA_BACKUP_ALL:
        rc = om_sv_backup_all(v, &seq, &same);
        if (!rc) say(p, OM_PSM_OK, same ? "Already backed up: nothing changed" : "Console memory backed up", 0);
        break;
    default:
        rc = om_sv_delete(v, &p->sel);
        if (!rc) say(p, OM_PSM_OK, p->sel.dev == OM_SV_SD ? "Deleted (the older set keeps a copy)"
                                                          : "Deleted (a copy is on the card)", 0);
        break;
    }
    if (rc) say(p, OM_PSM_ERR, om_sv_why(rc), 0);
    p->armed = 0;
    if (!rc || op != OM_PSA_BACKUP_ALL) p->page = OM_PS_LIST;
    relist(p);
}

static uint32_t key_list(om_ps *p, uint16_t k)
{
    int rep = (k & OM_PS_K_REP) != 0;
    if (k & (OM_PS_K_U | OM_PS_K_D))
        return step(&p->row, &p->top, p->nrow, p->vis, (k & OM_PS_K_U) ? -1 : 1, rep) ? OM_PS_MOVED : 0;
    if (k & (OM_PS_K_L | OM_PS_K_R)) {
        p->tab = (uint8_t)((p->tab + ((k & OM_PS_K_L) ? OM_SV_NDEV - 1u : 1u)) % OM_SV_NDEV);
        p->sdlev = 0;
        p->row = p->top = 0;
        p->msgk = OM_PSM_NONE;
        relist(p);
        return OM_PS_MOVED;
    }
    if (k & OM_PS_K_A) {
        const om_sv_item *it;
        const om_ps_set *st;
        om_ps_row(p, p->row, &it, &st);
        p->msgk = OM_PSM_NONE;
        if (st) {
            p->cur_set = p->map[p->row];
            p->sdlev = 1;
            p->row = p->top = 0;
            relist(p);
            return OM_PS_OK;
        }
        if (it) { open_acts(p, it); return OM_PS_OK; }
        return 0;
    }
    if (k & OM_PS_K_B) {
        p->msgk = OM_PSM_NONE;
        if (p->tab == OM_SV_SD && p->sdlev) {
            uint16_t s = p->cur_set, i;
            p->sdlev = 0;
            relist(p);
            for (i = 0; i < p->nrow; i++) if (p->map[i] == s) p->row = i;
            clamp(p);
            return OM_PS_BACK;
        }
        if (p->global) { p->page = OM_PS_GAMES; return OM_PS_BACK; }
        return OM_PS_CLOSE;
    }
    return 0;
}

static uint32_t key_act(om_ps *p, uint16_t k)
{
    if (k & (OM_PS_K_U | OM_PS_K_D)) {
        p->armed = 0;
        if (p->msgk == OM_PSM_ASK) p->msgk = OM_PSM_NONE;
        return step(&p->act, 0, p->nact, 0, (k & OM_PS_K_U) ? -1 : 1, (k & OM_PS_K_REP) != 0) ? OM_PS_MOVED : 0;
    }
    if (k & OM_PS_K_A) {
        if (p->acts[p->act] == OM_PSA_DELETE && !p->armed) {     /* asks twice */
            p->armed = 1;
            say(p, OM_PSM_ASK, "Delete asks twice and backs up first", 0);
            return OM_PS_OK;
        }
        p->pending = 1;
        return OM_PS_OK;
    }
    if (k & OM_PS_K_B) {
        if (p->armed) { p->armed = 0; p->msgk = OM_PSM_NONE; }
        else p->page = OM_PS_LIST;
        return OM_PS_BACK;
    }
    return 0;
}

static uint32_t key_games(om_ps *p, uint16_t k)
{
    if (k & (OM_PS_K_U | OM_PS_K_D))
        return step(&p->grow, &p->gtop, (uint16_t)(p->nfold + 1u), p->vis, (k & OM_PS_K_U) ? -1 : 1,
                    (k & OM_PS_K_REP) != 0) ? OM_PS_MOVED : 0;
    if ((k & OM_PS_K_A) && !p->grow) return OM_PS_IMPORT | OM_PS_OK;   /* row 0: the import (psave.h) */
    if ((k & OM_PS_K_A) && p->grow <= p->nfold) {
        p->msgk = OM_PSM_NONE;
        if (om_sv_set_folder(p->v, p->fold[p->grow - 1u])) { say(p, OM_PSM_ERR, om_sv_why(-1), 0); return OM_PS_OK; }
        p->page = OM_PS_LIST;
        p->tab = OM_SV_SD;
        p->sdlev = 0;
        p->row = p->top = 0;
        relist(p);
        return OM_PS_OK;
    }
    if (k & OM_PS_K_B) return OM_PS_CLOSE;
    return 0;
}

uint32_t om_ps_key(om_ps *p, uint16_t k)
{
    if (p->pending) return 0;                                    /* the view runs it first */
    switch (p->page) {
    case OM_PS_ACT:   return key_act(p, k);
    case OM_PS_GAMES: return key_games(p, k);
    default:          return key_list(p, k);
    }
}
