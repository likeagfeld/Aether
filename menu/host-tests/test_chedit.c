/* menu/host-tests/test_chedit.c -- the launcher's cheat editor model and the CHEATS.PAK
 * rewrite (menu/launcher/chedit.h): explicit toggles, the cap and its slots line, adding and editing a code with
 * validation and its reason, Kai's cheats read only, deletes asked twice, a corrupt file read only, a missing file
 * started fresh; the whole-file rewrite byte-identical when nothing changed (every key of the fixture and of the
 * shipped CHEATS.PAK, the PC writer's own layout), a shared record split without touching the other key, a new key
 * placed in order, an emptied record removed. BSD-3-Clause. */
#include "th.h"
#include "chedit.h"
#include "chkai.h"
#include "cheatpak.h"
#include "om_fmt.h"

static om_ch_file f, g;
static om_ced e;
static uint8_t out[OM_CP_MAX_FILE], out2[OM_CP_MAX_FILE], rec[OM_CP_MAX_REC];

static int key(uint16_t k) { return om_ced_key(&e, k); }
static void keys(uint16_t k, int n) { while (n--) key(k); }

static void one_code_cheat(om_ch_file *x, const char *name, int on, uint32_t addr)
{
    om_ch_cheat *c = &x->cheat[x->ncheats++];
    strcpy(c->name, name);
    c->flags = on ? OM_CH_ON : 0;
    c->first = x->ncodes;
    c->count = 1;
    x->code[x->ncodes].addr = addr;
    x->code[x->ncodes].value = 1;
    x->code[x->ncodes].cmd = 1;
    x->ncodes++;
}

static void fixture_list(void)
{
    uint32_t n;
    uint8_t *d = th_load(FIX("tiny.cheats.pak"), &n);
    CHECK_INT(om_cpk_find(d, n, "GS-9001", &f), 1);
    CHECK_INT(om_cpk_find(d, n, "NOPE", &g), 0);
    om_ced_open(&e, &f, 0);
    CHECK_INT(e.f->ncheats, 4);
    CHECK_INT(e.row, 0);
    key(OM_CK_A);                                          /* row 0 is a label row ("PLAYER 1") */
    CHECK_INT(e.msg, OM_CEM_LABEL);
    CHECK_INT(e.dirty, 0);
    key(OM_CK_D);
    CHECK_INT(e.row, 1);
    CHECK_INT(f.cheat[1].flags & OM_CH_ON, 0);             /* a cursor move never changes `on` */
    CHECK_INT(e.dirty, 0);
    key(OM_CK_A);
    CHECK_INT(f.cheat[1].flags & OM_CH_ON, OM_CH_ON);
    CHECK_INT(e.dirty, 1);
    key(OM_CK_A);
    CHECK_INT(f.cheat[1].flags & OM_CH_ON, 0);
    key(OM_CK_Y);                                          /* Kai's cheat: read only */
    CHECK_INT(e.msg, OM_CEM_KAI);
    CHECK_INT(e.page, OM_CE_LIST);
    key(OM_CK_Z);
    CHECK_INT(e.msg, OM_CEM_KAI);
    CHECK_INT(f.ncheats, 4);
    keys(OM_CK_D, 2);                                      /* "MY CODE", added by the user */
    CHECK_INT(e.row, 3);
    key(OM_CK_D);
    CHECK_INT(e.row, 0);                                   /* Down on the last row: the first (it wraps) */
    key(OM_CK_U);
    CHECK_INT(e.row, 3);                                   /* and Up on the first: the last */
    key(OM_CK_Y);
    CHECK_INT(e.page, OM_CE_CODES);
    key(OM_CK_B);
    CHECK_INT(e.page, OM_CE_LIST);
    key(OM_CK_Z);
    CHECK_INT(e.msg, OM_CEM_ASK);
    CHECK_INT(f.ncheats, 4);
    key(OM_CK_U);                                          /* any other key disarms */
    key(OM_CK_D);
    key(OM_CK_Z);
    key(OM_CK_Z);
    CHECK_INT(f.ncheats, 3);
    CHECK_INT(f.ncodes, 3);
    CHECK_INT(e.row, 2);
    CHECK_INT(key(OM_CK_B), 1);
    free(d);
}

