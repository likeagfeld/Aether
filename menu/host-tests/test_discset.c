/* menu/host-tests/test_discset.c -- multi-disc sets at launch (src/discset.h): the lead disc, the hand-off's discs */
#include "th.h"
#include "discset.h"
#include "settings.h"
#include <stddef.h>

static om_lib_entry ent[16];
static uint16_t order[16];
static om_lib_folder fold[4];
static char pool[4096];
static uint8_t buf[8192];

static void add(om_libw *w, const char *path, uint8_t disc, uint8_t total, uint16_t set, int alt)
{
    om_lib_entry e;
    memset(&e, 0, sizeof e);
    e.path = path; e.title = "";
    e.disc = disc; e.disc_total = total; e.image_type = 1;
    e.flags = (uint16_t)(set << 4 | (alt ? OM_LIB_ALT : 0));
    CHECK_INT(om_libw_entry(w, &e), OM_OK);
}

static void fresh(om_handoff *h, const char *dir, const char *image)
{
    om_launch_opts o;
    memset(&o, 0, sizeof o);
    o.loader = OM_LDR_JHL;
    CHECK_INT(om_handoff_fill2(h, dir, image, 1, 0, &o), OM_OK);
}

int main(void)
{
    om_libw w;
    om_libidx l;
    om_handoff h;
    uint32_t len, i;
    char longp[200];
    om_libw_init(&w, ent, 16, order, fold, 4, pool, sizeof pool);
    CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
    add(&w, "PDS (USA) (Disc 1)/PDS (USA) (Disc 1).cue", 1, 4, 1, 0);           /* 0 */
    add(&w, "PDS (USA) (Disc 1)/patched/PDS (USA) (Disc 1).cue", 1, 4, 1, 1);   /* 1: an alternate */
    add(&w, "PDS (USA) (Disc 2)/PDS (USA) (Disc 2).cue", 2, 4, 1, 0);           /* 2 */
    add(&w, "PDS (USA) (Disc 4)/PDS (USA) (Disc 4).cue", 4, 4, 1, 0);           /* 3: disc 3 missing */
    add(&w, "Lone/lone.iso", 1, 1, 0, 0);                                     /* 4: no set */
    add(&w, "Sets/X (Disc 1)/x.cue", 1, 2, 2, 0);                             /* 5 */
    add(&w, "Sets/X (Disc 2)/x.cue", 2, 2, 2, 0);                             /* 6 */
    add(&w, "Half (Disc 2)/h.cue", 2, 2, 3, 0);                               /* 7: a set of one disc */
    add(&w, "Bad (Disc 1)/b.cue", 1, 2, 4, 0);                                /* 8 */
    add(&w, "Bad (Disc 2)/b.cue", 2, 2, 4, 0);                                /* 9 */
    add(&w, "Bad (Disc 0)/b.cue", 0, 2, 4, 0);                                /* 10: set bits, a disc byte of 0 */
    memset(longp, 'L', sizeof longp - 1); longp[sizeof longp - 1] = 0;
    memcpy(longp, "Long/", 5);
    CHECK_INT(om_libw_finish(&w, 0, 0, 20261124u, buf, sizeof buf, &len), OM_OK);
    CHECK_INT(om_libidx_open(&l, buf, len), OM_OK);
    {   /* the writer keeps bits 2-15 (a PC-built index read and written back on the console) */
        om_lib_entry e;
        om_libidx_entry(&l, 1, &e);
        CHECK_INT(e.flags, 1 << 4 | OM_LIB_ALT);
    }
    /* the lead: the lowest disc number; the launched image itself when it has that number */
    CHECK_INT(om_discset_lead(&l, 0), 0);
    CHECK_INT(om_discset_lead(&l, 1), 1);           /* the patched disc 1 leads its own launch */
    CHECK_INT(om_discset_lead(&l, 2), 0);           /* disc 2's lead is disc 1's primary */
    CHECK_INT(om_discset_lead(&l, 3), 0);
    CHECK_INT(om_discset_lead(&l, 4), 4);           /* no set */
    CHECK_INT(om_discset_lead(&l, 7), 7);           /* a set of one disc is no set */
    CHECK_INT(om_discset_lead(&l, 6), 5);
    CHECK_INT(om_discset_lead(&l, 99), 99);
    /* disc 2 of the root set: the root is the parent, disc 3 is skipped, disc 2 is the second of three */
    fresh(&h, "/PDS (USA) (Disc 2)", "PDS (USA) (Disc 2).cue");
    CHECK_INT(om_discset_fill(&h, &l, 2), 3);
    CHECK_INT(om_handoff_check(&h), OM_OK);
    CHECK_STR(h.dir, "/");
    CHECK_STR(h.image, "PDS (USA) (Disc 2)/PDS (USA) (Disc 2).cue");
    CHECK_INT(h.ndisc, 3); CHECK_INT(h.cur, 2);
    CHECK_STR(om_handoff_disc(&h, 1), "PDS (USA) (Disc 1)/PDS (USA) (Disc 1).cue");   /* the primary, not patched */
    CHECK_STR(om_handoff_disc(&h, 2), "PDS (USA) (Disc 2)/PDS (USA) (Disc 2).cue");
    CHECK_STR(om_handoff_disc(&h, 3), "PDS (USA) (Disc 4)/PDS (USA) (Disc 4).cue");
    /* the alternate launched: it stands for disc 1 */
    fresh(&h, "/x", "y.cue");
    CHECK_INT(om_discset_fill(&h, &l, 1), 3);
    CHECK_INT(om_handoff_check(&h), OM_OK);
    CHECK_INT(h.cur, 1);
    CHECK_STR(om_handoff_disc(&h, 1), "PDS (USA) (Disc 1)/patched/PDS (USA) (Disc 1).cue");
    CHECK_STR(h.image, "PDS (USA) (Disc 1)/patched/PDS (USA) (Disc 1).cue");
    /* a set in a parent folder: that folder is the parent */
    fresh(&h, "/Sets/X (Disc 1)", "x.cue");
    CHECK_INT(om_discset_fill(&h, &l, 5), 2);
    CHECK_STR(h.dir, "/Sets");
    CHECK_STR(h.image, "X (Disc 1)/x.cue");
    CHECK_INT(h.cur, 1);
    CHECK_STR(om_handoff_disc(&h, 2), "X (Disc 2)/x.cue");
    CHECK_INT(om_handoff_check(&h), OM_OK);
    /* no set, or a set of one: the block is left alone (one disc, out.desc) */
    fresh(&h, "/Lone", "lone.iso");
    CHECK_INT(om_discset_fill(&h, &l, 4), 0);
    CHECK_INT(om_discset_fill(&h, &l, 7), 0);
    CHECK_INT(h.ndisc, 0); CHECK_STR(h.dir, "/Lone"); CHECK_INT(om_handoff_check(&h), OM_OK);
    /* an image with set bits but no usable disc number launches alone, the block untouched */
    fresh(&h, "/Bad (Disc 0)", "b.cue");
    CHECK_INT(om_discset_fill(&h, &l, 10), 0);
    CHECK_INT(h.ndisc, 0); CHECK_STR(h.dir, "/Bad (Disc 0)"); CHECK_INT(om_handoff_check(&h), OM_OK);
    /* paths that do not fit the pool: one disc */
    om_libw_init(&w, ent, 16, order, fold, 4, pool, sizeof pool);
    CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
    for (i = 0; i < 9; i++) {
        static char p[9][200];
        memcpy(p[i], longp, sizeof longp);
        p[i][6] = (char)('1' + i);
        add(&w, p[i], (uint8_t)(i + 1), 9, 1, 0);
    }
    CHECK_INT(om_libw_finish(&w, 0, 0, 20261124u, buf, sizeof buf, &len), OM_OK);
    CHECK_INT(om_libidx_open(&l, buf, len), OM_OK);
    fresh(&h, "/Long", "x");
    CHECK_INT(om_discset_fill(&h, &l, 0), 0);        /* 9 x 195 B > the 1,400 B pool */
    CHECK_INT(h.ndisc, 0); CHECK_STR(h.dir, "/Long"); CHECK_INT(om_handoff_check(&h), OM_OK);
    /* hidden root names: the launch descriptors never trip the card fingerprint */
    CHECK_INT(om_is_hidden_name("out.desc", 1), 1);
    CHECK_INT(om_is_hidden_name("OUT3.DESC", 1), 1);
    CHECK_INT(om_is_hidden_name("out9.desc", 1), 1);
    CHECK_INT(om_is_hidden_name("out0.desc", 1), 0);
    CHECK_INT(om_is_hidden_name("out10.desc", 1), 0);
    CHECK_INT(om_is_hidden_name("out1.desc", 0), 0);
    CHECK_INT(om_is_hidden_name("outlaws.desc", 1), 0);
    return th_done("test_discset");
}
