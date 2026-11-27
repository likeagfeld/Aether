/* menu/host-tests/test_pgsplan.c -- the per-game-save planner (pgsplan.h, SPEC §5.17): the five plans,
 * the undo lists, cap and field errors, the marker and journal lines, the archive file names. BSD-3-Clause. */
#include <string.h>
#include "th.h"
#include "pgsplan.h"
#include "bupfile.h"
#include "om_fmt.h"

static om_pgs_state st(const char *m, int mslot, const char *w, int wslot, int pgs)
{
    om_pgs_state s;
    memset(&s, 0, sizeof s);
    if (m) { strcpy(s.marker_gid, m); s.marker_slot = (uint8_t)mslot; s.has_marker = 1; }
    if (w) strcpy(s.want_gid, w);
    s.want_slot = (uint8_t)wslot;
    s.want_pgs = (uint8_t)pgs;
    return s;
}

static const char *n12(const char *s)
{   /* a 12-byte, NUL-padded record name */
    static char b[4][12];
    static int k;
    char *o = b[k++ & 3];
    memset(o, 0, 12);
    memcpy(o, s, strlen(s) < 12 ? strlen(s) : 12);
    return o;
}

typedef struct { int op; const char *dir; } exp_op;

static void check_list(int line, const om_pgs_op *o, uint32_t n, const exp_op *e, uint32_t ne)
{
    uint32_t i;
    if (n != ne) { th_fail++; printf("FAIL line %d: %u ops, want %u\n", line, n, ne); return; }
    for (i = 0; i < n; i++) {
        if (o[i].op != e[i].op || strcmp(o[i].dir, e[i].dir)) {
            th_fail++;
            printf("FAIL line %d op %u: %s \"%s\", want %s \"%s\"\n", line, i, om_pgs_op_name(o[i].op), o[i].dir,
                   om_pgs_op_name((uint32_t)e[i].op), e[i].dir);
        } else
            th_pass++;
    }
}
#define LIST(o, n, ...) do { static const exp_op _e[] = { __VA_ARGS__ }; \
    check_list(__LINE__, o, n, _e, sizeof _e / sizeof _e[0]); } while (0)

