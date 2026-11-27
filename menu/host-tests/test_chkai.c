/* menu/host-tests/test_chkai.c -- Kai's cheat method under CWX (menu/launcher/chkai.h) against simulated
 * memory: the place rule, the master opcode, code validation, the config layout Kai's handler reads, the 0x00 codes and
 * the displaced instruction (an LWRAM program translated into its HWRAM copy), the cap, and that no block, master or
 * code can reach VibeMenu's in-game-reset memory. A C model of Kai's in-RAM handler (cheat_codes_hdlr_ram.s:29-111,
 * read line by line) runs the built block; the real linked bytes run on the SH-2 model in sbuild check_chkai /
 * tools/tests/test_sh2kai.py. BSD-3-Clause. */
#include "th.h"
#include "chkai.h"
#include "cheatpak.h"
#include "om_fmt.h"

/* simulated RAM: HWRAM 0x06000000-0x060FFFFF and LWRAM 0x00200000-0x002FFFFF, big-endian */
static uint8_t hw[0x100000], lw[0x100000];
static uint8_t *at(uint32_t a)
{
    if (a >= 0x06000000u && a < 0x06100000u) return hw + (a - 0x06000000u);
    if (a >= 0x00200000u && a < 0x00300000u) return lw + (a - 0x00200000u);
    printf("FAIL access outside RAM %08X\n", a);
    exit(2);
}
static uint16_t rd16(uint32_t a) { return om_be16(at(a)); }
static void wr16(uint32_t a, uint16_t v) { om_put16(at(a), v); }
static const om_chk_mem mem = { rd16, wr16 };

/* stand-ins for the two Kai objects' bytes (the real ones are checked by sbuild); a recognisable fill */
static uint8_t boot[OM_CHK_BOOT], hdlr[OM_CHK_HDLR];

/* Kai's handler, as cheat_codes_hdlr_ram.s does it, reading the config at cfg in simulated RAM */
static void kai_handler(uint32_t cfg)
{
    uint32_t en = om_be32(at(cfg + 48)), n, p;
    if (en && rd16(en) != rd16(cfg + 52))
        return;
    n = rd16(cfg + 58);
    for (p = cfg + 64; n; n--, p += 8) {
        uint32_t a = om_be32(at(p));
        uint16_t v = rd16(p + 4);
        if (*at(p + 6) == 1) wr16(a, v);
        else *at(a) = (uint8_t)v;                        /* every other cached command: the low byte (M5) */
    }
}

static om_ch_file f;
static void file_reset(uint32_t master, uint16_t opc, uint32_t loc, uint16_t cache4)
{
    memset(&f, 0, sizeof f);
    f.master_addr = master;
    f.master_value = opc;
    f.location = loc;
    f.cache4 = cache4;
    f.flags = OM_CP_G_MASTER | OM_CP_G_CWX;
}
static void add_cheat(const char *name, int on, int n, const uint32_t *a, const uint16_t *v, const uint8_t *c)
{
    om_ch_cheat *ch = &f.cheat[f.ncheats++];
    int i;
    strcpy(ch->name, name);
    ch->flags = on ? OM_CH_ON : 0;
    ch->first = f.ncodes;
    ch->count = (uint16_t)n;
    for (i = 0; i < n; i++) {
        f.code[f.ncodes].addr = a[i];
        f.code[f.ncodes].value = v[i];
        f.code[f.ncodes].cmd = c[i];
        f.ncodes++;
    }
}
static void add1(const char *name, int on, uint32_t a, uint16_t v, uint8_t c) { add_cheat(name, on, 1, &a, &v, &c); }

