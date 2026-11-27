/* menu/host-tests/test_pcheat.c -- the Cheats popup's file side (menu/pop/pcheat.c) over fake_satiator.c: one record
 * opened through the index (never the whole file), a toggle saved with every other record unchanged, a file at the
 * 114,688 B cap saved and refused past it, a file that does not read opened read only and never written, no file at all
 * (a new record, a new file), and a save that never leaves a short file. BSD-3-Clause. */
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "th.h"
#include "cheatpak.h"
#include "om_fmt.h"
#include "cheats.h"
#include "pcheat.h"
#include "sd.h"

static char root[] = "/tmp/omsat_pcheat_XXXXXX";
static uint32_t arena32[OM_PC_ARENA / 4 + 4];
static om_ch_file f, g;
static om_pc pc;
static char msg[64];

static void put_file(const uint8_t *d, uint32_t n)
{
    char p[600];
    FILE *fp;
    snprintf(p, sizeof p, "%s/aether/CHEATS.PAK", root);
    fp = fopen(p, "wb");
    fwrite(d, 1, n, fp);
    fclose(fp);
}
static uint8_t *get_file(uint32_t *n)
{
    char p[600];
    FILE *fp;
    uint8_t *b;
    long k;
    snprintf(p, sizeof p, "%s/aether/CHEATS.PAK", root);
    if (!(fp = fopen(p, "rb"))) { *n = 0; return NULL; }
    fseek(fp, 0, SEEK_END); k = ftell(fp); fseek(fp, 0, SEEK_SET);
    b = malloc((size_t)k + 1);
    *n = (uint32_t)fread(b, 1, (size_t)k, fp);
    fclose(fp);
    return b;
}
static void rm_file(void)
{
    char p[600];
    snprintf(p, sizeof p, "%s/aether/CHEATS.PAK", root);
    unlink(p);
}

/* a near-largest record: 64 cheats, long names, 256 codes */
static void big_record(om_ch_file *r, uint32_t seed)
{
    uint32_t i, k = 0;
    memset(r, 0, sizeof *r);
    for (i = 0; i < OM_CH_MAX_CHEATS; i++) {
        om_ch_cheat *c = &r->cheat[r->ncheats++];
        snprintf(c->name, sizeof c->name, "CHEAT %02u OF A LONG NAME %u", i, seed % 1000u);
        c->first = (uint16_t)k;
        c->count = 4;
        for (; k < c->first + 4u; k++) {
            r->code[k].addr = 0x06010000u + 2u * (k + seed);
            r->code[k].value = (uint16_t)(k * 3u + seed);
            r->code[k].cmd = 1;
        }
    }
    r->ncodes = (uint16_t)k;
}

