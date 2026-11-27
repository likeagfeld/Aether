#include "th.h"
#include "handoff.h"
#include "omlayout.h"
#include "settings.h"
#include <stddef.h>
#include "cases.inc"
#include "tree.h"
#define OM_MAX_DEPTH_FOR_TEST OM_MAX_DEPTH

int main(void)
{
    om_handoff h, g;
    om_lnch_info li;
    uint8_t hdr[OM_LNCH_HDR];
    char longname[300];
    om_launch_opts o;
    uint32_t i, bad;
    memset(&o, 0, sizeof o);
    o.loader = OM_LDR_CWX; o.igr = 1; o.slot = 3;
    memcpy(o.gid, "T-23001H@U", 11);
    memcpy(o.ckey, "T-23001H", 9);
    /* version 2; 3, with the cheat key. The layout is shared by two binaries built apart
     * (menu, LAUNCHER.BIN): pin it. */
    CHECK_INT(OM_HANDOFF_VERSION, 5);                  /* 4: the cover and theme colors; 5: a multi-disc set's discs */
    CHECK_INT(sizeof(om_handoff), 1996);
    CHECK(sizeof(om_handoff) <= 0x800u);               /* below the L1 harness's words (l1gate.h, l1_lbtest) */
    CHECK_INT(offsetof(om_handoff, area_code), 528);
    CHECK_INT(offsetof(om_handoff, loader), 529);
    CHECK_INT(offsetof(om_handoff, savetool), 535);
    CHECK_INT(offsetof(om_handoff, gid), 536);
    CHECK_INT(sizeof h.gid, OM_GID_MAX);
    CHECK_INT(offsetof(om_handoff, ckey), 556);
    CHECK_INT(sizeof h.ckey, 17);
    CHECK_INT(offsetof(om_handoff, art_w), 573);
    CHECK_INT(offsetof(om_handoff, sty), 576);
    CHECK_INT(offsetof(om_handoff, art), 584);
    CHECK_INT(offsetof(om_handoff, ndisc), 588);
    CHECK_INT(offsetof(om_handoff, cur), 589);
    CHECK_INT(offsetof(om_handoff, discs), 590);
    CHECK_INT(offsetof(om_handoff, crc), 1992);
    CHECK_INT(om_handoff_fill2(&h, "/RPG/Grandia", "disc1.cue", 1, 0, &o), OM_OK);
    CHECK_INT(om_handoff_check(&h), OM_OK);
    CHECK_STR(h.dir, "/RPG/Grandia");
    CHECK_STR(h.image, "disc1.cue");
    CHECK_INT(h.version, 5); CHECK_INT(h.ndisc, 0); CHECK_INT(h.cur, 0);
    CHECK(om_handoff_disc(&h, 1) == NULL); CHECK_INT(h.loader, OM_LDR_CWX); CHECK_INT(h.igr, 1); CHECK_INT(h.slot, 3);
    CHECK_INT(h.saves, 0); CHECK_INT(h.cheats, 0); CHECK_INT(h.netlink, 0); CHECK_INT(h.savetool, 0);
    CHECK_STR(h.gid, "T-23001H@U");
    CHECK_STR(h.ckey, "T-23001H");
    for (i = 0, bad = 0; i < offsetof(om_handoff, crc); i++) {   /* every byte before the CRC is covered */
        g = h;
        ((uint8_t *)&g)[i] ^= 0x40;
        if (om_handoff_check(&g) == OM_OK) bad++;
    }
    CHECK_INT(bad, 0);
    g = h; g.dir[1] ^= 1;
    CHECK_INT(om_handoff_check(&g), OM_E_CRC);
    g = h; g.magic[0] = 'X';
    CHECK_INT(om_handoff_check(&g), OM_E_MAGIC);
    g = h; g.version = 1; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_VERSION);              /* a stale menu or LAUNCHER.BIN: named, not misread */
    g = h; g.loader = OM_LDR_AUTO; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);                /* resolved, never AUTO */
    g = h; g.loader = OM_LDR_COUNT; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);
    g = h; g.slot = 10; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);
    g = h; memset(g.gid, 'A', sizeof g.gid); g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);                /* an unterminated id */
    g = h; memset(g.ckey, 'A', sizeof g.ckey); g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);                /* an unterminated cheat key */
    g = h; g.version = 4; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_VERSION);              /* a 0.1 menu or LAUNCHER.BIN from before the lid swap */
    /* v5: the set's discs */
    g = h; g.ndisc = 2; g.cur = 2; strcpy(g.discs, "A (Disc 1)/a.cue"); strcpy(g.discs + 17, "A (Disc 2)/a.cue");
    g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_OK);
    CHECK_STR(om_handoff_disc(&g, 1), "A (Disc 1)/a.cue");
    CHECK_STR(om_handoff_disc(&g, 2), "A (Disc 2)/a.cue");
    CHECK(om_handoff_disc(&g, 3) == NULL && om_handoff_disc(&g, 0) == NULL);
    {
        om_handoff k;
        k = g; k.cur = 3; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* cur past ndisc */
        k = g; k.cur = 0; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);
        k = g; k.ndisc = 1; k.cur = 1; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* one disc is ndisc 0 (out.desc), never 1 */
        k = g; k.ndisc = 10; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* one digit: out1..out9 */
        k = g; k.ndisc = 3; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* a third path that is empty */
        k = g; k.discs[40] = 'x'; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* bytes after the last path */
        k = h; k.cur = 1; k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* cur without discs */
        k = g; memset(k.discs, 'a', 300); k.discs[300] = 0; strcpy(k.discs + 301, "b");
        k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* a path over 255 B */
        k = g; memset(k.discs, 'a', OM_HO_DISC_POOL); k.crc = om_crc32(0, &k, (uint32_t)offsetof(om_handoff, crc));
        CHECK_INT(om_handoff_check(&k), OM_E_FIELD);            /* no terminator in the pool */
    }
    g = h; g.version = 2; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_VERSION);              /* a v2 menu with a v3 launcher (and back) */
    /* saves is 0 off, 1 on (Move), 2 on (Share); anything else is refused */
    g = h; g.saves = 2; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_OK);
    g = h; g.saves = 3; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);
    /* a release launcher refuses a flag it does not know -- above all the L1
     * harness's BRAM test (OM_HO_TEST_BRAM), which only an OM_L1_HARNESS build knows; these host tests are a
     * release build */
    g = h; g.flags = OM_HO_TEST_BRAM; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);
    g = h; g.flags = 0x80000000u; g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_E_FIELD);
    g = h; g.flags = OM_HO_TEST_NO_SATIATOR | OM_HO_TEST_ERRSCREEN | OM_HO_SPIKE | OM_HO_NO_IGR | OM_HO_IMPORT | OM_HO_TIDY
            | OM_HO_CHEATS;
    g.crc = om_crc32(0, &g, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(&g), OM_OK);                     /* every flag a release build knows */
    o.loader = OM_LDR_AUTO;
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, 0, &o), OM_E_FIELD);
    o.loader = OM_LDR_JHL; o.slot = 10;
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, 0, &o), OM_E_FIELD);
    o.slot = 0; o.saves = 3;
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, 0, &o), OM_E_FIELD);   /* saves 0..2 */
    o.saves = 0;
    o.slot = 0; memset(o.gid, 'A', sizeof o.gid);
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, 0, &o), OM_E_FIELD);   /* an unterminated id */
    o.gid[0] = 0;                                               /* no IP info (exit A, the fake card): an empty id */
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, 0, &o), OM_OK);
    CHECK_INT(om_handoff_check(&h), OM_OK);
    CHECK_STR(h.gid, ""); CHECK_INT(h.loader, OM_LDR_JHL);
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, OM_HO_NO_IGR, &o), OM_OK);
    CHECK_INT(h.igr, 0);                                        /* Settings > Soft reset Off wins over opts.igr */
    CHECK_INT(om_handoff_fill2(&h, "/", "x.iso", 1, 0, NULL), OM_E_FIELD);
    memset(longname, 'a', 299);
    longname[299] = 0;
    CHECK_INT(om_handoff_fill2(&h, longname, "x.iso", 1, 0, &o), OM_E_FIELD);
    /* LH_HDR was made by tools/omsat/lnchhdr.py: a cross-implementation check */
    CHECK_INT(om_lnch_parse(LH_HDR, OM_LNCH_HDR + LH_BODY_LEN, &li), OM_OK);
    CHECK_INT(li.body_size, LH_BODY_LEN);
    CHECK_INT(li.body_crc, om_crc32(0, LH_BODY, LH_BODY_LEN));
    CHECK_INT(om_lnch_parse(LH_HDR, OM_LNCH_HDR + LH_BODY_LEN + 1, &li), OM_E_RANGE);
    memcpy(hdr, LH_HDR, sizeof hdr);
    hdr[0x13] ^= 1;
    CHECK_INT(om_lnch_parse(hdr, OM_LNCH_HDR + LH_BODY_LEN, &li), OM_E_CRC);
    memcpy(hdr, LH_HDR, sizeof hdr);
    om_put32(hdr + 0x0C, 0x06004000u);
    om_put32(hdr + 0x18, om_crc32(0, hdr, 0x18));
    CHECK_INT(om_lnch_parse(hdr, OM_LNCH_HDR + LH_BODY_LEN, &li), OM_E_FIELD);
    om_lnch_make(hdr, LH_BODY_LEN, om_crc32(0, LH_BODY, LH_BODY_LEN));
    CHECK(memcmp(hdr, LH_HDR, OM_LNCH_HDR) == 0);
    {   /* 2026-11-25: the return block -- unarmed at the hand-off, armed by the tool, read once by the menu */
        static om_retblk r, out;
        static uint16_t snd[sizeof(om_retblk) / 2u];
        CHECK(OM_LW_RETURN >= OM_LW_HANDOFF + 0x820u);             /* past the L1 harness's words (l1gate.h) */
        CHECK(OM_LW_RETURN + sizeof r <= OM_LW_SNAPSHOT);
        CHECK((OM_SND_RETURN & 0x7FFFFu) >= 0x40000u);              /* above the menu's audio regions (audio.c) */
        CHECK((OM_SND_RETURN & 0x7FFFFu) + sizeof r <= 0x78000u);   /* below the DSP ring (RBP_PARK) */
        CHECK(OM_RET_LOCS >= OM_MAX_DEPTH_FOR_TEST + 3u);
        CHECK_INT(sizeof(om_ret_loc), 4);
        memset(&r, 0, sizeof r);
        r.ov_kind = 3; r.ov_row = 5; r.entry = 12; r.depth = 2; r.cur = 4;
        memcpy(r.key, "T-23001H", 9);
        om_ret_seal(&r);
        CHECK_INT(om_ret_ok(&r), 1);
        CHECK_INT(om_ret_take(&r, &out), 0);                       /* unarmed: an ordinary start, cleared */
        CHECK(!memcmp(&out, &(om_retblk){0}, sizeof out));
        CHECK_INT(om_ret_ok(&r), 0);                               /* read once */
        CHECK_INT(om_ret_arm(&r), 0);                              /* nothing to arm: no block */
        r.ov_kind = 3; r.ov_row = 5; r.entry = 12; r.depth = 2; r.cur = 4;
        memcpy(r.key, "T-23001H", 9);
        om_ret_seal(&r);
        CHECK_INT(om_ret_arm(&r), 1);
        CHECK_INT(r.armed, 1);
        om_ret_copy16(snd, &r);                                    /* to sound RAM and back, word by word */
        memset(&r, 0, sizeof r);
        om_ret_copy16(&r, snd);
        om_ret_copy16(snd, 0);
        CHECK_INT(snd[0], 0); CHECK_INT(snd[sizeof snd / 2u - 1u], 0);
        CHECK_INT(om_ret_take(&r, &out), 1);
        CHECK_INT(out.ov_row, 5); CHECK_INT(out.entry, 12); CHECK_INT(out.depth, 2); CHECK_STR(out.key, "T-23001H");
        CHECK_INT(om_ret_take(&r, &out), 0);                       /* a second start: ordinary */
        om_ret_seal(&r);                                           /* armed but corrupt: ordinary */
        CHECK_INT(om_ret_arm(&r), 1);
        r.cur ^= 1;
        CHECK_INT(om_ret_take(&r, &out), 0);
        om_ret_seal(&r);                                           /* a depth past the locations: refused */
        r.depth = OM_RET_LOCS;
        om_ret_seal(&r);
        CHECK_INT(om_ret_ok(&r), 0);
        r.depth = 0;
        memset(r.key, 'K', sizeof r.key);                          /* an unterminated key: refused */
        om_ret_seal(&r);
        CHECK_INT(om_ret_ok(&r), 0);
    }
    return th_done("test_handoff");
}