static void place_rule(void)
{
    CHECK_INT(om_chk_place(0x06002800u), OM_RES_BASE);
    CHECK_INT(om_chk_place(0), OM_RES_BASE);
    CHECK_INT(om_chk_place(0x06002808u), OM_RES_BASE);
    CHECK_INT(om_chk_place(0x06002D44u), OM_RES_BASE);
    CHECK_INT(om_chk_place(0x060FF000u), 0x060FF000u);
    CHECK_INT(om_chk_place(0x06000B00u), 0x06000B00u);
    CHECK_INT(om_chk_place(0x0602B308u), 0x0602B308u);   /* Kai's Virtual Hydlide: 8-aligned, kept */
    CHECK_INT(om_chk_place(0x060FF002u), 0);              /* not long-word aligned */
    CHECK_INT(om_chk_place(0x06000040u), 0);              /* the vectors (and Task 27a's guard slots) */
    CHECK_INT(om_chk_place(0x060003C0u), 0);              /* SYS_EXECDMP's guard */
    CHECK_INT(om_chk_place(0x06000900u), 0);              /* the BIOS dispatcher */
    CHECK_INT(om_chk_place(0x060027F0u), 0);              /* would run into the stub */
    CHECK_INT(om_chk_place(0x06002200u), 0);              /* the IP's live code */
    CHECK_INT(om_chk_place(0x06003F00u), 0);              /* the IP area */
    CHECK_INT(om_chk_place(0x060FFF00u), 0);              /* past the end of HWRAM */
    CHECK_INT(om_chk_place(0x00280000u), 0);              /* LWRAM: the CWX tail's home, cleared by it */
    CHECK_INT(om_chk_place(0x06000C80u), 0);              /* runs into the BIOS slave stack */
    CHECK_INT(om_chk_place(0x06001800u), 0);              /* the master stack */
}

static void master_opc(void)
{
    CHECK_INT(om_chk_master_opc(0xC305), 0xC305);
    CHECK_INT(om_chk_master_opc(0xC30A), 0xC30A);
    CHECK_INT(om_chk_master_opc(0xC321), 0xC305);              /* trapa #0x21 is not vector 1 */
    CHECK_INT(om_chk_master_opc(0xFFFF), 0xC305);
    CHECK_INT(om_chk_master_opc(0x230B), 0xC305);
    CHECK_INT(om_chk_master_opc(0x0200), 0xC305);
}

static void validation(void)
{
    CHECK_INT(om_chk_code_ok(0x0607FFFFu, 0x01), OM_CHV_ODD);
    CHECK_INT(om_chk_code_ok(0x0607FFFFu, 0x03), OM_CHV_OK);   /* 8-bit: any address */
    CHECK_INT(om_chk_code_ok(0x0607FFFFu, 0x0D), OM_CHV_ODD);  /* the handler reads a word there */
    CHECK_INT(om_chk_code_ok(0x0607FFFFu, 0x00), OM_CHV_ODD);
    CHECK_INT(om_chk_code_ok(0x06002600u, 0x01), OM_CHV_BLOB);
    CHECK_INT(om_chk_code_ok(0x06002900u, 0x01), OM_CHV_BLOB);
    CHECK_INT(om_chk_code_ok(0x06000040u, 0x01), OM_CHV_GUARD);
    CHECK_INT(om_chk_code_ok(0x060003C0u, 0x03), OM_CHV_GUARD);
    CHECK_INT(om_chk_code_ok(0x06000914u, 0x01), OM_CHV_GUARD);
    CHECK_INT(om_chk_code_ok(0x05000000u, 0x01), OM_CHV_RANGE);
    CHECK_INT(om_chk_code_ok(0x06100000u, 0x01), OM_CHV_RANGE);
    CHECK_INT(om_chk_code_ok(0x26070000u, 0x01), OM_CHV_RANGE);   /* the cache-through alias is refused too */
    CHECK_INT(om_chk_code_ok(0x06070000u, 0x08), OM_CHV_CMD);
    CHECK_INT(om_chk_code_ok(0x06070000u, 0x02), OM_CHV_CMD);
    CHECK_INT(om_chk_code_ok(0x0020126Au, 0x01), OM_CHV_OK);      /* Kai's LWRAM codes (10 in the shipped table) */
    CHECK_INT(om_chk_code_ok(0x06004000u, 0x01), OM_CHV_OK);
    CHECK_INT(om_chk_code_ok(0x060FFFFEu, 0x01), OM_CHV_OK);
    CHECK(strlen(om_chk_why(OM_CHV_BLOB)) <= 38 && strlen(om_chk_why(OM_CHV_RANGE)) <= 38);
}

