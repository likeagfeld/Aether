#include "th.h"
#include "match.h"
#include "cases.inc"

static void a_key(void *c, uint32_t i, char out[17]) { (void)c; strncpy(out, MC_KEYS[i], 16); out[16] = 0; }
static int a_has(void *c, const char *k)
{
    uint32_t i;
    (void)c;
    for (i = 0; i < MC_NKEYS; i++)
        if (!strcmp(MC_KEYS[i], k)) return 1;
    return 0;
}
static int a_alias(void *c, const char *k, char canon[17])
{
    uint32_t i;
    char n[17];
    (void)c;
    for (i = 0; i < MC_NALIASES; i++) {
        om_normalise(MC_ALIASES[i][0], (uint32_t)strlen(MC_ALIASES[i][0]), n);
        if (!strcmp(n, k)) { strncpy(canon, MC_ALIASES[i][1], 16); canon[16] = 0; return 1; }
    }
    return 0;
}

int main(void)
{
    om_keysrc ks = { NULL, MC_NKEYS, a_key, a_has, a_alias };
    om_match_result r;
    uint32_t i, n;
    char buf[32];
    uint8_t *f;
    om_gamesdb db;
    om_normalise("GS-7103\n (610-6342)", 19, buf); CHECK_STR(buf, "GS-7103");
    om_normalise("  t-8113h (EAE) ", 16, buf); CHECK_STR(buf, "T-8113H");
    om_normalise("ABCDEFGHIJKLMNOPQRST", 20, buf); CHECK_STR(buf, "ABCDEFGHIJKLMNOP");
    om_cleanup("T-29002G V", 10, buf, sizeof buf); CHECK_STR(buf, "T-29002G");
    om_cleanup("GS-9001   ", 10, buf, sizeof buf); CHECK_STR(buf, "GS-9001   ");
    om_cleanup("MK-81029V1", 10, buf, sizeof buf); CHECK_STR(buf, "MK-81029");     /* SPEC §6 v3 version spill */
    om_cleanup("GS-9055 V1", 10, buf, sizeof buf); CHECK_STR(buf, "GS-9055");
    om_cleanup("T-1234V   ", 10, buf, sizeof buf); CHECK_STR(buf, "T-1234V   ");   /* no blank before V: kept */
    for (i = 0; i < MC_NCASES; i++) {                                    /* contract/fixtures/match_cases.json */
        om_match(&ks, MC_CASES[i].product, (uint32_t)strlen(MC_CASES[i].product), MC_CASES[i].area, &r);
        CHECK_STR(r.key, MC_CASES[i].key ? MC_CASES[i].key : "");
        if (MC_CASES[i].how) CHECK_STR(om_how_name(r.how), MC_CASES[i].how);
        else CHECK(r.how == OM_HOW_NONE);
        if (th_fail) printf("  case %u: product '%s' area %s\n", (unsigned)i, MC_CASES[i].product, MC_CASES[i].area);
    }
    printf("test_match: %u of %u contract cases run\n", (unsigned)i, (unsigned)MC_NCASES);
    f = th_load(FIX("tiny.games.db"), &n);                               /* GAMES.DB-backed key source */
    CHECK_INT(om_gamesdb_open(&db, f, n), OM_OK);
    om_keysrc_gamesdb(&ks, &db);
    CHECK_INT(om_match(&ks, "610-6342  ", 10, "U", &r), 1);
    CHECK_STR(r.key, "GS-7103"); CHECK(r.how == OM_HOW_ALIAS);
    CHECK_INT(om_match(&ks, "GS-9001   ", 10, "E", &r), 1);
    CHECK_STR(r.key, "GS-9001"); CHECK(r.how == OM_HOW_EXACT);
    CHECK_INT(om_match(&ks, "          ", 10, "J", &r), 0);
    free(f);
    return th_done("test_match");
}
