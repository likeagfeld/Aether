/* menu/launcher/bramtest.c -- see bramtest.h (Step 6). The diagnostics listing of the console's internal
 * backup RAM: hold Z while the launcher shows step 1. It lists every record (name, bytes, blocks) and the free
 * blocks, writes no record, waits for A and then the launch goes on. (BUP_Init itself is among the calls ST-162-R1
 * §1.2.5 says "will destroy data if writing is interrupted", so reset is disabled around it -- review M1.) The count
 * line also shows the reset button's mode as Init read it. The buffers come from the launcher heap
 * (launcher.ld counts them in its heap floor) and go back to it before step 2.
 *
 * The L1 harness (menu/spikes/l1_lbtest, OM_HO_TEST_BRAM, emulator only) also runs a write / verify / read / delete
 * round trip of one test record (contract/fixtures/tiny.bup's values) on the emulator's backup RAM, and formats it
 * first if the emulator reports it unformatted. That code exists only in a launcher built with
 * OM_L1_HARNESS (menu/launcher/out_l1, embedded by the L1 harness alone); release LAUNCHER.BIN has no write, delete or
 * format call here, sbuild's check_launcher fails on its symbols, and its om_handoff_check refuses OM_HO_TEST_BRAM.
 * The L1 result lines go to row 3, between the image name and step 1.
 * BSD-3-Clause. */
#include <stdlib.h>
#include <string.h>
#include "bram.h"
#include "bramtest.h"
#include "lvec.h"
#include "om_fmt.h"
#include "smpcpad.h"
#include "vdp2txt.h"

#define LIST_CAP  96u                      /* records listed (32 KB / 64 B blocks; most saves take several) */
#define TOP       14                       /* first text row used: lseq.c prints steps 1-9 on rows 4-12 */
#define ROWS      9                        /* records per page, rows 18..26 */
#define L1_FIELDS 240u                     /* the L1 harness has no pad: continue after ~4 s */

static void blank(int y0, int y1)
{
    int y;
    for (y = y0; y <= y1; y++)
        om_txt_puts(0, y, "                                        ", 0);
}