/* the layout Kai's handler reads (M5: enable address +48, value +52, count +58, codes from +64, handler after) */
static void layout(void)
{
    static uint8_t img[OM_CHK_MAX];
    om_chk_plan p;
    uint32_t n, i;
    file_reset(0x06000914u, 0xC305, 0x06002800u, 39);
    add1("BOOT", 1, 0x06050000u, 0x1234, 0x00);
    {
        uint32_t a[3] = { 0x06060000u, 0x06060010u, 0x06061000u };
        uint16_t v[3] = { 0x0380, 0x0063, 0x0001 };
        uint8_t c[3] = { 0x01, 0x03, 0x0D };
        add_cheat("TWO", 1, 3, a, v, c);
    }
    add1("OFF", 0, 0x06062000u, 0x0001, 0x01);
    add1("ENABLE2", 1, 0x06063000u, 0x0002, 0x0D);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(p.place, OM_RES_BASE);
    CHECK_INT(p.ncache, 2);                               /* no 0x00 / 0x0D in the cache */
    CHECK_INT(p.nboot, 1);
    CHECK_INT(p.en_addr, 0x06063000u);                    /* the LAST enable code */
    CHECK_INT(p.en_val, 2);
    CHECK_INT(p.vec, 5);
    CHECK_INT(p.len, 64 + 16 + 96);
    p.saved = 0x224B;
    n = om_chk_build(img, sizeof img, &p, boot, sizeof boot, hdlr, sizeof hdlr);
    CHECK_INT(n, 64 + 16 + 96);
    CHECK(!memcmp(img, boot, 32));                        /* the bootstrap's header half */
    CHECK_INT(om_be16(img + 32), 0x224B);                 /* the displaced instruction, in the bootstrap's nop slot */
    CHECK(!memcmp(img + 34, boot + 34, 6));               /* nop, rte, nop */
    CHECK_INT(om_be32(img + 40), OM_RES_BASE + 64 + 16);  /* the handler, right after the codes */
    CHECK_INT(om_be32(img + 44), OM_RES_BASE);
    CHECK_INT(om_be32(img + 48), 0x06063000u);
    CHECK_INT(om_be16(img + 52), 2);
    CHECK_INT(om_be16(img + 58), 2);
    CHECK_INT(om_be32(img + 64), 0x06060000u);
    CHECK_INT(om_be16(img + 68), 0x0380);
    CHECK_INT(img[70], 0x01);
    CHECK_INT(om_be32(img + 72), 0x06060010u);
    CHECK_INT(img[78], 0x03);
    CHECK(!memcmp(img + 80, hdlr, sizeof hdlr));
    for (i = 54; i < 58; i++) CHECK_INT(img[i], 0);
    CHECK_INT(om_chk_build(img, n - 1, &p, boot, sizeof boot, hdlr, sizeof hdlr), 0);   /* too small a cap */
    CHECK_INT(om_chk_build(img, sizeof img, &p, boot, 38, hdlr, sizeof hdlr), 0);       /* not Kai's bootstrap */
}

/* the whole arming on simulated RAM: Golden Axe: The Duel's "Unl Energy for P1" (Kai 2024: 06078A5C:0080:01, master
 * 06000914:C305) as a user-added cheat on a record with no master: the energy holds while the cheat is on and drains
 * while it is off -- the hardware row's own check, on the model */
