/* menu/host-tests/test_bupfile.c -- the .BUP save file (SPEC §5.16) against contract/fixtures/tiny.bup,
 * corrupt and truncated files, the slot paths and the BRAM error lines. BSD-3-Clause. */
#include <string.h>
#include "th.h"
#include "bupfile.h"
#include "om_fmt.h"
#include <strings.h>
#include "cases.inc"

static const char k_ab[12] = { 'A', 'B' };       /* "AB", NUL-padded */

static om_bup_dir tiny_dir(void)
{   /* the values make_fixtures.py builds tiny.bup from */
    om_bup_dir d;
    memset(&d, 0, sizeof d);
    memcpy(d.name, "TESTSAVE01", 11);
    memcpy(d.comment, "VIBEMENU", 9);
    d.language = 1;
    d.date = 0x01020304u;
    d.datasize = 6;
    d.blocks = 1;
    return d;
}

static void name12(char out[12], const char *s)
{
    memset(out, 0, 12);
    memcpy(out, s, strlen(s) < 12 ? strlen(s) : 12);
}

int main(void)
{
    uint32_t len, i;
    uint8_t *f = th_load(FIX("tiny.bup"), &len), hdr[OM_BUP_HDR], *g;
    om_bup_dir d = tiny_dir(), r;
    char p[96], nm[12], msg[41];

    /* encode == the fixture's header, byte for byte */
    CHECK_INT(len, OM_BUP_HDR + 6);
    memset(hdr, 0xEE, sizeof hdr);
    CHECK_INT(om_bup_encode_hdr(&d, hdr), OM_OK);
    CHECK(memcmp(hdr, f, OM_BUP_HDR) == 0);
    CHECK(memcmp(f + OM_BUP_HDR, "ABCDEF", 6) == 0);
    /* the fixed fields, checked directly (so a matching-but-wrong fixture cannot hide a layout error) */
    CHECK(memcmp(hdr, "Vmem", 4) == 0);
    for (i = 4; i < 16; i++) CHECK_INT(hdr[i], 0);
    CHECK(memcmp(hdr + 0x10, "TESTSAVE01\0\0", 12) == 0);
    CHECK(memcmp(hdr + 0x1C, "VIBEMENU\0\0\0", 11) == 0);
    CHECK_INT(hdr[0x27], 1);
    CHECK_INT(om_be32(hdr + 0x28), 0x01020304u);
    CHECK_INT(om_be32(hdr + 0x2C), 6);
    CHECK_INT(om_be16(hdr + 0x30), 1);
    CHECK_INT(om_be16(hdr + 0x32), 0);
    CHECK_INT(om_be32(hdr + 0x34), 0x01020304u);
    for (i = 0x38; i < 0x40; i++) CHECK_INT(hdr[i], 0);

    /* decode returns the same values */
    memset(&r, 0x55, sizeof r);
    CHECK_INT(om_bup_decode_hdr(f, len, &r), OM_OK);
    CHECK(memcmp(r.name, d.name, 12) == 0);
    CHECK(memcmp(r.comment, d.comment, 11) == 0);
    CHECK_INT(r.language, 1); CHECK_INT(r.date, 0x01020304u); CHECK_INT(r.datasize, 6); CHECK_INT(r.blocks, 1);

    /* corrupt: a bad magic (each of the 4 bytes), truncated anywhere, a datasize past the end */
    for (i = 0; i < 4; i++) {
        g = th_dup(f, len);
        g[i] ^= 0x01;
        CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_E_MAGIC);
        free(g);
    }
    for (i = 0; i < len; i++)
        CHECK_INT(om_bup_decode_hdr(f, i, &r), OM_E_SHORT);
    g = th_dup(f, len);
    om_put32(g + 0x2C, 7);                                  /* says 7, carries 6 */
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_E_SHORT);
    om_put32(g + 0x2C, 0xFFFFFFF0u);                        /* 64 + datasize would wrap a uint32 */
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_E_SHORT);
    om_put32(g + 0x2C, 6);
    CHECK_INT(om_bup_decode_hdr(g, len + 0, &r), OM_OK);
    /* the name: empty or non-printable -> OM_E_FIELD; bytes after its NUL are ignored, and so is byte 11 (Save Game
     * Copier writes '.' there after an 11-character name: 39 of the 326 files in Kai's release SAVES tree) */
    g[0x10] = 0;
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_E_FIELD);
    memcpy(g + 0x10, "DRACULAX_02.", 12);
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_OK);
    CHECK(memcmp(r.name, "DRACULAX_02", 12) == 0);
    memcpy(g + 0x10, "AB\tD\0\0\0\0\0\0\0\0", 12);
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_E_FIELD);
    memcpy(g + 0x10, "AB\0XYZ\0\0\0\0\0\0", 12);
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_OK);
    CHECK(memcmp(r.name, "AB\0\0\0\0\0\0\0\0\0\0", 12) == 0);
    /* Shift-JIS comment bytes pass through untouched; the second date need not equal the first */
    memcpy(g + 0x10, "BLASTWIND_0\0", 12);
    memcpy(g + 0x1C, "\xCC\xDE\xD7\xBD\xC4\xB3\xB2\xDD\xC4\xDE\0", 11);
    om_put32(g + 0x34, 0x014FC6DBu);
    CHECK_INT(om_bup_decode_hdr(g, len, &r), OM_OK);
    CHECK(memcmp(r.comment, "\xCC\xDE\xD7\xBD\xC4\xB3\xB2\xDD\xC4\xDE\0", 11) == 0);
    CHECK_INT(r.date, 0x01020304u);
    free(g);

    /* encode refuses a bad name and writes nothing past 64 bytes */
    name12(d.name, "");
    CHECK_INT(om_bup_encode_hdr(&d, hdr), OM_E_FIELD);
    memcpy(d.name, "ABC\001EFGHIJK", 12);
    CHECK_INT(om_bup_encode_hdr(&d, hdr), OM_E_FIELD);
    memcpy(d.name, "ABCDEFGHIJK.", 12);                     /* an SGC-style name: written with its NUL */
    CHECK_INT(om_bup_encode_hdr(&d, hdr), OM_OK);
    CHECK(memcmp(hdr + 0x10, "ABCDEFGHIJK", 12) == 0);
    name12(d.name, "AB");
    d.name[5] = 'X';                                        /* garbage after the NUL: encode pads with NUL */
    CHECK_INT(om_bup_name_ok(d.name), 0);
    CHECK_INT(om_bup_encode_hdr(&d, hdr), OM_OK);
    CHECK(memcmp(hdr + 0x10, k_ab, 12) == 0);
    CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 0, d.name), OM_OK);
    CHECK_STR(p, "/S/G/AB.BUP");
    CHECK_INT(om_bup_name_norm(nm, d.name), 1);
    CHECK(memcmp(nm, k_ab, 12) == 0);
    CHECK_INT(om_bup_name_norm(nm, "ABCDEFGHIJKL"), 1);
    CHECK(memcmp(nm, "ABCDEFGHIJK", 12) == 0);
    CHECK_INT(om_bup_name_ok("ABCDEFGHIJKL"), 0);           /* name_ok stays strict: byte 11 must be NUL */

    /* name_ok */
    name12(nm, "TESTSAVE01"); CHECK_INT(om_bup_name_ok(nm), 1);
    name12(nm, "ABCDEFGHIJK"); CHECK_INT(om_bup_name_ok(nm), 1);
    name12(nm, "A B"); CHECK_INT(om_bup_name_ok(nm), 1);
    name12(nm, ""); CHECK_INT(om_bup_name_ok(nm), 0);
    name12(nm, "A\x7F"); CHECK_INT(om_bup_name_ok(nm), 0);
    name12(nm, "A\xC4"); CHECK_INT(om_bup_name_ok(nm), 0);

    /* slot paths: slot 0 in the folder, 1..9 in "01".."09" (Kai's layout) */
    name12(nm, "TESTSAVE01");
    CHECK_INT(om_bup_slot_path(p, sizeof p, "/aether/SAVES", "MK-81214[UT]", 0, nm), OM_OK);
    CHECK_STR(p, "/aether/SAVES/MK-81214[UT]/TESTSAVE01.BUP");
    CHECK_INT(om_bup_slot_path(p, sizeof p, "/aether/SAVES", "MK-81214[UT]", 1, nm), OM_OK);
    CHECK_STR(p, "/aether/SAVES/MK-81214[UT]/01/TESTSAVE01.BUP");
    CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 9, nm), OM_OK);
    CHECK_STR(p, "/S/G/09/TESTSAVE01.BUP");
    CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 10, nm), OM_E_RANGE);
    name12(nm, "A:B"); CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 0, nm), OM_E_FIELD);
    name12(nm, "A/B"); CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 0, nm), OM_E_FIELD);
    name12(nm, "A*B"); CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 0, nm), OM_E_FIELD);
    name12(nm, ""); CHECK_INT(om_bup_slot_path(p, sizeof p, "/S", "G", 0, nm), OM_E_FIELD);
    name12(nm, "TESTSAVE01");
    /* "/S/G/TESTSAVE01.BUP" is 19 B + NUL */
    CHECK_INT(om_bup_slot_path(p, 20, "/S", "G", 0, nm), OM_OK);
    CHECK_STR(p, "/S/G/TESTSAVE01.BUP");
    memset(p, 'Q', sizeof p);
    CHECK_INT(om_bup_slot_path(p, 19, "/S", "G", 0, nm), OM_E_FULL);
    CHECK_INT(p[19], 'Q');                                  /* nothing written past cap */

    /* the error-screen lines */
    om_bup_msg(msg, sizeof msg, "INIT", OM_BUP_UNFORMAT); CHECK_STR(msg, "BRAM: NOT FORMATTED");
    om_bup_msg(msg, sizeof msg, "WRITE", OM_BUP_NOMEM); CHECK_STR(msg, "BRAM: FULL");
    om_bup_msg(msg, sizeof msg, "WRITE", OM_BUP_NON); CHECK_STR(msg, "BRAM: NOT CONNECTED");
    om_bup_msg(msg, sizeof msg, "READ", OM_BUP_BROKEN); CHECK_STR(msg, "BRAM: RECORD DAMAGED");
    om_bup_msg(msg, sizeof msg, "WRITE", OM_BUP_NOMATCH); CHECK_STR(msg, "BRAM: VERIFY FAILED");
    om_bup_msg(msg, sizeof msg, "WRITE", -93); CHECK_STR(msg, "BRAM: WRITE FAILED rc=-93");
    om_bup_msg(msg, sizeof msg, "WRITE", 9); CHECK_STR(msg, "BRAM: WRITE FAILED rc=9");
    om_bup_msg(msg, 12, "WRITE", -93);
    CHECK_INT((int)strlen(msg), 11);

    /* TechBulletins #9 */
    CHECK_INT(om_bup_mm_size(0), 1); CHECK_INT(om_bup_mm_size(32), 1); CHECK_INT(om_bup_mm_size(33), 2);
    CHECK_INT(om_bup_mm_size(268), 5);

    /* the 326 real headers of Kai's release (kai_bup_headers.json via gen_cases.py) decode as
     * the Python codec decodes them */
    {
        uint32_t k, bad = 0, own = 0;
        for (k = 0; k < KB_N; k++) {
            om_bup_dir e;
            int rc = om_bup_decode_hdr((const uint8_t *)KB[k].hdr, KB[k].len, &e);
            char want[12];
            name12(want, KB[k].name);
            if ((rc == OM_OK) != KB[k].ok || (rc == OM_OK && (memcmp(e.name, want, 12) || e.datasize != KB[k].datasize
                                                               || e.blocks != KB[k].blocks))) {
                bad++;
                printf("corpus: %s rc %d\n", KB[k].path, rc);
            }
            if (rc == OM_OK) {
                char fn[20];
                snprintf(fn, sizeof fn, "%s.BUP", e.name);
                if (!strcasecmp(fn, KB[k].file)) own++;
            }
        }
        CHECK_INT(KB_N, 326);
        CHECK_INT(bad, 0);
        CHECK_INT(own, 326 - 89);                           /* 89 files are not named after their record */
        /* duplicates in one folder resolve as SPEC §5.16 says (the Python rule's winners) */
        CHECK_INT(KB_NDUP, 3);
        for (k = 0; k < KB_NDUP; k++) {
            int w = KB_DUP[k].idx[0];
            unsigned j;
            CHECK(KB_DUP[k].n <= KB_DUP_W);                 /* the row holds the whole group */
            for (j = 1; j < KB_DUP[k].n && j < KB_DUP_W; j++) {
                int c = KB_DUP[k].idx[j];
                if (om_bup_prefer((const uint8_t *)KB[c].hdr, KB[c].file, (const uint8_t *)KB[w].hdr, KB[w].file)) w = c;
            }
            CHECK_INT(w, KB_DUP[k].winner);
        }
    }
    /* the order of the keys: date, then save_id, then the own-name file, then file-name order */
    {
        uint8_t a[OM_BUP_HDR], b[OM_BUP_HDR];
        om_bup_dir t = tiny_dir();
        om_bup_encode_hdr(&t, a);
        memcpy(b, a, sizeof b);
        CHECK_INT(om_bup_prefer(a, "X.BUP", b, "Y.BUP"), 1);              /* all equal: byte order of the names */
        CHECK_INT(om_bup_prefer(a, "Y.BUP", b, "X.BUP"), 0);
        CHECK_INT(om_bup_prefer(a, "Z.BUP", b, "testsave01.bup"), 0);     /* the own-name file, any case */
        om_put32(a + 0x04, 5);
        CHECK_INT(om_bup_prefer(a, "Z.BUP", b, "TESTSAVE01.BUP"), 1);     /* save_id beats the name */
        om_put32(b + 0x28, 0x01020305u);
        CHECK_INT(om_bup_prefer(a, "Z.BUP", b, "TESTSAVE01.BUP"), 0);     /* the date beats save_id */
    }

    free(f);
    return th_done("test_bupfile");
}