static char *put_s(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static char *put_u(char *o, uint32_t v, int width)
{
    char t[11];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (width-- > n) *o++ = ' ';
    while (n) *o++ = t[--n];
    *o = 0;
    return o;
}

static void show_rc(int y, const char *op, int rc)
{
    char l[41];
    om_bup_msg(l, sizeof l, op, rc);
    om_txt_puts(0, y, l, 2);
}

/* one record per row: "NAME_______ 123456 B  1234 BL  MM 12" (MM: the Memory Manager's Size, TechBulletins #9) */
static void show_page(const om_bup_dir *t, uint32_t n, uint32_t first)
{
    uint32_t i;
    blank(TOP + 4, TOP + 4 + ROWS - 1);
    for (i = 0; i < ROWS && first + i < n; i++) {
        const om_bup_dir *d = &t[first + i];
        char l[41], nm[12], *o = l;
        int k;
        if (!om_bup_name_norm(nm, d->name))
            memcpy(nm, "?\0\0\0\0\0\0\0\0\0\0\0", 12);
        for (k = 0; k < 11; k++) *o++ = nm[k] ? nm[k] : ' ';
        *o++ = ' ';
        o = put_u(o, d->datasize, 6);
        o = put_s(o, " B ");
        o = put_u(o, d->blocks, 5);
        o = put_s(o, " BL  MM ");
        o = put_u(o, om_bup_mm_size(d->datasize), 3);
        om_txt_puts(0, TOP + 4 + (int)i, l, 0);
    }
}

/* A (or, under L1, the time-out) -> 0; B -> 1 */
static int wait_ab(int l1)
{
    uint16_t prev = 0xFFFFu;                       /* a button already held is not a press */
    uint32_t f;
    for (f = 0; !l1 || f < L1_FIELDS; f++) {
        uint16_t p = om_smpc_pad(), newly = (uint16_t)(p & ~prev);
        prev = p;
        if (newly & OM_PAD_A) return 0;
        if (newly & OM_PAD_B) return 1;
    }
    return 0;
}

#ifdef OM_L1_HARNESS
int om_bram_l1_roundtrip(int y);
void om_bram_l1_finish(int y);

int om_bram_l1_roundtrip(int y)
{   /* L1 only: tiny.bup's record, written, listed by the caller, then read back and deleted by l1_finish */
    om_bup_dir d;
    int rc;
    memset(&d, 0, sizeof d);
    memcpy(d.name, "TESTSAVE01", 11);
    memcpy(d.comment, "VIBEMENU", 9);
    d.language = 1;
    d.date = 0x01020304u;
    d.datasize = 6;
    rc = om_bram_write(&d, (const uint8_t *)"ABCDEF");
    if (rc) show_rc(y, "WRITE", rc);
    else om_txt_puts(0, y, "L1: WRITE + VERIFY TESTSAVE01 OK", 1);
    return rc;
}

void om_bram_l1_finish(int y)
{
    static uint32_t buf[4];                        /* 16 B: 6 data bytes, a guard after them */
    om_bup_dir d;
    char l[41], *o;
    int rc;
    uint32_t i, extra = 0;
    memset(buf, 0x5A, sizeof buf);
    rc = om_bram_read("TESTSAVE01\0", (uint8_t *)buf, sizeof buf, &d);
    for (i = 7; i < sizeof buf; i++)               /* byte 6 is om_bram_read's own guard */
        if (((uint8_t *)buf)[i] != 0x5A) extra++;
    if (rc) { show_rc(y, "READ", rc); return; }
    if (memcmp(buf, "ABCDEF", 6)) { om_txt_puts(0, y, "L1: READ BACK DIFFERS", 2); return; }
    rc = om_bram_delete("TESTSAVE01\0");
    if (rc) { show_rc(y, "DELETE", rc); return; }
    o = put_s(l, "L1: READ OK, DELETE OK, BL ");
    o = put_u(o, d.blocks, 1);
    o = put_s(o, " PAST ");
    put_u(o, extra, 1);
    om_txt_puts(0, y, l, 1);
}
#endif

void om_bram_selftest(int l1)
{
    uint8_t *raw = (uint8_t *)malloc(OM_BRAM_BUF_BYTES + LIST_CAP * sizeof(om_bup_dir) + 4u), *lib, *work;
    om_bup_dir *tab;
    om_bram_info st;
    const om_bram_unit *u;
    uint32_t n = 0, first = 0, f;
    char l[41], h[9], *o;
    int rc, rt = 1;
    blank(TOP, 27);
    om_txt_puts(0, TOP, l1 ? "BRAM SELF-TEST (L1: WRITE/READ/DELETE)" : "BRAM (INTERNAL): LIST, NO RECORD WRITTEN", 1);
    if (!raw) {
        om_txt_puts(0, TOP + 1, "BRAM: NO HEAP FOR THE LIBRARY", 2);
        wait_ab(l1);
        blank(TOP, 27);
        return;
    }
    lib = (uint8_t *)(((uintptr_t)raw + 3u) & ~(uintptr_t)3u);
    work = lib + OM_BRAM_LIB_BYTES + OM_BRAM_GUARD;
    tab = (om_bup_dir *)(work + OM_BRAM_WORK_BYTES + OM_BRAM_GUARD);
    rc = om_bram_init(lib, work);
    u = om_bram_units();
    om_hex8(h, om_bram_lib_entry());
    o = put_s(l, "BUP_INIT ");
    o = put_s(o, h);
    o = put_s(o, " UNITS ");
    for (f = 0; f < 3; f++) {
        o = put_u(o, u[f].unit_id, 1);
        *o++ = '/';
        o = put_u(o, u[f].partition, 1);
        *o++ = ' ';
    }
    *o = 0;
    om_txt_puts(0, TOP + 1, l, 0);
#ifdef OM_L1_HARNESS
    if (rc == OM_BUP_UNFORMAT && l1) {             /* the emulator's fresh backup RAM only (see the top) */
        om_txt_puts(0, TOP + 2, "L1: FORMATTING THE EMULATOR'S BRAM", 1);
        rc = om_bram_format();
    }
#endif
    if (rc) {
        show_rc(TOP + 2, "INIT", rc);
        om_txt_puts(0, 27, "A: CONTINUE THE LAUNCH", 1);
        wait_ab(l1);
        free(raw);
        blank(TOP, 27);
        return;
    }
#ifdef OM_L1_HARNESS
    if (l1)
        rt = om_bram_l1_roundtrip(3);
#endif
    if ((rc = om_bram_info_get(0, &st)) != 0)
        show_rc(TOP + 2, "STAT", rc);
    else {
        o = put_s(l, "FREE ");
        o = put_u(o, st.freeblock, 1);
        o = put_s(o, " OF ");
        o = put_u(o, st.totalblock, 1);
        o = put_s(o, " BLOCKS x ");
        o = put_u(o, st.blocksize, 1);
        put_s(o, " B");
        om_txt_puts(0, TOP + 2, l, 0);
    }
    rc = om_bram_list(tab, LIST_CAP, &n);
    o = put_u(l, n, 1);
    o = put_s(o, n == 1 ? " RECORD" : " RECORDS");
    o = put_s(o, om_bram_reset_mode() == 0 ? "  RESET WAS ON" : om_bram_reset_mode() == 1 ? "  RESET WAS OFF"
                                                                                       : "  RESET MODE ?");
    if (rc == OM_E_TABLESIZE) { put_s(o, " (96 SHOWN)"); n = LIST_CAP; }
    else if (rc) { show_rc(TOP + 3, "DIR", rc); n = 0; }
    if (!rc || rc == OM_E_TABLESIZE)
        om_txt_puts(0, TOP + 3, l, 0);
    om_txt_puts(0, 27, n > ROWS ? "A: CONTINUE   B: NEXT PAGE" : "A: CONTINUE THE LAUNCH", 1);
    for (;;) {
        show_page(tab, n, first);
        if (!wait_ab(l1)) break;
        first = first + ROWS < n ? first + ROWS : 0;
    }
#ifdef OM_L1_HARNESS
    if (l1 && !rt)
        om_bram_l1_finish(3);
#else
    (void)rt;
#endif
    if (l1)
        for (f = 0; f < 120u; f++) om_smpc_pad();  /* ~2 s to read the result line */
    free(raw);
    blank(TOP, 27);
}