static int ga_run(int on)
{
    static uint8_t img[OM_CHK_MAX];
    om_chk_plan p;
    uint32_t n, frame;
    memset(hw, 0, sizeof hw);
    wr16(0x06000914u, 0x224B);                            /* the dispatcher's `or r4,r2` (plan Task 27a M1) */
    memset(&f, 0, sizeof f);                              /* the game Kai's 2016 table lacks */
    om_chk_default_master(&f);
    add1("P1 ENERGY", on, 0x06078A5Cu, 0x0080, 0x01);
    if (om_chk_plan_make(&f, &p) != OM_CHS_ARMED) {
        wr16(0x06078A5Cu, 0x0080);                        /* nothing armed: the game alone */
        for (frame = 0; frame < 120; frame++)
            wr16(0x06078A5Cu, (uint16_t)(rd16(0x06078A5Cu) - 1));
        return rd16(0x06078A5Cu) == 0x0080 ? -1 : rd16(0x06078A5Cu);
    }
    n = om_chk_arm(&p, &mem, 0x06004000u, 0x06004000u, 0x80000u, img, sizeof img, boot, sizeof boot, hdlr,
                   sizeof hdlr);
    if (!n || p.saved != 0x224B || p.place != OM_RES_BASE || p.master != 0x06000914u || p.opc != 0xC305)
        return -2;
    memcpy(at(p.place), img, n);                          /* the hook's copy */
    wr16(p.master, p.opc);                                /* then the master */
    wr16(0x06078A5Cu, 0x0080);                            /* full energy */
    for (frame = 0; frame < 120; frame++) {
        wr16(0x06078A5Cu, (uint16_t)(rd16(0x06078A5Cu) - 1));   /* the game hurts P1 */
        if (rd16(p.master) == 0xC305)                     /* the dispatcher's trapa #5 -> the bootstrap */
            kai_handler(om_be32(at(p.place + OM_CHK_P_CFG)));
    }
    return rd16(0x06078A5Cu);
}

static void arm_ga(void)
{
    om_chk_plan p;
    CHECK_INT(ga_run(1), 0x0080);                         /* held */
    CHECK_INT(ga_run(0), 0x0080 - 120);                   /* off: nothing armed, it drains ... */
    memset(&f, 0, sizeof f);
    om_chk_default_master(&f);
    add1("P1 ENERGY", 0, 0x06078A5Cu, 0x0080, 0x01);
    add1("OTHER", 1, 0x06070000u, 0x0001, 0x03);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(p.ncache, 1);
    CHECK_INT(p.cache[0].addr, 0x06070000u);              /* ... and with another cheat on, P1's is not in the cache */
    CHECK_INT(f.master_addr, OM_CHK_DEF_MASTER);
    CHECK_INT(f.flags & (OM_CP_G_MASTER | OM_CP_G_CWX), OM_CP_G_MASTER | OM_CP_G_CWX);
}

/* the enable code: the codes run only while the word there equals the value */
static void enable_code(void)
{
    static uint8_t img[OM_CHK_MAX];
    om_chk_plan p;
    uint32_t n;
    memset(hw, 0, sizeof hw);
    file_reset(0x06000914u, 0xC305, 0x060FF000u, 125);
    {
        uint32_t a[2] = { 0x06070000u, 0x06070100u };
        uint16_t v[2] = { 0x0007, 0xBEEF };
        uint8_t c[2] = { 0x0D, 0x01 };
        add_cheat("COND", 1, 2, a, v, c);
    }
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    n = om_chk_arm(&p, &mem, 0x06004000u, 0x06004000u, 0x1000u, img, sizeof img, boot, sizeof boot, hdlr, sizeof hdlr);
    CHECK_INT(p.place, 0x060FF000u);
    memcpy(at(p.place), img, n);
    wr16(0x06070000u, 0x0006);
    kai_handler(p.place);
    CHECK_INT(rd16(0x06070100u), 0);
    wr16(0x06070000u, 0x0007);
    kai_handler(p.place);
    CHECK_INT(rd16(0x06070100u), 0xBEEF);
}

/* an LWRAM program (read to 0x06004000, copied to exec by the tail): 0x00 codes and the displaced instruction go
 * through the HWRAM copy, the master poke (in the hook, after the copy) uses the real address */
