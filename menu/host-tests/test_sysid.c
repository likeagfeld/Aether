#include "th.h"
#include "sysid.h"
#include "cases.inc"

static void unhex(const char *h, uint8_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

int main(void)
{
    uint32_t n, i, titled = 0;
    uint8_t *g = th_load(FIX("sysid_gs9001.bin"), &n), *p = th_load(FIX("sysid_pal_trunc.bin"), &n);
    uint8_t raw[2352], b[256], dn, dm;
    om_sysid s;
    char out[64];
    const char *cue = "REM x\r\n  FILE \"Track 01.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n";
    CHECK_INT(om_sysid_parse(g, 256, &s), OM_OK);
    CHECK_STR(s.product, "GS-9001"); CHECK_STR(s.area, "JTUE"); CHECK_STR(s.device, "CD-1/1");
    CHECK_STR(s.title, "DAYTONA USA"); CHECK_STR(s.date, "19950401"); CHECK_STR(s.maker, "SEGA ENTERPRISES");
    CHECK_INT(s.area_flags, 15); CHECK_INT(s.disc, 1); CHECK_INT(s.disc_total, 1);
    CHECK_INT(om_sysid_parse(p, 256, &s), OM_OK);
    CHECK_STR(s.product, "T-11304H-5"); CHECK_INT(s.area_flags, 8);
    memset(raw, 0, sizeof raw);                                          /* MODE1/2352 raw sector: System ID at 16 */
    memset(raw + 1, 0xFF, 10); raw[15] = 1;
    memcpy(raw + 16, g, 256);
    CHECK_INT(om_sysid_parse(raw, sizeof raw, &s), OM_OK); CHECK_STR(s.product, "GS-9001");
    raw[15] = 2; memset(raw + 16, 0, 8); memcpy(raw + 24, g, 256);       /* MODE2: at 24 (v1 clarification 4) */
    CHECK_INT(om_sysid_parse(raw, sizeof raw, &s), OM_OK); CHECK_STR(s.product, "GS-9001");
    CHECK_INT(om_sysid_parse(raw, 100, &s), OM_E_SHORT);
    memset(b, 0, sizeof b);
    CHECK_INT(om_sysid_parse(b, 256, &s), OM_E_MAGIC);
    for (i = 0; i < RH_N; i++) {                                         /* F-real: the user's retail discs */
        unhex(RH[i].hex, b, 256);
        CHECK_INT(om_sysid_parse(b, 256, &s), OM_OK);
        CHECK_STR(s.product, RH[i].product); CHECK_STR(s.device, RH[i].device);
        CHECK_STR(s.area, RH[i].area); CHECK_STR(s.date, RH[i].date);
        if (RH[i].title) { CHECK_STR(s.title, RH[i].title); titled++; }
    }
    printf("test_sysid: F-real coverage %u of %u headers (%u with ASCII titles compared)\n",
           (unsigned)i, (unsigned)RH_N, (unsigned)titled);
    CHECK_INT(om_cue_first_file(cue, (uint32_t)strlen(cue), out, sizeof out), OM_OK);
    CHECK_STR(out, "Track 01.bin");
    CHECK_INT(om_cue_first_file("file game.bin BINARY\n", 21, out, sizeof out), OM_OK);
    CHECK_STR(out, "game.bin");
    CHECK_INT(om_cue_first_file("TRACK 01 AUDIO\n", 15, out, sizeof out), OM_E_FIELD);
    CHECK_INT(om_cue_first_file("FILEX a.bin\n", 12, out, sizeof out), OM_E_FIELD);
    {   /* 2026-11-23 (clarification 7's cue dedupe): every FILE name, path stripped; a broken line is skipped (the
         * same text as tools/tests/test_cardscan.py test_fs_source_reads_cue_file_lines) */
        static const char t[] = "REM x\r\n  file \"sub\\game.iso\" BINARY\r\nFILE track02.bin BINARY\n"
                                "FILE \"broken.bin\nFILE ok.iso BINARY\n";
        char names[64];
        CHECK_INT(om_cue_files(t, (uint32_t)strlen(t), names, sizeof names), 3);
        CHECK_STR(names, "game.iso"); CHECK_STR(names + 9, "track02.bin"); CHECK_STR(names + 21, "ok.iso");
        CHECK_INT(om_cue_files(t, (uint32_t)strlen(t), names, 16), 1);      /* the rest no longer fits: dropped */
        CHECK_INT(om_cue_files("TRACK 01 AUDIO\n", 15, names, sizeof names), 0);
    }
    om_disc_n_m("cd-2/4", &dn, &dm); CHECK_INT(dn, 2); CHECK_INT(dm, 4);
    om_disc_n_m("", &dn, &dm); CHECK_INT(dn, 1); CHECK_INT(dm, 1);
    CHECK_INT(om_area_flags("JTUEB"), 31);

    /* --- hostile-input RED tests (lesson from the Tasks 3+4 review) --- */

    /* 5. truncated cue: a FILE line whose quoted filename is never closed because the text ends first
     * (e.g. a cue sheet read that was cut off mid-line). Must be rejected, not read past the end. */
    {
        const char *trunc1 = "REM x\r\n  FILE \"Track 01.bin";                 /* no closing quote, no newline */
        CHECK_INT(om_cue_first_file(trunc1, (uint32_t)strlen(trunc1), out, sizeof out), OM_E_FIELD);
        /* also: closing quote replaced by a newline inside the text (quote never found before EOL) */
        CHECK_INT(om_cue_first_file("FILE \"Track 01.bin\nBINARY\n", 26, out, sizeof out), OM_E_FIELD);
    }
    /* 6. .bin shorter than the System ID needs: exactly at and one byte below the MODE1 (16+256=272)
     * and MODE2 (24+256=280) thresholds, using the same raw[] buffer already carrying a MODE2 System ID. */
    CHECK_INT(om_sysid_parse(raw, 24 + 256 - 1, &s), OM_E_SHORT);         /* MODE2: one byte short */
    CHECK_INT(om_sysid_parse(raw, 24 + 256, &s), OM_OK);                 /* MODE2: exactly enough */
    raw[15] = 1; memset(raw + 16, 0, 256); memcpy(raw + 16, g, 256);      /* back to MODE1 at 16 */
    CHECK_INT(om_sysid_parse(raw, 16 + 256 - 1, &s), OM_E_SHORT);         /* MODE1: one byte short */
    CHECK_INT(om_sysid_parse(raw, 16 + 256, &s), OM_OK);                 /* MODE1: exactly enough */

    free(g); free(p);
    return th_done("test_sysid");
}
