/* menu/host-tests/test_uifont.c -- the proportional AA UI faces: the generated atlas's shape, UTF-8 decoding
 * of the three non-ASCII glyphs, proportional measuring, drawing into a 4 bpp buffer and the fade-clip ramp. BSD-3-Clause. */
#include "th.h"
#include "uifont.h"

static int nib(const uint8_t *buf, int stride_px, int x, int y)
{
    uint8_t b = buf[y * (stride_px / 2) + x / 2];
    return (x & 1) ? (b & 15) : (b >> 4);
}

int main(void)
{
    const char *s;
    uint8_t buf[64 * 16];
    int f, w, x, y, any, maxv;

    /* every face: sane metrics, space has an advance and no ink, every printable glyph has ink in range */
    for (f = 0; f < OM_FACE_COUNT; f++) {
        const om_face *fc = om_face_get(f);
        int g;
        CHECK(fc->asc >= 6 && fc->asc <= 18);
        CHECK(om_uf_height(fc) >= 8 && om_uf_height(fc) <= 22);
        CHECK(fc->g[0].w == 0 && fc->g[0].adv8 > 0);              /* ' ' */
        for (g = 1; g < OM_UF_NGLYPH; g++) {
            CHECK(fc->g[g].adv8 > 0);
            if (f == OM_FACE_LABEL8 && (g < 'A' - 32 || g > 'Z' - 32) && (g < '0' - 32 || g > '9' - 32)
                && g != 'v' - 32 && g != '.' - 32) {   /* upper case only; + "v.0-9" (vertag.c) */
                CHECK(fc->g[g].w == 0);
                continue;
            }
            CHECK(fc->g[g].w > 0 && fc->g[g].h > 0);
            CHECK(fc->g[g].yoff + fc->g[g].h <= fc->desc + 1);      /* no ink far below the descender */
        }
    }
    CHECK(om_face_get(99) == &om_face_item);
    {   /* the interstitial centers its label on the pen's end = advances + tracking x (n - 1), no trailing
         * tracking (task-13d brief) */
        const char *lab = "LOADING YOUR LIBRARY", *p = lab;
        int pen8 = 0, n = 0, g2;
        static uint8_t big[192 / 2 * 22];
        while ((g2 = om_uf_next(&p)) >= 0) { pen8 += om_face_label8.g[g2].adv8; n++; }
        pen8 += 13 * (n - 1);
        CHECK_INT(om_uf_draw_tracked(&om_face_label8, lab, big, 192, 192, om_uf_height(&om_face_label8), 0,
                                     om_face_label8.asc, 13), (pen8 + 4) >> 3);
    }
    CHECK(om_uifont_bytes > 10000 && om_uifont_bytes <= 40000);    /* the size gate (tools/omsat/uifont.py MAX_BYTES) */

    /* UTF-8: the three extra glyphs, ASCII, and '?' for anything else (whole sequences skipped) */
    s = "a\xE2\x80\xBA\xC2\xB7\xE2\x80\xA6\xE3\x81\x82\x01" "b";
    CHECK_INT(om_uf_next(&s), 'a' - 32);
    CHECK_INT(om_uf_next(&s), OM_UF_RSAQUO);
    CHECK_INT(om_uf_next(&s), OM_UF_MIDDOT);
    CHECK_INT(om_uf_next(&s), OM_UF_ELLIPSIS);
    CHECK_INT(om_uf_next(&s), '?' - 32);                          /* U+3042: one '?', all three bytes consumed */
    CHECK_INT(om_uf_next(&s), '?' - 32);                          /* control byte */
    CHECK_INT(om_uf_next(&s), 'b' - 32);
    CHECK_INT(om_uf_next(&s), -1);
    s = "\xE2";                                                    /* truncated sequence: no read past the NUL */
    CHECK_INT(om_uf_next(&s), '?' - 32);
    CHECK_INT(om_uf_next(&s), -1);

    /* proportional: W is wider than i, widths add up, SemiBold is at least as wide as Regular */
    CHECK(om_uf_width(&om_face_item, "W") > om_uf_width(&om_face_item, "i"));
    CHECK(om_uf_width(&om_face_item, "iiii") < om_uf_width(&om_face_item, "WWWW"));
    CHECK_INT(om_uf_width(&om_face_item, ""), 0);
    w = om_uf_width(&om_face_brand, "VibeMenu");                   /* the actual wordmark text */
    CHECK(w >= 60 && w <= 80);                                     /* mockup (Segoe 13 SemiBold): ~66 px */
    CHECK(om_uf_width(&om_face_sel, "Panzer Dragoon Saga") >= om_uf_width(&om_face_item, "Panzer Dragoon Saga"));

    /* draw: ink appears inside the buffer, max-combine, clipping at every edge never writes outside */
    memset(buf, 0, sizeof buf);
    x = om_uf_draw(&om_face_item, "Hi", buf, 64, 64, 16, 2, 12);
    CHECK(x > 2 && x < 30);
    for (any = maxv = 0, y = 0; y < 16; y++)
        for (w = 0; w < 64; w++) {
            int v = nib(buf, 64, w, y);
            if (v) any++;
            if (v > maxv) maxv = v;
        }
    CHECK(any > 10); CHECK_INT(maxv, 15);
    CHECK_INT(nib(buf, 64, 0, 0), 0);
    memset(buf, 0, sizeof buf);
    om_uf_draw(&om_face_title, "WWWWWWWWWWWW", buf, 64, 64, 16, -20, 30);   /* off every edge: clipped, no UB */
    om_uf_draw(&om_face_title, "WW", buf, 64, 64, 16, 60, 4);
    memset(buf, 0, sizeof buf);
    om_uf_icon(&om_icon_star, buf, 64, 64, 16, 0, 0);
    CHECK(nib(buf, 64, 4, 4) > 8);                                 /* the star's centre is solid */
    CHECK_INT(nib(buf, 64, 0, 7), 0);                              /* its corners are not */
    CHECK_INT(om_icon_badge_a.h, 11); CHECK_INT(om_icon_badge_a.w, 12); CHECK_INT(om_icon_badge_st.w, 18);
    CHECK_INT(om_icon_folder.w, 9); CHECK_INT(om_icon_folder.h, 8);

    /* flags (addendum 6): all eight area codes, the flag in columns 4..15, opaque, an opaque palette */
    {
        static const char codes[] = "JTUBKAEL";
        for (f = 0; f < 8; f++) {
            const om_flag *fl = om_flag_for(codes[f]);
            CHECK(fl != NULL);
            if (!fl) continue;
            CHECK_INT(nib(fl->bits, 16, 0, 3), 0); CHECK_INT(nib(fl->bits, 16, 3, 3), 0);   /* transparent pad */
            for (y = 0; y < 8; y++) for (x = 4; x < 16; x++) CHECK(nib(fl->bits, 16, x, y) != 0);
            for (x = 0; x < 15; x++) CHECK(fl->rgb[x] & 0x8000u);
        }
        CHECK(om_flag_for('Z') == NULL);
    }

    /* fade-clip (addendum 3): a 12 px ramp to nothing at the cut, nothing touched before it */
    memset(buf, 0, sizeof buf);
    for (y = 0; y < 16; y++) memset(buf + y * 32, 0xFF, 32);     /* full coverage everywhere */
    om_uf_fade(buf, 64, 16, 52, 12);
    CHECK_INT(nib(buf, 64, 51, 5), 15);                            /* before the ramp: untouched */
    CHECK(nib(buf, 64, 52, 5) >= 13);                              /* the ramp's first column keeps 12/13 */
    CHECK(nib(buf, 64, 58, 5) > nib(buf, 64, 62, 5));              /* falling */
    CHECK(nib(buf, 64, 63, 5) <= 2);                               /* the last column nearly gone */
    for (x = 53; x < 64; x++) CHECK(nib(buf, 64, x, 3) <= nib(buf, 64, x - 1, 3));
    om_uf_fade(buf, 64, 16, 60, 12);                               /* a ramp running past the buffer: clipped */
    om_uf_fade(buf, 64, 16, -4, 6);                                /* or starting before it */
    return th_done("test_uifont");
}