static void lwram_program(void)
{
    static uint8_t img[OM_CHK_MAX];
    om_chk_plan p;
    memset(hw, 0, sizeof hw);
    memset(lw, 0, sizeof lw);
    file_reset(0x00201000u, 0xC305, 0x060FF000u, 125);    /* a master in the game's own code (Astal-like) */
    add1("BOOT", 1, 0x00202000u, 0x5555, 0x00);
    add1("RUN", 1, 0x06070000u, 0x0001, 0x01);
    wr16(0x06004000u + 0x1000u, 0x6E03);                  /* the instruction at exec + 0x1000, in the HWRAM copy */
    CHECK_INT(om_chk_xlat(0x00202000u, 0x00200000u, 0x06004000u, 0x10000u), 0x06006000u);
    CHECK_INT(om_chk_xlat(0x00212000u, 0x00200000u, 0x06004000u, 0x10000u), 0x00212000u);   /* past the file */
    CHECK_INT(om_chk_xlat(0x06070000u, 0x06004000u, 0x06004000u, 0x10000u), 0x06070000u);   /* read in place */
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK(om_chk_arm(&p, &mem, 0x00200000u, 0x06004000u, 0x10000u, img, sizeof img, boot, sizeof boot, hdlr,
                     sizeof hdlr) > 0);
    CHECK_INT(rd16(0x06006000u), 0x5555);                 /* the 0x00 code, in the copy the tail moves */
    CHECK_INT(rd16(0x00202000u), 0);                      /* not at exec (the tail's copy would erase it) */
    CHECK_INT(p.saved, 0x6E03);
    CHECK_INT(p.master, 0x00201000u);
    CHECK_INT(om_be16(img + OM_CHK_SLOT), 0x6E03);
}

/* what may run moved (review of 27b, I3) and the 0x00 codes that never reach the game (I1) */
static void arm_refusals(void)
{
    static uint8_t img[OM_CHK_MAX];
    static const uint16_t ok[] = { 0x224B, 0x6E03, 0x2122, 0x7004, 0xE001, 0x5123, 0x1234, 0x0124, 0xC901, 0x8012,
                                   0x312C, 0x4F1F & 0xF0FF, 0x0009 };
    static const uint16_t bad[] = { 0x9001, 0xD001, 0xA000, 0xB000, 0x8901, 0x8B01, 0x8D01, 0x8F01, 0x412B, 0x410B,
                                    0x000B, 0x002B, 0x0123, 0x0103, 0x6F03, 0x2F06, 0x4F22, 0x3120, 0x8800, 0xC801,
                                    0x0029, 0x0008, 0x0018, 0x4100, 0x4124, 0x400E, 0x0002, 0xC700,
                                    0xC305, 0x6A0A & 0xF0FF, 0x312E, 0xF000, 0x2128 };
    uint32_t i;
    om_chk_plan p;
    for (i = 0; i < sizeof ok / sizeof ok[0]; i++) CHECK(om_chk_op_safe(ok[i], 0x0009));
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK(!om_chk_op_safe(bad[i], 0x0009));
    CHECK(!om_chk_op_safe(0x224B, 0xA123));               /* a bra's delay slot */
    CHECK(!om_chk_op_safe(0x224B, 0x000B));               /* rts's */
    CHECK(!om_chk_op_safe(0x224B, 0x8D02));               /* bt/s's */
    CHECK(om_chk_op_safe(0x224B, 0x8902));                /* bt has none */
    /* an unsafe master instruction: nothing armed, nothing written */
    memset(hw, 0, sizeof hw);
    file_reset(0x06000914u, 0xC305, 0, 8);
    add1("BOOT", 1, 0x06070000u, 0x1234, 0x00);
    wr16(0x06000914u, 0xD001);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(om_chk_arm(&p, &mem, 0x06004000u, 0x06004000u, 0x1000u, img, sizeof img, boot, sizeof boot, hdlr,
                         sizeof hdlr), 0);
    CHECK_INT(p.status, OM_CHS_NOTSUP);
    CHECK_INT(rd16(0x06070000u), 0);
    /* Kai's place inside the program just read: refused */
    memset(hw, 0, sizeof hw);
    file_reset(0x06000914u, 0xC305, 0x060FF000u, 125);
    add1("RUN", 1, 0x06070000u, 0x0001, 0x01);
    wr16(0x06000914u, 0x224B);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(om_chk_arm(&p, &mem, 0x06004000u, 0x06004000u, 0xFC000u, img, sizeof img, boot, sizeof boot, hdlr,
                         sizeof hdlr), 0);
    CHECK_INT(p.status, OM_CHS_NOTSUP);
    /* an HWRAM program: a 0x00 code in LWRAM (the launcher, until the tail clears it) is dropped, HWRAM's is written */
    memset(hw, 0, sizeof hw);
    memset(lw, 0, sizeof lw);
    file_reset(0x06000914u, 0xC305, 0, 8);
    add1("LW", 1, 0x00202000u, 0x5555, 0x00);
    add1("HW", 1, 0x06070000u, 0x6666, 0x00);
    wr16(0x06000914u, 0x224B);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK(om_chk_arm(&p, &mem, 0x06004000u, 0x06004000u, 0x1000u, img, sizeof img, boot, sizeof boot, hdlr,
                     sizeof hdlr) > 0);
    CHECK_INT(rd16(0x00202000u), 0);
    CHECK_INT(rd16(0x06070000u), 0x6666);
    /* an LWRAM program: an HWRAM 0x00 code inside its temporary copy is dropped */
    memset(hw, 0, sizeof hw);
    memset(lw, 0, sizeof lw);
    file_reset(0x06000914u, 0xC305, 0, 8);
    add1("COPY", 1, 0x06004800u, 0x7777, 0x00);
    wr16(0x06000914u, 0x224B);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK(om_chk_arm(&p, &mem, 0x00200000u, 0x06004000u, 0x10000u, img, sizeof img, boot, sizeof boot, hdlr,
                     sizeof hdlr) > 0);
    CHECK_INT(rd16(0x06004800u), 0);
}