/* the cap: 29 one-code cheats on, a 2-code cheat refused with the slots left, then allowed once one is off */
static void cap_limit(void)
{
    char s[48];
    int i;
    memset(&f, 0, sizeof f);
    om_chk_default_master(&f);
    for (i = 0; i < 29; i++) one_code_cheat(&f, "ONE", 1, 0x06070000u + 2u * (uint32_t)i);
    {
        om_ch_cheat *c = &f.cheat[f.ncheats++];
        strcpy(c->name, "TWO");
        c->first = f.ncodes;
        c->count = 2;
        f.code[f.ncodes].addr = 0x06071000u; f.code[f.ncodes].value = 1; f.code[f.ncodes++].cmd = 1;
        f.code[f.ncodes].addr = 0x06071002u; f.code[f.ncodes].value = 1; f.code[f.ncodes++].cmd = 1;
    }
    om_ced_open(&e, &f, 0);
    CHECK_INT(e.cap, 30);
    CHECK_INT(om_ced_active(&e), 29);
    om_ced_slots(&e, s, sizeof s);
    CHECK_STR(s, "30 - 29 = 1 SLOT LEFT");
    keys(OM_CK_D, 29);
    CHECK_INT(e.row, 29);
    key(OM_CK_A);
    CHECK_INT(e.msg, OM_CEM_FULL);
    CHECK_INT(f.cheat[29].flags & OM_CH_ON, 0);
    CHECK_INT(e.dirty, 0);
    key(OM_CK_U);
    key(OM_CK_A);                                          /* one off: 28 */
    om_ced_slots(&e, s, sizeof s);
    CHECK_STR(s, "30 - 28 = 2 SLOTS LEFT");
    key(OM_CK_D);
    key(OM_CK_A);
    CHECK_INT(f.cheat[29].flags & OM_CH_ON, OM_CH_ON);
    CHECK_INT(om_ced_active(&e), 30);
    /* Kai's own small cache: cache=1 at Kai's own place -> 4 */
    memset(&f, 0, sizeof f);
    f.flags = OM_CP_G_MASTER | OM_CP_G_CWX;
    f.master_addr = 0x06000914u; f.master_value = 0xC305; f.location = 0x060FF000u; f.cache4 = 1;
    for (i = 0; i < 5; i++) one_code_cheat(&f, "ONE", i < 4, 0x06070000u + 2u * (uint32_t)i);
    om_ced_open(&e, &f, 0);
    CHECK_INT(e.cap, 4);
    om_ced_slots(&e, s, sizeof s);
    CHECK_STR(s, "4 - 4 = 0 SLOTS LEFT");
    keys(OM_CK_D, 4);
    key(OM_CK_A);
    CHECK_INT(e.msg, OM_CEM_FULL);
}

static void set_code(uint32_t addr, uint16_t val, int type_steps)
{   /* the D-pad way: each digit from its current value, Right to the next field */
    int d;
    while (e.field) key(OM_CK_L);
    for (d = 0; d < 8; d++) {
        uint32_t want = (addr >> (28 - 4 * d)) & 15u, have = (e.addr >> (28 - 4 * d)) & 15u;
        while (have != want) { key(OM_CK_U); have = (e.addr >> (28 - 4 * d)) & 15u; }
        key(OM_CK_R);
    }
    for (d = 0; d < 4; d++) {
        uint32_t want = (val >> (12 - 4 * d)) & 15u, have = (e.val >> (12 - 4 * d)) & 15u;
        while (have != want) { key(OM_CK_D); have = (e.val >> (12 - 4 * d)) & 15u; }
        key(OM_CK_R);
    }
    CHECK_INT(e.field, 12);
    keys(OM_CK_U, type_steps);
}

