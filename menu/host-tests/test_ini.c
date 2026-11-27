/* menu/host-tests/test_ini.c -- contract/SPEC.md §5.5 text-file rules. BSD-3-Clause. */
#include "th.h"
#include "ini.h"
#include "om_fmt.h"

static char seen[16][3][64];
static int nseen;

static int rec(void *ctx, const char *sec, const char *k, const char *v)
{
    (void)ctx;
    if (nseen < 16) { strcpy(seen[nseen][0], sec); strcpy(seen[nseen][1], k); strcpy(seen[nseen][2], v); nseen++; }
    return 0;
}

static int stop_at_b(void *ctx, const char *sec, const char *k, const char *v)
{
    (void)ctx; (void)sec; (void)v;
    return !strcmp(k, "b") ? 7 : 0;
}

int main(void)
{
    const char *t = "# comment\r\n  a = 1 \r\n\r\nb=two words\n[GS-9001]\nregion=U\n  [ x ]\nc=\n";
    char longl[300], line[OM_INI_LINE + 2];
    uint32_t pos = 0;
    CHECK_INT(om_ini_parse(t, (uint32_t)strlen(t), rec, NULL), OM_OK);
    CHECK_INT(nseen, 4);
    CHECK_STR(seen[0][0], ""); CHECK_STR(seen[0][1], "a"); CHECK_STR(seen[0][2], "1");
    CHECK_STR(seen[1][2], "two words");
    CHECK_STR(seen[2][0], "GS-9001"); CHECK_STR(seen[2][1], "region"); CHECK_STR(seen[2][2], "U");
    CHECK_STR(seen[3][0], "x"); CHECK_STR(seen[3][1], "c"); CHECK_STR(seen[3][2], "");
    CHECK_INT(om_ini_parse(t, (uint32_t)strlen(t), stop_at_b, NULL), 7);         /* callback can stop the walk */
    CHECK_INT(om_ini_parse("novalue\n", 8, rec, NULL), OM_E_FIELD);            /* no '=' is an error, not a skip */
    CHECK_INT(om_ini_parse("[open\n", 6, rec, NULL), OM_E_FIELD);
    memset(longl, 'k', 256); longl[256] = '\n';                                  /* 256 B: one over the limit */
    CHECK_INT(om_ini_parse(longl, 257, rec, NULL), OM_E_FIELD);
    memset(longl, 'k', 255); longl[255] = '\r'; longl[256] = '\n';              /* 255 B + CRLF is legal */
    CHECK_INT(om_text_line(longl, 257, &pos, line), 1);
    CHECK_INT((long long)strlen(line), 255);
    CHECK_INT(pos, 257);
    CHECK_INT(om_text_line(longl, 257, &pos, line), 0);                          /* end */
    pos = 0;
    CHECK_INT(om_text_line("x", 1, &pos, line), 1);                              /* last line without LF */
    CHECK_STR(line, "x");
    {
        /* an embedded NUL must be a hard error at that line, not silently copied through and
         * truncating the key/value a downstream C-string op would see. sizeof-1 (not strlen) counts the NUL. */
        static const char nul_val[] = "k=va\0lue\n";                 /* NUL mid-value */
        static const char nul_key[] = "k\0ey=v\n";                   /* NUL mid-key */
        CHECK_INT(om_ini_parse(nul_val, (uint32_t)sizeof(nul_val) - 1, rec, NULL), OM_E_FIELD);
        CHECK_INT(om_ini_parse(nul_key, (uint32_t)sizeof(nul_key) - 1, rec, NULL), OM_E_FIELD);
    }
    return th_done("test_ini");
}