/* the cap: 30 codes, or Kai's own cache size at Kai's own place; a cheat that does not fit is left out whole */
static void cap(void)
{
    om_chk_plan p;
    uint32_t a[4] = { 0x06070000u, 0x06070002u, 0x06070004u, 0x06070006u };
    uint16_t v[4] = { 1, 2, 3, 4 };
    uint8_t c[4] = { 1, 1, 1, 1 };
    int i;
    file_reset(0x06000914u, 0xC305, 0x06002800u, 1);      /* Kai cache=1 at 0x06002800: moved to ours, 30 */
    CHECK_INT(om_chk_cap(&f), 30);
    file_reset(0x06000914u, 0xC305, 0x060FF000u, 1);      /* Kai cache=1 at Kai's own place: 4 */
    CHECK_INT(om_chk_cap(&f), 4);
    add_cheat("THREE", 1, 3, a, v, c);
    add_cheat("TWO", 1, 2, a, v, c);
    add_cheat("ONE", 1, 1, a + 3, v, c);
    CHECK_INT(om_chk_active(&f), 6);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(p.ncache, 3);                               /* THREE fits, TWO does not; Kai stops there */
    file_reset(0x06000914u, 0xC305, 0x060FF000u, 125);
    CHECK_INT(om_chk_cap(&f), 30);
    for (i = 0; i < 8; i++) add_cheat("FOUR", 1, 4, a, v, c);
    CHECK_INT(om_chk_active(&f), 32);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(p.ncache, 28);
    CHECK(p.len <= OM_CHK_MAX);
}

static void refusals(void)
{
    om_chk_plan p;
    file_reset(0x06000914u, 0xC305, 0x06002800u, 39);
    f.flags |= OM_CP_G_JHL;
    add1("X", 1, 0x06070000u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_JHL_ONLY);
    file_reset(0x06000914u, 0xC305, 0x06002800u, 39);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NONE);     /* no cheat on */
    f.flags = 0;
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NOREC);    /* no master */
    file_reset(0x06000915u, 0xC305, 0x06002800u, 39);     /* odd master */
    add1("X", 1, 0x06070000u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NOTSUP);
    file_reset(0x06002900u, 0xC305, 0x06002800u, 39);     /* a master inside the IGR stub */
    add1("X", 1, 0x06070000u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NOTSUP);
    file_reset(0x060003C4u, 0xC305, 0x06002800u, 39);     /* inside SYS_EXECDMP's guard */
    add1("X", 1, 0x06070000u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NOTSUP);
    file_reset(0x06000914u, 0xC305, 0x060FF004u + 2u, 39); /* an unusable location */
    add1("X", 1, 0x06070000u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NOTSUP);
    file_reset(0x06000914u, 0xC305, 0x060FF000u, 125);    /* a code into the block itself */
    add1("SELF", 1, 0x060FF010u, 1, 1);
    add1("OK", 1, 0x06070000u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_ARMED);
    CHECK_INT(p.ncache, 1);
    CHECK_INT(p.cache[0].addr, 0x06070000u);
    file_reset(0x06000914u, 0xC305, 0x060FF000u, 125);    /* a refused code drops only its own cheat */
    add1("GUARD", 1, 0x060003C0u, 1, 1);
    CHECK_INT(om_chk_plan_make(&f, &p), OM_CHS_NONE);
}