static void add_flow(void)
{
    char t[18];
    memset(&f, 0, sizeof f);
    om_ced_open(&e, &f, 1);                                /* no record: an empty list */
    CHECK_INT(f.ncheats, 0);
    key(OM_CK_A);
    CHECK_INT(e.dirty, 0);
    key(OM_CK_X);
    CHECK_INT(e.page, OM_CE_NAME);
    key(OM_CK_B);                                          /* cancel: nothing added */
    CHECK_INT(e.page, OM_CE_LIST);
    CHECK_INT(f.ncheats, 0);
    key(OM_CK_X);
    key(OM_CK_A);                                          /* an empty name is refused */
    CHECK_INT(e.msg, OM_CEM_NAME);
    CHECK_INT(e.page, OM_CE_NAME);
    keys(OM_CK_U, 16);                                     /* ' ' -> 'P' */
    key(OM_CK_R);
    keys(OM_CK_U, 2);                                      /* 'B' */
    key(OM_CK_R);
    key(OM_CK_D);                                          /* ' ' -> the wheel's last character */
    key(OM_CK_U);                                          /* back to ' ' */
    key(OM_CK_R);
    CHECK_STR(e.name, "PB ");
    key(OM_CK_A);
    CHECK_INT(e.page, OM_CE_CODE);
    om_ced_code_text(&e, t);
    CHECK_STR(t, "06000000:0000:01");
    key(OM_CK_A);                                          /* the template is the BIOS area: refused, with why */
    CHECK_INT(e.msg, OM_CEM_BAD);
    CHECK_INT(e.why, OM_CHV_GUARD);
    CHECK_INT(f.ncheats, 0);
    set_code(0x0607FFFFu, 0x0001, 0);
    key(OM_CK_A);
    CHECK_INT(e.why, OM_CHV_ODD);
    set_code(0x06002600u, 0x0001, 0);
    key(OM_CK_A);
    CHECK_INT(e.why, OM_CHV_BLOB);
    set_code(0x05000000u, 0x0001, 0);
    key(OM_CK_A);
    CHECK_INT(e.why, OM_CHV_RANGE);
    CHECK_INT(f.ncheats, 0);
    set_code(0x06078A5Cu, 0x0080, 0);
    om_ced_code_text(&e, t);
    CHECK_STR(t, "06078A5C:0080:01");
    key(OM_CK_U);                                          /* type 01 -> 03 -> 0D -> 00 -> 01 */
    om_ced_code_text(&e, t);
    CHECK_STR(t, "06078A5C:0080:03");
    keys(OM_CK_U, 3);
    CHECK_INT(e.cmd, 0x01);
    key(OM_CK_A);
    CHECK_INT(e.page, OM_CE_LIST);
    CHECK_INT(f.ncheats, 1);
    CHECK_STR(f.cheat[0].name, "PB");                      /* trailing spaces trimmed */
    CHECK_INT(f.cheat[0].flags, OM_CH_USER);               /* added off */
    CHECK_INT(f.cheat[0].count, 1);
    CHECK_INT(f.code[0].addr, 0x06078A5Cu);
    CHECK_INT(f.code[0].value, 0x0080);
    CHECK_INT(f.code[0].cmd, 0x01);
    CHECK_INT(f.flags & OM_CP_G_MASTER, OM_CP_G_MASTER);   /* a new record gets Kai's default master */
    CHECK_INT(f.master_addr, OM_CHK_DEF_MASTER);
    CHECK_INT(e.dirty, 1);
    key(OM_CK_A);
    CHECK_INT(f.cheat[0].flags, OM_CH_USER | OM_CH_ON);
    /* its codes: add one, edit it, delete it (asked twice), the last one stays */
    key(OM_CK_Y);
    CHECK_INT(e.page, OM_CE_CODES);
    key(OM_CK_X);
    CHECK_INT(e.page, OM_CE_CODE);
    set_code(0x06078B50u, 0x0080, 0);
    key(OM_CK_A);
    CHECK_INT(e.page, OM_CE_CODES);
    CHECK_INT(f.cheat[0].count, 2);
    CHECK_INT(f.ncodes, 2);
    CHECK_INT(f.code[1].addr, 0x06078B50u);
    key(OM_CK_D | OM_CK_REP);                              /* held on the last code: it stays */
    key(OM_CK_A);
    set_code(0x06078B52u, 0x00FF, 1);
    key(OM_CK_A);
    CHECK_INT(f.code[1].addr, 0x06078B52u);
    CHECK_INT(f.code[1].cmd, 0x03);
    key(OM_CK_A);
    key(OM_CK_B);                                          /* edit cancelled: unchanged */
    CHECK_INT(f.code[1].addr, 0x06078B52u);
    key(OM_CK_Z);
    key(OM_CK_Z);
    CHECK_INT(f.cheat[0].count, 1);
    CHECK_INT(f.ncodes, 1);
    key(OM_CK_Z);
    key(OM_CK_Z);
    CHECK_INT(e.msg, OM_CEM_LAST);
    CHECK_INT(f.cheat[0].count, 1);
    key(OM_CK_Y);                                          /* rename */
    CHECK_INT(e.page, OM_CE_NAME);
    CHECK_STR(e.name, "PB");
    key(OM_CK_A);
    CHECK_INT(e.page, OM_CE_CODES);
    key(OM_CK_B);
    CHECK_INT(key(OM_CK_B), 1);
}