int main(void)
{
    om_pgs_op o[OM_PGS_MAX_OPS];
    om_pgs_state s;
    uint32_t n = 99;
    char d[OM_PGS_DIR], line[200], gid[OM_GID_MAX], fn[OM_PGS_FNAME], taken[12][OM_PGS_FNAME];
    uint8_t slot;
    uint32_t nonce, fpv;
    om_bup_dir L[4];
    om_pgs_journal j, k;
    int i;

    /* case 1: BRAM already holds G slot 3 */
    s = st("MK-81045@U", 3, "MK-81045@U", 3, 1);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_INT(n, 0);
    CHECK_INT(om_pgs_undo(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_INT(n, 0);

    /* case 2: M = MK-81045@U slot 3, G = MK-81214@UT slot 0 (the brief's example dir) */
    s = st("MK-81045@U", 3, "MK-81214@UT", 0, 1);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ARCHIVE_BRAM_TO, "SAVES/MK-81045[U]/03" }, { OM_OP_VERIFY_DIR, "SAVES/MK-81045[U]/03" },
         { OM_OP_CHECK_FITS, "SAVES/MK-81214[UT]" }, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" },
         { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81214[UT]" }, { OM_OP_WRITE_MARKER, "SAVES/MK-81214[UT]" });
    CHECK_INT(om_pgs_undo(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81045[U]/03" },
         { OM_OP_WRITE_MARKER, "SAVES/MK-81045[U]/03" });
    /* the same game, another slot, is case 2 too */
    s = st("MK-81045@U", 3, "MK-81045@U", 4, 1);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ARCHIVE_BRAM_TO, "SAVES/MK-81045[U]/03" }, { OM_OP_VERIFY_DIR, "SAVES/MK-81045[U]/03" },
         { OM_OP_CHECK_FITS, "SAVES/MK-81045[U]/04" }, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" },
         { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81045[U]/04" }, { OM_OP_WRITE_MARKER, "SAVES/MK-81045[U]/04" });
    /* case 2, Share, G's slot dir new: the general set first */
    s = st("MK-81045@U", 3, "MK-81214@UT", 0, 1);
    s.share = 1;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ARCHIVE_BRAM_TO, "SAVES/MK-81045[U]/03" }, { OM_OP_VERIFY_DIR, "SAVES/MK-81045[U]/03" },
         { OM_OP_CHECK_FITS, "SAVES/MK-81214[UT]" }, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" },
         { OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL }, { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81214[UT]" },
         { OM_OP_WRITE_MARKER, "SAVES/MK-81214[UT]" });
    s.slot_exists = 1;                                     /* not its first use: no copy */
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_INT(n, 6);

    /* case 3: no marker, Move */
    s = st(NULL, 0, "T-1234567@JTUEBKAL", 9, 1);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ROTATE_GENERAL, "" }, { OM_OP_ARCHIVE_BRAM_TO, OM_PGS_GENERAL },
         { OM_OP_VERIFY_DIR, OM_PGS_GENERAL }, { OM_OP_CHECK_FITS, "SAVES/T-1234567[JTUEBKAL]/09" },
         { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/T-1234567[JTUEBKAL]/09" },
         { OM_OP_WRITE_MARKER, "SAVES/T-1234567[JTUEBKAL]/09" });
    CHECK_INT(om_pgs_undo(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL },
         { OM_OP_DELETE_MARKER, "" });
    /* case 3, Share, new slot dir: _GENERAL loaded first */
    s = st(NULL, 0, "MK-81214@", 0, 1);
    s.share = 1;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ROTATE_GENERAL, "" }, { OM_OP_ARCHIVE_BRAM_TO, OM_PGS_GENERAL },
         { OM_OP_VERIFY_DIR, OM_PGS_GENERAL }, { OM_OP_CHECK_FITS, "SAVES/MK-81214" },
         { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL },
         { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81214" }, { OM_OP_WRITE_MARKER, "SAVES/MK-81214" });

    /* case 4: marker M, want no pgs (a save tool is the same: the caller passes want_pgs 0) */
    s = st("MK-81045@U", 3, "MK-81214@UT", 0, 0);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ARCHIVE_BRAM_TO, "SAVES/MK-81045[U]/03" }, { OM_OP_VERIFY_DIR, "SAVES/MK-81045[U]/03" },
         { OM_OP_CHECK_FITS, OM_PGS_GENERAL }, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" },
         { OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL }, { OM_OP_DELETE_MARKER, "" });
    CHECK_INT(om_pgs_undo(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81045[U]/03" },
         { OM_OP_WRITE_MARKER, "SAVES/MK-81045[U]/03" });
    s = st("MK-81045@U", 0, "", 0, 0);                     /* no game id (no IP.BIN product): still case 4 */
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_INT(n, 6);
    CHECK_STR(o[0].dir, "SAVES/MK-81045[U]");

    /* case 5 */
    s = st(NULL, 0, "MK-81214@UT", 0, 0);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_INT(n, 0);

    /* errors: cap too small, slot 10, bad gids */
    s = st("MK-81045@U", 3, "MK-81214@UT", 0, 1);
    n = 99;
    CHECK_INT(om_pgs_plan(&s, o, 5, &n), OM_E_FULL);
    CHECK_INT(om_pgs_undo(&s, o, 2, &n), OM_E_FULL);
    s.want_slot = 10;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_E_FIELD);
    s.want_slot = 0; s.marker_slot = 10;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_E_FIELD);
    s = st("MK-81045@U", 3, "bad id", 0, 1);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_E_FIELD);
    s = st("MK 81045", 3, "MK-81214@UT", 0, 1);
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_E_FIELD);

    /* slot dirs and op names */
    CHECK_INT(om_pgs_slot_dir(d, "MK-81045@U", 3), OM_OK);
    CHECK_STR(d, "SAVES/MK-81045[U]/03");
    CHECK_INT(om_pgs_slot_dir(d, "MK-81045@", 0), OM_OK);
    CHECK_STR(d, "SAVES/MK-81045");
    CHECK_INT(om_pgs_slot_dir(d, "MK-81045@U", 10), OM_E_FIELD);
    /* Kai's product-only folder (imported from Kai or Rings), used when the region folder is absent */
    CHECK_INT(om_pgs_slot_dir_po(d, "MK-81045@U", 3, 1), OM_OK);
    CHECK_STR(d, "SAVES/MK-81045/03");
    CHECK_INT(om_pgs_slot_dir_po(d, "MK-81045@U", 0, 1), OM_OK);
    CHECK_STR(d, "SAVES/MK-81045");
    CHECK_INT(om_pgs_slot_dir_po(d, "MK-81045@U", 0, 0), OM_OK);
    CHECK_STR(d, "SAVES/MK-81045[U]");
    CHECK_INT(om_pgs_product_dir(d, "MK-81045@U"), OM_OK);
    CHECK_STR(d, "SAVES/MK-81045");
    CHECK_INT(om_pgs_product_dir(d, "MK-81045@"), OM_E_FIELD);            /* no areas: nothing to prefer */
    s = st("MK-81214@UT", 0, "MK-81045@U", 3, 1);
    s.want_po = 1;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_STR(o[n - 1].dir, "SAVES/MK-81045/03");                        /* WRITE_MARKER names the product folder */
    CHECK_STR(o[0].dir, "SAVES/MK-81214[UT]");                           /* the marker's own folder is unchanged */
    s.marker_po = 1;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_STR(o[0].dir, "SAVES/MK-81214");
    CHECK_INT(om_pgs_undo(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_STR(o[1].dir, "SAVES/MK-81214");
    /* a slot's FIRST load (its folder exists, VibeMenu never loaded it)
     * is followed by LOAD_NEWER _GENERAL -- per record, a newer intact copy in the general set wins over the slot's */
    s = st(NULL, 0, "MK-81045@U", 0, 1);
    s.slot_exists = 1;
    s.first_load = 1;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_ROTATE_GENERAL, "" }, { OM_OP_ARCHIVE_BRAM_TO, "SAVES/_GENERAL" },
         { OM_OP_VERIFY_DIR, "SAVES/_GENERAL" }, { OM_OP_CHECK_FITS, "SAVES/MK-81045[U]" },
         { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/MK-81045[U]" },
         { OM_OP_LOAD_NEWER, "SAVES/_GENERAL" }, { OM_OP_WRITE_MARKER, "SAVES/MK-81045[U]" });
    s = st("MK-81214@UT", 0, "MK-81045@U", 3, 1);                       /* C1a: slot 3, its own line */
    s.slot_exists = 1; s.first_load = 1;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK(o[n - 2].op != OM_OP_LOAD_NEWER);
    s.want_slot = 0;                                                      /* case 2 at slot 0: it applies */
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK_INT(o[n - 2].op, OM_OP_LOAD_NEWER);
    s.want_slot = 3;
    s.slot_exists = 0;                                                    /* a new slot: nothing to compare */
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK(o[n - 2].op != OM_OP_LOAD_NEWER);
    s.slot_exists = 1; s.first_load = 0;                                  /* loaded before: the slot's own saves */
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    CHECK(o[n - 2].op != OM_OP_LOAD_NEWER);
    CHECK_STR(om_pgs_op_name(OM_OP_LOAD_NEWER), "LOAD_NEWER");
    CHECK_STR(om_pgs_op_name(OM_OP_ARCHIVE_BRAM_TO), "ARCHIVE");
    CHECK_STR(om_pgs_op_name(OM_OP_CHECK_FITS), "CHECK_FITS");
    CHECK_STR(om_pgs_op_name(OM_OP_COUNT), "?");
    for (i = 0; i < OM_OP_COUNT; i++)
        CHECK(strcmp(om_pgs_op_name((uint32_t)i), "?") != 0);
    CHECK_INT(strlen(OM_PGS_MARKER), 11);                 /* ST-162-R1 pp.46-47, ST-203 p.4 */

    /* the marker line (R3: a session nonce) and ACTIVE.TXT's (plus the fingerprint) */
    CHECK_INT(om_pgs_marker_fmt(line, sizeof line, "MK-81214@UT", 0, 0x0123ABCDu), 23);
    CHECK_STR(line, "MK-81214@UT 0 0123ABCD\n");
    CHECK_INT(om_pgs_marker_fmt(line, 5, "MK-81214@UT", 0, 1), OM_E_FULL);
    CHECK_INT(om_pgs_marker_fmt(line, sizeof line, "x", 0, 1), OM_E_FIELD);
    CHECK_INT(om_pgs_active_fmt(line, sizeof line, "MK-81045@U", 7, 0xDEADBEEFu, 0x00C0FFEEu), 31);
    CHECK_STR(line, "MK-81045@U 7 DEADBEEF 00C0FFEE\n");
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)line, 31, gid, &slot, &nonce, &fpv), OM_OK);
    CHECK_STR(gid, "MK-81045@U"); CHECK_INT(slot, 7); CHECK(nonce == 0xDEADBEEFu && fpv == 0x00C0FFEEu);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)line, 31, gid, &slot, &nonce, NULL), OM_E_FIELD);  /* 4 fields */
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"MK-81045@U 7 0000002A\n", 22, gid, &slot, &nonce, NULL), OM_OK);
    CHECK_INT(nonce, 42);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"MK-81045@U 7 0000002A", 21, gid, &slot, &nonce, NULL), OM_OK);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"MK-81045@U 7 0000002A\n", 22, gid, &slot, &nonce, &fpv),
              OM_E_FIELD);                                                        /* ACTIVE.TXT needs its fp */
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"MK-81045@U 7\n", 13, gid, &slot, &nonce, NULL), OM_E_FIELD);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"MK-81045@U 12 0000002A\n", 23, gid, &slot, &nonce, NULL),
              OM_E_FIELD);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"MK-81045@U 7 0000002g\n", 22, gid, &slot, &nonce, NULL),
              OM_E_FIELD);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"junk 1 00000000\n", 16, gid, &slot, &nonce, NULL), OM_E_FIELD);
    CHECK_INT(om_pgs_marker_parse((const uint8_t *)"", 0, gid, &slot, &nonce, NULL), OM_E_FIELD);

    /* the journal line round-trips every input */
    memset(&j, 0, sizeof j);
    j.s = st("MK-81045@U", 3, "T-1234567@JTUEBKAL", 9, 1);
    j.s.share = 1; j.s.slot_exists = 1; j.op = 4; j.ph = 2; j.undo = 1; j.fp = 0x89ABCDEFu; j.fn = 17;
    CHECK(om_pgs_journal_fmt(line, sizeof line, &j) > 0);
    CHECK_STR(line, "op=4 ph=2 undo=1 gid=T-1234567@JTUEBKAL slot=9 pgs=1 share=1 had=1 marker=1 mgid=MK-81045@U "
                    "mslot=3 foreign=0 q=0 fp=89ABCDEF fn=17\n");
    memset(&k, 0xEE, sizeof k);
    CHECK_INT(om_pgs_journal_parse(line, (uint32_t)strlen(line), &k), OM_OK);
    CHECK_STR(k.s.marker_gid, "MK-81045@U");
    CHECK_STR(k.s.want_gid, "T-1234567@JTUEBKAL");
    CHECK_INT(k.s.marker_slot, 3); CHECK_INT(k.s.has_marker, 1); CHECK_INT(k.s.want_slot, 9);
    CHECK_INT(k.s.want_pgs, 1); CHECK_INT(k.s.share, 1); CHECK_INT(k.s.slot_exists, 1);
    CHECK_INT(k.op, 4); CHECK_INT(k.ph, 2); CHECK_INT(k.undo, 1);
    CHECK(k.fp == 0x89ABCDEFu); CHECK_INT(k.fn, 17); CHECK_INT(k.s.foreign, 0); CHECK_INT(k.s.qnum, 0);
    CHECK_INT(k.g, 0);                                     /* no "g=", generation 0 */
    j.g = 99999;                                           /* the archive's generation, written only when set */
    CHECK(om_pgs_journal_fmt(line, sizeof line, &j) > 0);
    CHECK(strstr(line, " fn=17 g=99999\n") != NULL);
    CHECK_INT(om_pgs_journal_parse(line, (uint32_t)strlen(line), &k), OM_OK);
    CHECK_INT(k.g, 99999); CHECK_INT(k.op, 4);
    strcpy(strstr(line, " g=99999"), " g=100000\n");      /* above the last generation */
    CHECK_INT(om_pgs_journal_parse(line, (uint32_t)strlen(line), &k), OM_E_FIELD);
    memset(&j, 0, sizeof j);                               /* empty gids are "-"; the foreign plan's fields */
    j.s = st(NULL, 0, "", 0, 0);
    j.s.foreign = 1; j.s.qnum = 65535;
    CHECK(om_pgs_journal_fmt(line, sizeof line, &j) > 0);
    CHECK_STR(line, "op=0 ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 mgid=- mslot=0 foreign=1 q=65535 "
                    "fp=00000000 fn=0\n");
    CHECK_INT(om_pgs_journal_parse(line, (uint32_t)strlen(line), &k), OM_OK);
    CHECK_STR(k.s.want_gid, ""); CHECK_STR(k.s.marker_gid, ""); CHECK_INT(k.s.has_marker, 0);
    CHECK_INT(k.s.foreign, 1); CHECK_INT(k.s.qnum, 65535);
    CHECK_INT(om_pgs_journal_fmt(line, 20, &j), OM_E_FULL);
    CHECK_INT(om_pgs_journal_parse("op=0 ph=0", 9, &k), OM_E_FIELD);                         /* missing keys */
