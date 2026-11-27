/* menu/ui/text.c -- see text.h. BSD-3-Clause. */
#include "sgl.h"
#include "text.h"

static int s_inset;          /* NBG0 cells moved in from every edge (0..3) */
static uint32_t s_cut;       /* characters that did not fit since boot */

void om_text_set_inset(int cells) { s_inset = cells < 0 ? 0 : cells > 3 ? 3 : cells; }
uint32_t om_text_cut_count(void) { return s_cut; }

static void put_at(int x, int y, const char *s, int width)
{   /* (x, y) are absolute cells; width is already within the row */
    char line[41];
    int i;
    for (i = 0; i < width && s[i]; i++)
        line[i] = ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] >= 0x7F) ? '?' : s[i];
    for (; i < width; i++)
        line[i] = ' ';
    line[width] = 0;
    slPrint(line, slLocate((Uint16)x, (Uint16)y));
}

void om_text_put(int x, int y, const char *s, int width)
{   /* inside the inset area: (x, y) moves in by the inset and the width is capped at 40 - 2 * inset - x; every
     * character of s past that cap, or on a row below the area, is counted (om_text_cut_count) */
    int cap = 40 - 2 * s_inset - x, n = 0;
    if (x < 0 || y < 0 || cap <= 0) return;
    while (s[n]) n++;
    if (y > 27 - 2 * s_inset) {
        s_cut += (uint32_t)n;
        return;
    }
    if (width > cap) width = cap;
    if (width <= 0) return;
    if (n > width) s_cut += (uint32_t)(n - width);
    put_at(x + s_inset, y + s_inset, s, width);
}

void om_text_clear(void)
{   /* the whole 40 x 28 screen, whatever the inset (a page drawn at an inset must not leave the border rows) */
    int y;
    for (y = 0; y < 28; y++)
        put_at(0, y, "", 40);
}

char *om_itoa(char *buf, int v)
{
    char t[12];
    int n = 0, i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) buf[i++] = '-';
    while (n) buf[i++] = t[--n];
    buf[i] = 0;
    return buf;
}

int om_tv_is_pal(void)
{
    return (*(volatile Uint16 *)0x25F80004) & 1;   /* VDP2 TVSTAT bit0 = PAL (HWF-10) */
}