int main(void)
{
    uint32_t n0, n1, len, i;
    uint8_t *fixture = th_load(FIX("tiny.cheats.pak"), &n0), *before, *after, *big, *out;
    static uint8_t rec[OM_CP_MAX_REC];
    char p[600], key[17];
    int rc;

    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root);
    mkdir(p, 0755);
    CHECK(OM_PC_ARENA <= 262068u);                                /* the menu's lent card-scan work area */

    /* one record, toggled and saved: the other games' records are the same bytes */
    put_file(fixture, n0);
    CHECK_INT(om_pc_open(&pc, "GS-9001", &f, (uint8_t *)arena32, sizeof arena32), 0);
    CHECK_STR(f.cheat[2].name, "MAX MONEY");
    {
        uint16_t row = 0;
        while (row < f.ncheats && (f.cheat[row].flags & OM_CH_HEADER)) row++;
        pc.ed.row = row;
        i = f.cheat[row].flags & OM_CH_ON;
        CHECK_INT(om_ced_key(&pc.ed, OM_CK_A), 0);                   /* A: on/off */
        CHECK_INT(f.cheat[row].flags & OM_CH_ON, i ^ OM_CH_ON);
        CHECK(pc.ed.dirty);
        CHECK_INT(om_ced_key(&pc.ed, OM_CK_B), 1);                   /* B on the list: done */
        CHECK_INT(om_pc_save(&pc, NULL, msg, sizeof msg), 0);
        CHECK_STR(msg, "Saved");
        after = get_file(&n1);
        CHECK(after != NULL);
        CHECK_INT(om_cpk_find(after, n1, "GS-9001", &g), 1);
        CHECK_INT(g.cheat[row].flags & OM_CH_ON, i ^ OM_CH_ON);
        CHECK_INT(om_cpk_find(after, n1, "T-8113H", &g), 1);
        CHECK_INT(om_cpk_find(fixture, n0, "T-8113H", &f), 1);
        CHECK_INT(memcmp(&f, &g, sizeof f), 0);
        /* the Launch Options Cheats row (folders.c pg_cheat_line, after OM_POP_SAVED) reads the count from the file
         * just written: one more on (or off) than before the popup */
        {
            uint32_t on0 = 0, n0c = 0, on1 = 0, n1c = 0;
            uint32_t k;
            CHECK_INT(om_cpk_find(fixture, n0, "GS-9001", &g), 1);
            for (k = 0; k < g.ncheats; k++) if (g.cheat[k].flags & OM_CH_ON) on0++;
            n0c = g.ncheats;
            CHECK_INT(om_cheats_count("GS-9001", (uint8_t *)arena32, sizeof arena32, &on1, &n1c), 1);
            CHECK_INT(n1c > 0, 1);
            CHECK_INT(on1, i ? on0 - 1u : on0 + 1u);
            (void)n0c;
        }
        free(after);
    }

    /* a file at the cap: built record by record, then one game toggled and saved; past the cap, refused, unchanged */
    big = malloc(OM_CP_MAX_FILE + 4096);
    out = malloc(OM_CP_MAX_FILE + 4096);
    len = 0;
    for (i = 0; ; i++) {
        uint32_t n = 0;
        snprintf(key, sizeof key, "T-%05uG", i);
        big_record(&f, i);
        rc = om_cpk_put(len ? big : NULL, len, key, &f, 20261124u, out, OM_CP_MAX_FILE, rec, &n);
        if (rc < 0) break;
        memcpy(big, out, n);
        len = n;
    }
    CHECK(len > OM_CP_MAX_FILE - 2u * OM_CP_MAX_REC);
    put_file(big, len);
    CHECK_INT(om_pc_open(&pc, "T-00003G", &f, (uint8_t *)arena32, sizeof arena32), 0);
    CHECK_INT(f.ncheats, OM_CH_MAX_CHEATS);
    f.cheat[5].flags ^= OM_CH_ON;
    pc.ed.dirty = 1;
    CHECK_INT(om_pc_save(&pc, NULL, msg, sizeof msg), 0);
    after = get_file(&n1);
    CHECK_INT(n1, len);                                           /* same size: one flag byte and its CRC */
    CHECK_INT(om_cpk_find(after, n1, "T-00004G", &g), 1);
    big_record(&f, 4);
    CHECK_INT(memcmp(f.cheat, g.cheat, sizeof f.cheat), 0);
    free(after);
    /* a brand-new game's record no longer fits: refused, and the file is the same bytes */
    before = get_file(&n0);
    CHECK_INT(om_pc_open(&pc, "T-99999G", &f, (uint8_t *)arena32, sizeof arena32), 1);
    big_record(&f, 99);
    for (i = 0; i < f.ncheats; i++) f.cheat[i].flags |= OM_CH_USER;
    pc.ed.dirty = 1;
    CHECK(om_pc_save(&pc, NULL, msg, sizeof msg) < 0);
    CHECK_STR(msg, "Not saved: CHEATS.PAK would be too big");
    after = get_file(&n1);
    CHECK_INT(n1, n0);
    CHECK_INT(memcmp(before, after, n0), 0);
    free(before); free(after);

    /* a file that does not read: read only, nothing written */
    put_file(fixture, 100);
    CHECK_INT(om_pc_open(&pc, "GS-9001", &f, (uint8_t *)arena32, sizeof arena32), 2);
    CHECK(pc.ed.ro);
    CHECK_INT(f.ncheats, 0);
    pc.ed.dirty = 1;
    CHECK(om_pc_save(&pc, NULL, msg, sizeof msg) < 0);
    after = get_file(&n1);
    CHECK_INT(n1, 100);
    free(after);
    /* no key: read only */
    put_file(fixture, n0);
    CHECK_INT(om_pc_open(&pc, "", &f, (uint8_t *)arena32, sizeof arena32), 2);

    /* no file: a new record, a new file */
    rm_file();
    CHECK_INT(om_pc_open(&pc, "GS-9001", &f, (uint8_t *)arena32, sizeof arena32), 1);
    CHECK_INT(f.ncheats, 0);
    strcpy(f.cheat[0].name, "MY CHEAT");
    f.cheat[0].flags = OM_CH_USER | OM_CH_ON;
    f.cheat[0].count = 1;
    f.code[0].addr = 0x06020000u; f.code[0].value = 0x1234; f.code[0].cmd = 1;
    f.ncheats = 1; f.ncodes = 1;
    pc.ed.dirty = 1;
    CHECK_INT(om_pc_save(&pc, NULL, msg, sizeof msg), 0);
    CHECK_INT(om_pc_open(&pc, "GS-9001", &g, (uint8_t *)arena32, sizeof arena32), 0);
    CHECK_STR(g.cheat[0].name, "MY CHEAT");
    CHECK_INT(g.code[0].value, 0x1234);
    snprintf(p, sizeof p, "%s/aether/CHEATS.PAK.NEW", root);
    CHECK(access(p, F_OK) != 0);                                  /* the replace left no .NEW behind */

    free(big); free(out); free(fixture);
    printf("test_pcheat: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