/* a code added to a cheat that is on counts against the cap at once */
static void add_code_cap(void)
{
    int i;
    memset(&f, 0, sizeof f);
    om_chk_default_master(&f);
    for (i = 0; i < 30; i++) one_code_cheat(&f, "ONE", 1, 0x06070000u + 2u * (uint32_t)i);
    f.cheat[0].flags |= OM_CH_USER;
    om_ced_open(&e, &f, 0);
    key(OM_CK_Y);
    key(OM_CK_X);
    CHECK_INT(e.msg, OM_CEM_FULL);
    CHECK_INT(e.page, OM_CE_CODES);
}

static void readonly_and_full(void)
{
    int i;
    memset(&f, 0, sizeof f);
    om_ced_open(&e, &f, 2);
    CHECK_INT(e.ro, 1);
    key(OM_CK_X);
    CHECK_INT(e.msg, OM_CEM_READONLY);
    CHECK_INT(f.ncheats, 0);
    key(OM_CK_A);
    CHECK_INT(e.msg, OM_CEM_READONLY);
    CHECK_INT(key(OM_CK_B), 1);
    CHECK_INT(e.dirty, 0);
    memset(&f, 0, sizeof f);
    om_chk_default_master(&f);
    for (i = 0; i < 64; i++) one_code_cheat(&f, "ONE", 0, 0x06070000u);
    om_ced_open(&e, &f, 0);
    key(OM_CK_X);
    CHECK_INT(e.msg, OM_CEM_FILEFULL);
    CHECK_INT(e.page, OM_CE_LIST);
    CHECK_INT(f.ncheats, 64);
    for (i = 0; i < 20; i++) key(OM_CK_D);
    CHECK_INT(e.row, 20);
    CHECK(e.top <= e.row && e.row < e.top + OM_CE_VIS);    /* the window follows */
}

static int pak_ok(const uint8_t *d, uint32_t n)
{   /* the reader's checks plus sorted, unique keys */
    om_cp_hdr h;
    uint32_t i, off, len;
    if (om_cp_header(d, &h) || h.data_end != n || om_crc32(0, d + 64, h.count * OM_CP_ROW) != h.table_crc)
        return 0;
    for (i = 0; i < h.count; i++) {
        const uint8_t *r = d + 64 + i * OM_CP_ROW;
        if (om_cp_row(&h, r, &off, &len) || om_cp_decode(d + off, len, &g))
            return 0;
        if (i && memcmp(r - OM_CP_ROW, r, 16) >= 0)
            return 0;
    }
    return 1;
}

static void pak_rewrite(const char *path, int all)
{
    uint32_t n, m, i, k;
    om_cp_hdr h;
    uint8_t *d = th_load(path, &n);
    CHECK_INT(om_cp_header(d, &h), OM_OK);
    for (i = 0, k = 0; i < h.count; i++) {
        char key16[17];
        memcpy(key16, d + 64 + i * OM_CP_ROW, 16);
        key16[16] = 0;
        if (!all && i) break;
        CHECK_INT(om_cpk_find(d, n, key16, &f), 1);
        CHECK_INT(om_cpk_put(d, n, key16, &f, 0, out, sizeof out, rec, &m), OM_OK);
        if (m == n && !memcmp(out, d, n)) k++;
    }
    CHECK_INT(k, all ? h.count : 1);                       /* a save that changes nothing: the same bytes */
    printf("%s: %u of %u keys rewritten byte-identical\n", path, k, all ? h.count : 1);
    free(d);
}