#define JL(head, tail) head " mslot=0 foreign=0 q=0 fp=00000000 fn=0" tail
    CHECK_INT(om_pgs_journal_parse(JL("op=x ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 mgid=-", "\n"),
                                   (uint32_t)strlen(JL("op=x ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 "
                                                       "mgid=-", "\n")), &k), OM_E_FIELD);
    {
        const char *bad[] = {
            "op=0 ph=0 undo=0 gid=- slot=12 pgs=0 share=0 had=0 marker=0 mgid=- mslot=0 foreign=0 q=0 fp=00000000 fn=0\n",
            "op=0 ph=0 undo=0 gid=bad slot=0 pgs=1 share=0 had=0 marker=0 mgid=- mslot=0 foreign=0 q=0 fp=00000000 fn=0\n",
            "op=0 ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 mgid=- mslot=0 foreign=1 q=0 fp=00000000 fn=0\n",
            "op=0 ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 mgid=- mslot=0 foreign=0 q=0 fp=0000000 fn=0\n",
            "op=0 ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 mgid=- mslot=0 foreign=0 q=65536 fp=00000000 "
            "fn=0\n",
            "op=0 ph=0 undo=0 gid=- slot=0 pgs=0 share=0 had=0 marker=0 mgid=- mslot=0\n",       /* round-0 line */
        };
        for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++)
            CHECK_INT(om_pgs_journal_parse(bad[i], (uint32_t)strlen(bad[i]), &k), OM_E_FIELD);
    }

    /* the foreign plan (R2/R3): quarantine BRAM, give the general set back, drop the marker */
    s = st("MK-81045@U", 3, "MK-81214@UT", 0, 1);
    s.foreign = 1; s.qnum = 7;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_QUARANTINE, "SAVES/_QUARANTINE/7" }, { OM_OP_CHECK_FITS, OM_PGS_GENERAL },
         { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL },
         { OM_OP_DELETE_MARKER, "" });
    CHECK_INT(om_pgs_undo(&s, o, OM_PGS_MAX_OPS, &n), OM_OK);
    LIST(o, n, { OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "" }, { OM_OP_LOAD_DIR_TO_BRAM, "SAVES/_QUARANTINE/7" },
         { OM_OP_DELETE_MARKER, "" });
    s.qnum = 0;
    CHECK_INT(om_pgs_plan(&s, o, OM_PGS_MAX_OPS, &n), OM_E_FIELD);
    CHECK_INT(om_pgs_q_dir(d, 12), OM_OK);
    CHECK_STR(d, "SAVES/_QUARANTINE/12");
    CHECK_INT(om_pgs_q_dir(d, 0), OM_E_FIELD);
    CHECK_INT(om_pgs_q_dir(d, 65536), OM_E_FIELD);
    CHECK_STR(om_pgs_op_name(OM_OP_QUARANTINE), "QUARANTINE");

    /* R4: reserved records */
    CHECK(om_pgs_reserved(n12("VIBEMENU_PG")));
    CHECK(om_pgs_reserved(n12("PSEUDO__SAT")) && om_pgs_reserved(n12("PSEUDO_XTRA")) && om_pgs_reserved(n12("PSEUDO_BRAM")));
    CHECK(om_pgs_reserved(n12("_CARTDMPHDR")) && om_pgs_reserved(n12("_CARTDMPDAT")));
    CHECK(om_pgs_reserved(n12("SAVRING_001")));
    CHECK(!om_pgs_reserved(n12("GHEROES_001")) && !om_pgs_reserved(n12("PSEUDO")) && !om_pgs_reserved(n12("VIBEMENU")));

    /* R2: the fingerprint ignores order and reserved records, and sees name, date and size */
    memset(L, 0, sizeof L);
    memcpy(L[0].name, "B_REC", 5); L[0].date = 1; L[0].datasize = 10;
    memcpy(L[1].name, "A_REC", 5); L[1].date = 2; L[1].datasize = 20;
    memcpy(L[2].name, "VIBEMENU_PG", 11); L[2].date = 3; L[2].datasize = 23;
    memcpy(L[3].name, "PSEUDO__SAT", 11); L[3].date = 4; L[3].datasize = 40;
    {
        uint32_t c1, c2, f1 = om_pgs_fp(L, 4, sizeof L[0], &c1), f2;
        om_bup_dir t = L[0];
        CHECK_INT(c1, 2);
        L[0] = L[1]; L[1] = t;                             /* another order */
        f2 = om_pgs_fp(L, 4, sizeof L[0], &c2);
        CHECK(f1 == f2 && c2 == 2);
        CHECK(om_pgs_fp(L, 2, sizeof L[0], &c2) == f1);    /* the reserved two change nothing */
        L[0].date++;
        CHECK(om_pgs_fp(L, 4, sizeof L[0], NULL) != f1);
        L[0].date--; L[0].datasize++;
        CHECK(om_pgs_fp(L, 4, sizeof L[0], NULL) != f1);
        CHECK(om_pgs_fp(L, 0, sizeof L[0], &c2) == 0 && c2 == 0);
    }

    /* R1: what may replace an older SD copy */
    CHECK(om_pgs_supersedes(5, 4, 0));
    CHECK(om_pgs_supersedes(5, 5, 1));
    CHECK(!om_pgs_supersedes(5, 5, 0));
    CHECK(!om_pgs_supersedes(4, 5, 1));

    /* archive file names */
    CHECK_INT(om_pgs_file_name(fn, n12("SWAGMAN.DAT"), NULL, 0), OM_OK);
    CHECK_STR(fn, "SWAGMAN_DAT.BUP");
    strcpy(taken[0], "swagman_dat.bup");                  /* case-insensitive collision */
    CHECK_INT(om_pgs_file_name(fn, n12("SWAGMAN_DAT"), (const char (*)[OM_PGS_FNAME])taken, 1), OM_OK);
    CHECK_STR(fn, "SWAGMAN_DAT~1.BUP");
    CHECK_INT(om_pgs_file_name(fn, n12("Gs a*b"), NULL, 0), OM_OK);
    CHECK_STR(fn, "G_____.BUP");
    CHECK_INT(om_pgs_file_name(fn, n12("MK-81214_00"), NULL, 0), OM_OK);
    CHECK_STR(fn, "MK-81214_00.BUP");
    strcpy(taken[0], "A.BUP");
    for (i = 1; i <= 9; i++)
        sprintf(taken[i], "A~%d.BUP", i);
    CHECK_INT(om_pgs_file_name(fn, n12("A"), (const char (*)[OM_PGS_FNAME])taken, 9), OM_OK);
    CHECK_STR(fn, "A~9.BUP");
    CHECK_INT(om_pgs_file_name(fn, n12("A"), (const char (*)[OM_PGS_FNAME])taken, 10), OM_E_FULL);
    CHECK_INT(om_pgs_file_name(fn, n12(""), NULL, 0), OM_E_FIELD);

    /* blocks and the fit message */
    CHECK_INT(om_pgs_blocks(6, 3), 3);                     /* the file's own count wins */
    CHECK_INT(om_pgs_blocks(10, 0), 1);                    /* Kai's 1 + (d + 0x1D) / 58 */
    CHECK_INT(om_pgs_blocks(28, 0), 1);
    CHECK_INT(om_pgs_blocks(29, 0), 2);
    CHECK_INT(om_pgs_blocks(30000, 0), 518);               /* the review's worked figure */
    om_pgs_fit_msg(line, sizeof line, 600, 64);
    CHECK_STR(line, "SAVES: 38 KB DO NOT FIT IN 32 KB BRAM");
    om_pgs_fit_msg(line, 12, 600, 64);
    CHECK_INT(strlen(line), 11);
    return th_done("test_pgsplan");
}