/* no overlap with the in-game reset: every place the rule can return, with its largest block, stays clear of the
 * stub home, SYS_EXECDMP's guard and cell, the security-code cell, the vector table, the BIOS dispatcher and the IP's
 * live bytes; and no master the plan accepts is inside any of them */
static int hits(uint32_t a, uint32_t n)
{
    static const uint32_t own[][2] = {
        { 0x06002800u, 0x06002D48u },   /* IGR stub (igr.h) */
        { 0x060003C0u, 0x06000400u },   /* SYS_EXECDMP's guard (igrguard.c) */
        { 0x0600026Cu, 0x06000270u },   /* SYS_EXECDMP's cell */
        { 0x06002270u, 0x06002274u },   /* the security code's read cell (the hook) */
        { 0x06000000u, 0x06000400u },   /* VBR's vector table (vector 5 at +0x14) and the BIOS cells */
        { 0x06000900u, 0x06000B00u },   /* the BIOS dispatcher and its table */
        { 0x06002000u, 0x06002580u },   /* the IP's live code */
        { 0x06002D48u, 0x06004000u },   /* the IP's live cells and the rest of the IP */
        { 0x002FF000u, 0x00300000u },   /* the CWX tail */
    };
    uint32_t i;
    for (i = 0; i < sizeof own / sizeof own[0]; i++)
        if (a < own[i][1] && a + n > own[i][0])
            return 1;
    return 0;
}

static void no_overlap(void)
{
    uint32_t a, bad = 0, places = 0, masters = 0;
    om_chk_plan p;
    for (a = 0x06000000u; a < 0x06100000u; a += 2u) {
        uint32_t pl = om_chk_place(a);
        if (pl) {
            places++;
            if (hits(pl, OM_CHK_MAX)) bad++;
        }
    }
    CHECK_INT(bad, 0);
    CHECK(places > 200000u);                              /* the denominator: every even HWRAM location tried */
    bad = 0;
    for (a = 0x06000000u; a < 0x06006000u; a += 2u) {     /* the low 24 KB, where every VibeMenu cell lives */
        file_reset(a, 0xC305, 0x060FF000u, 125);
        add1("X", 1, 0x06070000u, 1, 1);
        if (om_chk_plan_make(&f, &p) == OM_CHS_ARMED) {
            masters++;
            if (hits(a, 2) && !(a >= 0x06000900u && a < 0x06000B00u))   /* the dispatcher is Kai's master home */
                bad++;
        }
    }
    CHECK_INT(bad, 0);
    CHECK(masters > 1000u);
    bad = 0;
    for (a = 0x06000000u; a < 0x06006000u; a += 1u) {     /* every code address the validator accepts */
        uint8_t c;
        for (c = 0; c < 0x10; c++)
            if (!om_chk_code_ok(a, c) && (hits(a, c == 3 ? 1 : 2) || (a >= 0x06000900u && a < 0x06000B00u)))
                bad++;
    }
    CHECK_INT(bad, 0);
    CHECK_INT(om_chk_place(0x06002800u) + OM_CHK_MAX <= OM_RES_END, 1);   /* our block ends below the stub */
}

int main(void)
{
    uint32_t i;
    for (i = 0; i < sizeof boot; i++) boot[i] = (uint8_t)(0xB0 + i);
    for (i = 0; i < sizeof hdlr; i++) hdlr[i] = (uint8_t)(0x40 + i);
    place_rule();
    master_opc();
    validation();
    layout();
    arm_ga();
    arm_refusals();
    enable_code();
    lwram_program();
    cap();
    refusals();
    no_overlap();
    printf("test_chkai: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