static void pak_edits(void)
{
    uint32_t n, m, m2;
    uint8_t *d = th_load(FIX("tiny.cheats.pak"), &n);
    om_ch_file before;
    /* T-8113H-50 shares GS-9001's record: toggling it gives it its own and leaves GS-9001's bytes */
    CHECK_INT(om_cpk_find(d, n, "GS-9001", &before), 1);
    CHECK_INT(om_cpk_find(d, n, "T-8113H-50", &f), 1);
    f.cheat[1].flags |= OM_CH_ON;
    CHECK_INT(om_cpk_put(d, n, "T-8113H-50", &f, 0, out, sizeof out, rec, &m), OM_OK);
    CHECK(pak_ok(out, m));
    CHECK_INT(m, n + 104);
    CHECK_INT(om_cpk_find(out, m, "GS-9001", &g), 1);
    CHECK(!memcmp(&g, &before, sizeof g));
    CHECK_INT(om_cpk_find(out, m, "T-8113H-50", &g), 1);
    CHECK_INT(g.cheat[1].flags & OM_CH_ON, OM_CH_ON);
    CHECK_INT(om_cpk_find(out, m, "T-8113H", &g), 1);
    CHECK_INT(g.ncheats, 1);
    CHECK_INT(om_be32(out + 0x20), 20261117u);             /* the build date is kept */
    /* toggled back: the PC writer stores identical records once, so the file is the original again */
    f.cheat[1].flags &= (uint8_t)~OM_CH_ON;
    CHECK_INT(om_cpk_put(out, m, "T-8113H-50", &f, 0, out2, sizeof out2, rec, &m2), OM_OK);
    CHECK_INT(m2, n);
    CHECK(!memcmp(out2, d, n));
    /* a new key goes in order (first and last), an emptied record goes */
    memset(&f, 0, sizeof f);
    om_chk_default_master(&f);
    one_code_cheat(&f, "NEW", 0, 0x06078A5Cu);
    CHECK_INT(om_cpk_put(d, n, "AAA", &f, 0, out, sizeof out, rec, &m), OM_OK);
    CHECK(pak_ok(out, m));
    CHECK(!memcmp(out + 64, "AAA\0", 4));
    CHECK_INT(om_cpk_find(out, m, "AAA", &g), 1);
    CHECK_INT(g.master_addr, OM_CHK_DEF_MASTER);
    CHECK_INT(om_cpk_put(d, n, "ZZZ", &f, 0, out, sizeof out, rec, &m), OM_OK);
    CHECK(pak_ok(out, m));
    CHECK(!memcmp(out + 64 + 3 * OM_CP_ROW, "ZZZ\0", 4));
    f.ncheats = 0;
    f.ncodes = 0;
    CHECK_INT(om_cpk_put(out, m, "ZZZ", &f, 0, out2, sizeof out2, rec, &m2), OM_OK);
    CHECK_INT(m2, n);
    CHECK(!memcmp(out2, d, n));
    /* no file yet: a fresh one with the given date */
    memset(&f, 0, sizeof f);
    om_chk_default_master(&f);
    one_code_cheat(&f, "NEW", 1, 0x06078A5Cu);
    CHECK_INT(om_cpk_put(NULL, 0, "MK-81045", &f, 20261124u, out, sizeof out, rec, &m), OM_OK);
    CHECK(pak_ok(out, m));
    CHECK_INT(om_be32(out + 0x20), 20261124u);
    CHECK_INT(om_cpk_find(out, m, "MK-81045", &g), 1);
    CHECK_INT(g.cheat[0].flags, OM_CH_ON);
    /* too small an output, a damaged input */
    CHECK_INT(om_cpk_put(d, n, "GS-9001", &before, 0, out, n - 1, rec, &m), OM_E_FULL);
    {
        uint8_t *b = th_dup(d, n);
        b[70] ^= 1;                                        /* the index: its CRC fails */
        CHECK(om_cpk_find(b, n, "GS-9001", &g) < 0);
        CHECK(om_cpk_put(b, n, "GS-9001", &before, 0, out, sizeof out, rec, &m) < 0);
        free(b);
    }
    free(d);
}

/* the list wraps on a fresh press (the last row's Down goes to the top, the first row's Up to the bottom); a held
 * key's repeat stops at the ends */
static void wraps(void)
{
    uint32_t n;
    uint8_t *d = th_load(FIX("tiny.cheats.pak"), &n);
    CHECK_INT(om_cpk_find(d, n, "GS-9001", &f), 1);
    om_ced_open(&e, &f, 0);
    key(OM_CK_U);
    CHECK_INT(e.row, 3);                                   /* Up on the first row: the last */
    key(OM_CK_D);
    CHECK_INT(e.row, 0);                                   /* Down on the last: the first */
    key(OM_CK_U | OM_CK_REP);
    CHECK_INT(e.row, 0);                                   /* a repeat stops at the top */
    keys(OM_CK_D | OM_CK_REP, 9);
    CHECK_INT(e.row, 3);                                   /* and at the bottom */
    CHECK_INT(e.dirty, 0);
}

int main(void)
{
    fixture_list();
    wraps();
    cap_limit();
    add_flow();
    add_code_cap();
    readonly_and_full();
    pak_rewrite(FIX("tiny.cheats.pak"), 1);
    pak_rewrite("../../tools/data/cheats/CHEATS.PAK", 1);
    pak_edits();
    printf("test_chedit: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
