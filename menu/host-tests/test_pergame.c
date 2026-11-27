/* menu/host-tests/test_pergame.c -- PERGAME.INI (SPEC §5.14). BSD-3-Clause. */
#include <string.h>
#include "th.h"
#include "pergame.h"
#include "settings.h"
#include "om_fmt.h"

static const char k_in[] =
    "# header comment\r\n"
    "[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\nloader=jhl\r\nsaves=on\r\nslot=3\r\nmine=keep\r\n"
    "[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n";

static char out[40000];
static uint32_t n;

/* put g into text and NUL-terminate the result; returns the rc */
static int put_s(const char *text, const om_pergame *g)
{
    int rc = om_pergame_put(text, (uint32_t)strlen(text), g, out, sizeof out - 1, &n);
    out[rc ? 0 : n] = 0;
    return rc;
}

int main(void)
{
    om_pergame g;
    uint32_t len = (uint32_t)strlen(k_in);
    CHECK_INT(om_pergame_count(k_in, len), 2);
    CHECK_INT(om_pergame_get(k_in, len, "MK-81045@U", &g), 1);
    CHECK_STR(g.id, "MK-81045@U");
    CHECK_STR(g.title, "Golden Axe - The Duel");
    CHECK_INT(g.loader, OM_LDR_JHL); CHECK_INT(g.saves, OM_PG_ON); CHECK_INT(g.slot, 3);
    CHECK_INT(g.cheats, OM_PG_DEFAULT); CHECK_INT(g.igr, OM_PG_DEFAULT);
    CHECK_INT(om_pergame_get(k_in, len, "T-4507G@J", &g), 1);
    CHECK_INT(g.slot, OM_PG_SLOT_ASK); CHECK_INT(g.cheats, OM_PG_OFF); CHECK_INT(g.loader, OM_LDR_CWX);
    CHECK_STR(g.title, "");
    CHECK_INT(om_pergame_get(k_in, len, "NONE@U", &g), 0);
    CHECK_INT(om_pergame_nth(k_in, len, 0, &g), 1); CHECK_STR(g.id, "MK-81045@U");
    CHECK_INT(om_pergame_nth(k_in, len, 1, &g), 1); CHECK_STR(g.id, "T-4507G@J");
    CHECK_INT(om_pergame_nth(k_in, len, 2, &g), 0);
    /* a hand-edited header that is not a game id -- here 25 B, longer than any id --
     * is reported as such (OM_PG_NTH_BAD, id empty), never truncated into a name that is then looked up "past the
     * end"; the sections after it are still reachable by position */
    {
        static const char bad[] = "[ABCDEFGHIJKLMNOPQRSTUVWXY]\nloader=jhl\n[GS-9001@J]\nloader=cwx\n[x y]\n";
        uint32_t bl = (uint32_t)strlen(bad);
        CHECK_INT(om_pergame_count(bad, bl), 3);
        CHECK_INT(om_pergame_nth(bad, bl, 0, &g), OM_PG_NTH_BAD); CHECK_STR(g.id, "");
        CHECK_INT(g.loader, OM_LDR_AUTO);
        CHECK_INT(om_pergame_nth(bad, bl, 1, &g), 1); CHECK_STR(g.id, "GS-9001@J"); CHECK_INT(g.loader, OM_LDR_CWX);
        CHECK_INT(om_pergame_nth(bad, bl, 2, &g), OM_PG_NTH_BAD);           /* a short name that is not an id */
        CHECK_INT(om_pergame_nth(bad, bl, 3, &g), 0);
    }
    /* put: change one key in place, keep the unknown key and the CRLF section byte-for-byte otherwise */
    om_pergame_get(k_in, len, "MK-81045@U", &g);
    g.loader = OM_LDR_CWX;
    CHECK_INT(put_s(k_in, &g), 0);
    CHECK_STR(out, "# header comment\r\n"
                   "[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\nloader=cwx\r\nsaves=on\r\nslot=3\r\nmine=keep\r\n"
                   "[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n");
    /* put: a key the section lacks goes after its last line, with the section's own CRLF */
    g.cheats = OM_PG_ON; g.igr = OM_PG_OFF;
    CHECK_INT(put_s(k_in, &g), 0);
    CHECK(strstr(out, "mine=keep\r\ncheats=on\r\nigr=off\r\n[T-4507G@J]\n") != NULL);
    /* put: a key set back to its default is dropped (the writer never writes igr=default, saves=default, ...) */
    om_pergame_get(k_in, len, "MK-81045@U", &g);
    g.saves = OM_PG_DEFAULT; g.slot = 0; g.loader = OM_LDR_AUTO; g.igr = OM_PG_DEFAULT;
    CHECK_INT(put_s(k_in, &g), 0);
    CHECK_STR(out, "# header comment\r\n[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\nmine=keep\r\n"
                   "[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n");
    CHECK(strstr(out, "default") == NULL);
    /* put with unchanged values is the identity (an unchanged file is rewritten byte-for-byte) */
    {
        uint32_t i;
        for (i = 0; i < 2; i++) {
            om_pergame_nth(k_in, len, i, &g);
            CHECK_INT(put_s(k_in, &g), 0);
            CHECK_STR(out, k_in);
        }
    }
    /* put: a new game is appended with only its non-default keys */
    om_pergame_default(&g, "GS-9001@J");
    CHECK_INT(g.loader, OM_LDR_AUTO); CHECK_INT(g.slot, 0); CHECK_INT(g.igr, OM_PG_DEFAULT); CHECK_STR(g.title, "");
    g.saves = OM_PG_OFF;
    CHECK_INT(put_s(k_in, &g), 0);
    CHECK(strlen(out) > len && !memcmp(out, k_in, len) && !strcmp(out + len, "[GS-9001@J]\nsaves=off\n"));
    CHECK_INT(om_pergame_count(out, n), 3);
    /* every key, in the fixed order title, loader, saves, slot, cheats, igr */
    om_pergame_default(&g, "GS-9001@J");
    strcpy(g.title, "Daytona USA"); g.loader = OM_LDR_JHL; g.saves = OM_PG_ON; g.slot = OM_PG_SLOT_ASK;
    g.cheats = OM_PG_OFF; g.igr = OM_PG_ON;
    CHECK_INT(put_s("", &g), 0);
    CHECK_STR(out, "[GS-9001@J]\ntitle=Daytona USA\nloader=jhl\nsaves=on\nslot=ask\ncheats=off\nigr=on\n");
    {
        om_pergame h;
        CHECK_INT(om_pergame_get(out, n, "GS-9001@J", &h), 1);
        CHECK(!memcmp(&g, &h, sizeof g) || (!strcmp(g.title, h.title) && g.loader == h.loader && g.saves == h.saves
              && g.slot == h.slot && g.cheats == h.cheats && g.igr == h.igr));
    }
    /* a remembered game with nothing but defaults is just its header (a section = remembered) */
    om_pergame_default(&g, "T-1@");
    CHECK_INT(put_s("", &g), 0);
    CHECK_STR(out, "[T-1@]\n");
    CHECK_INT(om_pergame_get(out, n, "T-1@", &g), 1);
    /* a file without a final newline gets one before the appended section; the old bytes stay */
    om_pergame_default(&g, "B@U");
    g.igr = OM_PG_ON;
    CHECK_INT(put_s("[A@J]\nloader=jhl", &g), 0);
    CHECK_STR(out, "[A@J]\nloader=jhl\n[B@U]\nigr=on\n");
    om_pergame_default(&g, "A@J");
    g.loader = OM_LDR_JHL; g.saves = OM_PG_ON;
    CHECK_INT(put_s("[A@J]\nloader=jhl", &g), 0);
    CHECK_STR(out, "[A@J]\nloader=jhl\nsaves=on\n");
    g.saves = OM_PG_DEFAULT;
    CHECK_INT(put_s("[A@J]\nloader=jhl", &g), 0);
    CHECK_STR(out, "[A@J]\nloader=jhl");
    /* added keys go after the section's last non-blank line, so a blank separator stays between sections */
    om_pergame_default(&g, "A@J");
    g.loader = OM_LDR_JHL; g.saves = OM_PG_ON;
    CHECK_INT(put_s("[A@J]\nloader=jhl\n# note\n\n\n[B@U]\n", &g), 0);
    CHECK_STR(out, "[A@J]\nloader=jhl\n# note\nsaves=on\n\n\n[B@U]\n");
    CHECK_INT(put_s("[A@J]\n\n", &g), 0);
    CHECK_STR(out, "[A@J]\nloader=jhl\nsaves=on\n\n");
    /* a managed key given twice: the first one counts, the writer keeps one line at its position */
    {
        static const char t[] = "[A@J]\nloader=jhl\nx=1\nloader=cwx\n";
        CHECK_INT(om_pergame_get(t, (uint32_t)strlen(t), "A@J", &g), 1);
        CHECK_INT(g.loader, OM_LDR_JHL);
        g.loader = OM_LDR_CWX;
        CHECK_INT(put_s(t, &g), 0);
        CHECK_STR(out, "[A@J]\nloader=cwx\nx=1\n");
    }
    /* values and spacing are read leniently (hand-edited files); the header may carry blanks */
    {
        static const char t[] = "  [ A@J ]  \n  loader = JHL \nSaves=on\ncheats=On\n";
        CHECK_INT(om_pergame_get(t, (uint32_t)strlen(t), "A@J", &g), 1);
        CHECK_INT(g.loader, OM_LDR_JHL); CHECK_INT(g.cheats, OM_PG_ON);
        CHECK_INT(g.saves, OM_PG_DEFAULT);                                /* keys are case-sensitive */
    }
    /* the title is at most 40 bytes, read and written */
    {
        static const char t[] = "[A@J]\ntitle=0123456789012345678901234567890123456789XYZ\n";
        CHECK_INT(om_pergame_get(t, (uint32_t)strlen(t), "A@J", &g), 1);
        CHECK_STR(g.title, "0123456789012345678901234567890123456789");
        CHECK_INT(put_s(t, &g), 0);
        CHECK_STR(out, "[A@J]\ntitle=0123456789012345678901234567890123456789\n");
    }
    /* the global section (before the first '[') is reserved: kept, never read */
    {
        static const char t[] = "loader=jhl\n[A@J]\n";
        CHECK_INT(om_pergame_get(t, (uint32_t)strlen(t), "A@J", &g), 1);
        CHECK_INT(g.loader, OM_LDR_AUTO);
        CHECK_INT(om_pergame_count(t, (uint32_t)strlen(t)), 1);
    }

    /* del: the section from its header to the next '[' goes; the rest stays byte-for-byte */
    CHECK_INT(om_pergame_del(k_in, len, "MK-81045@U", out, sizeof out, &n), 0);
    out[n] = 0;
    CHECK_STR(out, "# header comment\r\n[T-4507G@J]\nloader=cwx\nslot=ask\ncheats=off\n");
    CHECK_INT(om_pergame_count(out, n), 1);
    CHECK_INT(om_pergame_del(k_in, len, "T-4507G@J", out, sizeof out, &n), 0);
    out[n] = 0;
    CHECK_STR(out, "# header comment\r\n"
                   "[MK-81045@U]\r\ntitle=Golden Axe - The Duel\r\nloader=jhl\r\nsaves=on\r\nslot=3\r\nmine=keep\r\n");
    CHECK_INT(om_pergame_del(k_in, len, "NONE@U", out, sizeof out, &n), 0);
    CHECK_INT(n, len); CHECK(!memcmp(out, k_in, len));
    /* duplicates: get / put act on the first; del drops every copy so a deleted game is really forgotten */
    {
        static const char t[] = "[A@J]\nloader=jhl\n[B@U]\n[A@J]\nloader=cwx\n";
        uint32_t tl = (uint32_t)strlen(t);
        CHECK_INT(om_pergame_count(t, tl), 3);
        CHECK_INT(om_pergame_get(t, tl, "A@J", &g), 1); CHECK_INT(g.loader, OM_LDR_JHL);
        g.saves = OM_PG_ON;
        CHECK_INT(put_s(t, &g), 0);
        CHECK_STR(out, "[A@J]\nloader=jhl\nsaves=on\n[B@U]\n[A@J]\nloader=cwx\n");
        CHECK_INT(om_pergame_del(t, tl, "A@J", out, sizeof out, &n), 0);
        out[n] = 0;
        CHECK_STR(out, "[B@U]\n");
    }

    /* bad values keep the default and are not fatal */
    {
        static const char b[] = "[A@J]\nloader=fast\nslot=12\nsaves=maybe\nigr=\n";
        CHECK_INT(om_pergame_get(b, (uint32_t)strlen(b), "A@J", &g), 1);
        CHECK_INT(g.loader, OM_LDR_AUTO); CHECK_INT(g.slot, 0); CHECK_INT(g.saves, OM_PG_DEFAULT);
        CHECK_INT(g.igr, OM_PG_DEFAULT);
    }
    /* an invalid id is refused by put and del */
    om_pergame_default(&g, "A@J");
    strcpy(g.id, "a b@J");
    CHECK_INT(put_s(k_in, &g), OM_E_FIELD);
    CHECK_INT(om_pergame_del(k_in, len, "x", out, sizeof out, &n), OM_E_FIELD);

    /* atomic: a put / del that cannot fit returns OM_E_FULL and leaves the old text untouched, so the caller never
     * writes a partial file (SD write-then-replace is Task 21's om_sdw_replace) */
    {
        char *copy = (char *)th_dup((const uint8_t *)k_in, len + 1);
        om_pergame_get(k_in, len, "MK-81045@U", &g);
        CHECK_INT(om_pergame_put(copy, len, &g, out, 40, &n), OM_E_FULL);
        CHECK_INT(om_pergame_put(copy, len, &g, out, len - 1, &n), OM_E_FULL);
        CHECK_INT(om_pergame_put(copy, len, &g, out, len, &n), 0);      /* exactly enough */
        CHECK_INT(n, len);
        CHECK_INT(om_pergame_del(copy, len, "T-4507G@J", out, 20, &n), OM_E_FULL);
        CHECK(!memcmp(copy, k_in, len + 1));
        free(copy);
    }
    /* oversize (> 16,384 B, the Task 15 ruling carried to Task 18) and corrupt (a line > 255 B, an embedded NUL) files
     * are refused, never half-read */
    CHECK_INT(OM_PERGAME_MAX_BYTES, 16384);
    {
        static char big[OM_PERGAME_MAX_BYTES + 2];
        uint32_t i, bl;
        memset(big, '#', sizeof big);
        for (i = 100; i < sizeof big; i += 101) big[i] = '\n';
        memcpy(big, "[A@J]\n", 6);
        bl = OM_PERGAME_MAX_BYTES + 1;
        CHECK_INT(om_pergame_get(big, bl, "A@J", &g), OM_E_FULL);
        CHECK_INT(om_pergame_count(big, bl), OM_E_FULL);
        CHECK_INT(om_pergame_nth(big, bl, 0, &g), OM_E_FULL);
        om_pergame_default(&g, "A@J");
        CHECK_INT(om_pergame_put(big, bl, &g, out, sizeof out, &n), OM_E_FULL);
        CHECK_INT(om_pergame_del(big, bl, "A@J", out, sizeof out, &n), OM_E_FULL);
        CHECK_INT(om_pergame_count(big, OM_PERGAME_MAX_BYTES), 1);       /* exactly at the cap is fine */
        /* a put whose result would pass the cap is refused too */
        g.saves = OM_PG_ON;
        CHECK_INT(om_pergame_put(big, OM_PERGAME_MAX_BYTES - 3, &g, out, sizeof out, &n), OM_E_FULL);
    }
    {
        static char longl[300];
        static const char nul[] = "[A@J]\nloader=j\0hl\n";
        memset(longl, 'x', sizeof longl);
        memcpy(longl, "[A@J]\ntitle=", 12);
        CHECK_INT(om_pergame_get(longl, sizeof longl, "A@J", &g), OM_E_FIELD);
        CHECK_INT(om_pergame_count(longl, sizeof longl), OM_E_FIELD);
        CHECK_INT(om_pergame_put(longl, sizeof longl, &g, out, sizeof out, &n), OM_E_FIELD);
        CHECK_INT(om_pergame_get(nul, sizeof nul - 1, "A@J", &g), OM_E_FIELD);
    }
    /* at most 512 sections: a 513th is refused, an existing one can still change */
    {
        static char many[OM_PERGAME_MAX_BYTES];
        uint32_t i, ml = 0;
        for (i = 0; i < OM_PERGAME_MAX_SECTIONS; i++) {
            many[ml++] = '[';
            many[ml++] = 'G'; many[ml++] = (char)('0' + i / 100); many[ml++] = (char)('0' + i / 10 % 10);
            many[ml++] = (char)('0' + i % 10);
            memcpy(many + ml, "@J]\n", 4); ml += 4;
        }
        CHECK_INT(om_pergame_count(many, ml), 512);
        om_pergame_default(&g, "NEW@U");
        CHECK_INT(om_pergame_put(many, ml, &g, out, sizeof out, &n), OM_E_FULL);
        om_pergame_default(&g, "G511@J");
        g.loader = OM_LDR_JHL;
        CHECK_INT(om_pergame_put(many, ml, &g, out, sizeof out, &n), 0);
        CHECK_INT(n, ml + 11);
    }

    /* the golden fixture (the same bytes tools/tests/test_pergame.py reads) */
    {
        uint32_t fl, i;
        char *f = (char *)th_load(FIX("tiny.pergame.ini"), &fl);
        CHECK_INT(om_pergame_count(f, fl), 2);
        CHECK_INT(om_pergame_get(f, fl, "MK-81045@U", &g), 1);
        CHECK_STR(g.title, "Golden Axe - The Duel");
        CHECK_INT(g.loader, OM_LDR_JHL); CHECK_INT(g.saves, OM_PG_ON); CHECK_INT(g.slot, 3);
        CHECK_INT(om_pergame_get(f, fl, "T-4507G@J", &g), 1);
        CHECK_INT(g.loader, OM_LDR_CWX); CHECK_INT(g.slot, OM_PG_SLOT_ASK); CHECK_INT(g.cheats, OM_PG_OFF);
        for (i = 0; i < 2; i++) {
            CHECK_INT(om_pergame_nth(f, fl, i, &g), 1);
            CHECK_INT(om_pergame_put(f, fl, &g, out, sizeof out, &n), 0);
            CHECK_INT(n, fl); CHECK(!memcmp(out, f, fl));
        }
        free(f);
    }
    return th_done("test_pergame");
}
