/* menu/host-tests/test_settings.c -- SETTINGS.INI model and merge-writer (contract/SPEC.md §5.5). BSD-3-Clause. */
#include "th.h"
#include "om_fmt.h"
#include "anim.h"
#include "remap.h"
#include "safearea.h"
#include "settings.h"

int main(void)
{
    om_settings s;
    char out[OM_SETTINGS_MAX];
    uint32_t n;
    const char *old = "# mine\nview=folders\ntheme=saturn\nsort=az\nfoo=bar\n[GS-9001]\nregion=U\nsort=year\n";
    const char *want = "theme=TEST\nsort=genre\nfilter_genre=Racing\nfilter_region=UE\nmusic=off\nmusic_vol=10\n"
                       "sfx=on\nclock=12\n"
                       "anim=reduced\n"
                       "overscan=8\nloader=auto\nigr=on\nsaves=on\nsaves_first=share\ncheats=on\n"
                       "reselect=on\nsaver=1\nfw_check=on\n"
                       "pad_ok=A\npad_back=B\npad_info=C\npad_fav=X\npad_sort=Y\npad_filter=Z\npad_pgup=L\npad_pgdn=R\n"
                       "# mine\nview=folders\nfoo=bar\n[GS-9001]\nregion=U\nsort=year\n";
    om_settings_default(&s);
    CHECK_STR(s.theme, "saturn");
    CHECK_INT(s.sort, OM_SORT_AZ); CHECK_INT(s.music, 1); CHECK_INT(s.sfx, 1); CHECK_INT(s.clock24, 0);   /* 12h */
    CHECK_INT(s.filter_region, 0); CHECK_STR(s.filter_genre, ""); CHECK_INT(s.anim, OM_ANIM_FULL);
    {
        const char *in = "sort=year\nfilter_region=ub\nmusic=off\nclock=24\ntheme=x\nsort=bogus\nanim=off\nanim=fast\n";
        CHECK_INT(om_settings_parse(&s, in, (uint32_t)strlen(in)), OM_OK);
    }
    CHECK_INT(s.sort, OM_SORT_YEAR);                              /* "bogus" keeps the previous value */
    CHECK_INT(s.filter_region, (1u << 2) | (1u << 4));            /* U = bit2, B = bit4 (GAMES.DB region bits) */
    CHECK_INT(s.music, 0); CHECK_INT(s.clock24, 1); CHECK_STR(s.theme, "x");   /* a card's clock=24 is kept */
    CHECK_INT(s.anim, OM_ANIM_OFF);                               /* "fast" is unknown: keeps "off" */
    CHECK_STR(s.anim_bad, "fast");                                /* named for the status line */
    CHECK_INT(om_settings_parse(&s, "anim=full\n", 10), OM_OK);
    CHECK_STR(s.anim_bad, "");
    CHECK_INT(om_settings_parse(&s, "[T-1]\nmusic=on\n", 15), OM_OK);
    CHECK_INT(s.music, 0);                                        /* [sections] are M6 per-game options, not globals */
    om_settings_default(&s);
    strcpy(s.theme, "TEST"); s.sort = OM_SORT_GENRE; strcpy(s.filter_genre, "Racing");
    s.filter_region = (1u << 2) | (1u << 3); s.music = 0; s.clock24 = 0;
    s.anim = OM_ANIM_REDUCED;
    CHECK_INT(om_settings_merge(&s, old, (uint32_t)strlen(old), out, sizeof out, &n), OM_OK);
    out[n] = 0;
    CHECK_STR(out, want);                                         /* managed keys first; everything else verbatim */
    CHECK_INT(om_settings_merge(&s, old, (uint32_t)strlen(old), out, 20, &n), OM_E_FULL);
    CHECK_INT(om_settings_merge(&s, "", 0, out, sizeof out, &n), OM_OK);
    CHECK_INT(n, 297);              /* 174 + 32 + 87 + 4 (on, share) */
    CHECK_INT(s.music_vol, 10);                                   /* music_vol= 1..10, default 10 */
    CHECK_INT(om_settings_parse(&s, "music_vol=4\n", 12), OM_OK); CHECK_INT(s.music_vol, 4);
    CHECK_INT(om_settings_parse(&s, "music_vol=11\nmusic_vol=0\nmusic_vol=x\nmusic_vol=\n", 47), OM_OK);
    CHECK_INT(s.music_vol, 4);                                    /* out of range: kept */
    CHECK_INT(om_settings_merge(&s, "music_vol=9\n", 12, out, sizeof out, &n), OM_OK);
    out[n] = 0;
    CHECK(strstr(out, "music_vol=4\n") != NULL && strstr(out, "music_vol=9") == NULL);
    CHECK_STR(om_sort_name(OM_SORT_REGION), "REGION");
    CHECK_STR(om_anim_name(OM_ANIM_FULL), "full"); CHECK_STR(om_anim_name(OM_ANIM_OFF), "off");
    CHECK_STR(om_anim_name(99), "full");
    {   /* an old anim= line is managed: rewritten once, never duplicated */
        const char *o2 = "anim=off\nx=1\n";
        CHECK_INT(om_settings_merge(&s, o2, (uint32_t)strlen(o2), out, sizeof out, &n), OM_OK);
        out[n] = 0;
        /* six more managed lines now sit between anim= and the old file's own lines; eleven
         * more (reselect, saver, fw_check and the eight pad_* keys) after cheats= */
        CHECK(strstr(out, "anim=reduced\noverscan=8\nloader=auto\nigr=on\nsaves=on\nsaves_first=share\ncheats=on\n"
                          "reselect=on\nsaver=1\nfw_check=on\npad_ok=A\n") != NULL);
        CHECK(strstr(out, "pad_pgup=L\npad_pgdn=R\nx=1\n") != NULL);
        CHECK(strstr(out, "anim=off") == NULL);
    }
    CHECK_INT(om_region_mask("jJ x E"), 0x09);                    /* case-insensitive, unknown letters ignored */
    {
        /* SPEC §5.5 "keep every other line byte-for-byte" includes the line ending, not just the
         * content -- a passed-through CRLF line must stay CRLF, not get normalized to LF. */
        const char *crlf_old = "theme=saturn\r\nfoo=bar\r\n[GS-9001]\r\nregion=U\r\n";
        om_settings_default(&s);
        CHECK_INT(om_settings_merge(&s, crlf_old, (uint32_t)strlen(crlf_old), out, sizeof out, &n), OM_OK);
        out[n] = 0;
        CHECK(strstr(out, "foo=bar\r\n[GS-9001]\r\nregion=U\r\n") != NULL);
    }
    {   /* overscan / loader / igr / saves / saves_first / cheats */
        om_settings_default(&s);
        CHECK_INT(s.overscan, OM_OVS_STD); CHECK_INT(s.loader, OM_LDR_AUTO); CHECK_INT(s.igr, 1);
        CHECK_INT(s.saves, 1); CHECK_INT(s.saves_first, OM_SVF_SHARE); CHECK_INT(s.cheats, 1); CHECK_STR(s.bad_key, "");
        {
            const char *in = "overscan=24\nloader=jhl\nigr=off\nsaves=on\nsaves_first=share\ncheats=off\n";
            CHECK_INT(om_settings_parse(&s, in, (uint32_t)strlen(in)), OM_OK);
        }
        CHECK_INT(s.overscan, OM_OVS_MAX); CHECK_INT(s.loader, OM_LDR_JHL); CHECK_INT(s.igr, 0);
        CHECK_INT(s.saves, 1); CHECK_INT(s.saves_first, OM_SVF_SHARE); CHECK_INT(s.cheats, 0);
        CHECK_INT(om_settings_parse(&s, "overscan=12\nloader=fast\n", 24), OM_OK);
        CHECK_INT(s.overscan, OM_OVS_MAX); CHECK_INT(s.loader, OM_LDR_JHL);    /* unknown values: kept, named */
        CHECK_STR(s.bad_key, "overscan");                                       /* the first one */
        {   /* old lines of the new keys are managed: rewritten once */
            const char *o3 = "loader=cwx\nfoo=1\nsaves=on\n";
            om_settings_default(&s);
            s.saves = 0;
            CHECK_INT(om_settings_merge(&s, o3, (uint32_t)strlen(o3), out, sizeof out, &n), OM_OK);
            out[n] = 0;
            CHECK(strstr(out, "loader=auto\n") != NULL && strstr(out, "loader=cwx") == NULL);
            CHECK(strstr(out, "saves=off\n") != NULL && strstr(out, "saves=on") == NULL);
            CHECK(strstr(out, "foo=1\n") != NULL);
        }
        {   /* user decision 2026-11-21: per-game saves are on from the start, no first-enable prompt. Every
             * card the 0.1-M6 writer touched says "saves=off" and "saves_first=" (never decided): that reads as on,
             * Share; an explicit choice (a saves_first value, written with the Settings toggle) is kept */
            const char *never = "saves=off\nsaves_first=\n", *off = "saves=off\nsaves_first=share\n";
            const char *moved = "saves=on\nsaves_first=move\n";
            om_settings_default(&s);
            CHECK_INT(om_settings_parse(&s, never, (uint32_t)strlen(never)), OM_OK);
            CHECK_INT(s.saves, 1); CHECK_INT(s.saves_first, OM_SVF_SHARE);
            om_settings_default(&s);
            CHECK_INT(om_settings_parse(&s, off, (uint32_t)strlen(off)), OM_OK);
            CHECK_INT(s.saves, 0); CHECK_INT(s.saves_first, OM_SVF_SHARE);
            om_settings_default(&s);
            CHECK_INT(om_settings_parse(&s, moved, (uint32_t)strlen(moved)), OM_OK);
            CHECK_INT(s.saves, 1); CHECK_INT(s.saves_first, OM_SVF_MOVE);
            om_settings_default(&s);
            CHECK_INT(om_settings_parse(&s, "", 0), OM_OK);                      /* no file: the defaults */
            CHECK_INT(s.saves, 1); CHECK_INT(s.saves_first, OM_SVF_SHARE);
        }
        CHECK_STR(om_safe_key(OM_OVS_OFF), "off"); CHECK_STR(om_safe_label(OM_OVS_LARGE), "16 px");
        CHECK_INT(om_safe_margin(OM_OVS_MAX), 24); CHECK_INT(om_safe_margin(9), 8);
        CHECK_STR(om_loader_key(OM_LDR_CWX), "cwx"); CHECK_STR(om_loader_label(9), "Auto");
    }
    {   /* M3.5 Task 4 without overscan: reselect / saver / fw_check / pad_* */
        om_settings_default(&s);
        CHECK_INT(s.reselect, 1); CHECK_INT(s.saver_min, 1);   /* user decision 2026-11-23: 1 min */
        CHECK_INT(s.fw_check, 1); CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_A); CHECK_STR(s.bad_key, "");
        CHECK_INT(s.pad_bad, 0);
        {
            const char *in = "reselect=off\nsaver=1\nfw_check=off\npad_ok=B\npad_back=A\n";
            CHECK_INT(om_settings_parse(&s, in, (uint32_t)strlen(in)), OM_OK);
        }
        CHECK_INT(s.reselect, 0); CHECK_INT(s.saver_min, 1);
        CHECK_INT(s.fw_check, 0); CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_B); CHECK_INT(s.pad.btn[OM_ACT_BACK], OM_BTN_A);
        CHECK_INT(s.pad_bad, 0); CHECK_STR(s.bad_key, "");
        CHECK_INT(om_settings_parse(&s, "saver=2\n", 8), OM_OK);
        CHECK_INT(s.saver_min, 1);                                             /* unknown values: kept, named */
        CHECK_STR(s.bad_key, "saver");
        om_settings_default(&s);
        CHECK_INT(om_settings_parse(&s, "pad_sort=X\n", 11), OM_OK);           /* X is Favorite's: a duplicate */
        CHECK_INT(s.pad_bad, 1); CHECK_INT(s.pad.btn[OM_ACT_SORT], OM_BTN_Y);  /* defaults used */
        CHECK_INT(s.pad.btn[OM_ACT_FAV], OM_BTN_X);
        om_settings_default(&s);
        CHECK_INT(om_settings_parse(&s, "pad_sort=X\npad_fav=Y\n", 21), OM_OK); /* a whole-file swap is fine */
        CHECK_INT(s.pad_bad, 0); CHECK_INT(s.pad.btn[OM_ACT_SORT], OM_BTN_X); CHECK_INT(s.pad.btn[OM_ACT_FAV], OM_BTN_Y);
        CHECK_INT(om_settings_parse(&s, "pad_ok=Q\n", 9), OM_OK);
        CHECK_STR(s.bad_key, "pad_ok"); CHECK_INT(s.pad.btn[OM_ACT_OK], OM_BTN_A);
        om_settings_default(&s);
        CHECK_INT(om_settings_parse(&s, "reselect=maybe\nfw_check=2\n", 26), OM_OK);
        CHECK_INT(s.reselect, 1); CHECK_INT(s.fw_check, 1); CHECK_STR(s.bad_key, "reselect");
        {   /* old lines of the new keys are managed: rewritten once */
            const char *o3 = "saver=3\nfoo=1\npad_ok=C\n";
            om_settings_default(&s);
            CHECK_INT(om_settings_merge(&s, o3, (uint32_t)strlen(o3), out, sizeof out, &n), OM_OK);
            out[n] = 0;
            CHECK(strstr(out, "saver=1\n") != NULL && strstr(out, "saver=3") == NULL);
            CHECK(strstr(out, "pad_ok=A\n") != NULL && strstr(out, "pad_ok=C") == NULL);
            CHECK(strstr(out, "foo=1\n") != NULL);
        }
        {   /* a remapped table is written letter by letter */
            om_settings_default(&s);
            om_remap_assign(&s.pad, OM_ACT_OK, OM_BTN_B);
            CHECK_INT(om_settings_merge(&s, "", 0, out, sizeof out, &n), OM_OK);
            out[n] = 0;
            CHECK(strstr(out, "pad_ok=B\npad_back=A\n") != NULL);
        }
        CHECK_STR(om_saver_key(0), "off"); CHECK_STR(om_saver_key(3), "3");
        CHECK_INT(om_saver_next(5, 1), 0); CHECK_INT(om_saver_next(0, -1), 5); CHECK_INT(om_saver_next(1, 1), 3);
    }
    return th_done("test_settings");
}
