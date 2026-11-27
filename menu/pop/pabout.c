/* menu/pop/pabout.c -- see pabout.h. BSD-3-Clause. */
#include <string.h>
#include "pabout.h"

#define C(who, project, licence) { who, project " | " licence }
/* CREDITS.md, "Credited for source code or inspiration" (test_pabout.c checks every row against the file) */
const om_pa_credit om_pa_credits[] = {
    C("mrneo240, megavolt85, sbstnc", "openMenu (https://github.com/mrneo240/openMenu)", "BSD-3-Clause"),
    C("Derek Pascarella (ateam)", "openMenu Virtual Folder Bundle, GD MENU Card Manager "
      "(https://github.com/DerekPascarella/openMenu-Virtual-Folder-Bundle)", "BSD-3-Clause / GPL-3.0"),
    C("sonik-br", "GDMENUCardManager (https://github.com/sonik-br/GDMENUCardManager)", "GPL-3.0"),
    C("abrasive (James Laird-Wah)", "Satiator, satiator-menu, libsatiator, satiator-arpatch (https://github.com/satiator)",
      "MPL-2.0"),
    C("retrohead", "Satiator Rings (https://github.com/retrohead/satiator-rings)", "none stated"),
    C("cafe-alpha", "Pseudo Saturn Kai / Satiator Menu Kai (https://ppcenter.webou.net/pskai/)", "GPL-2.0 / WTFPL"),
    C("Theo Berkau (CyberWarriorX) and the Pseudo Saturn team", "Pseudo Saturn (https://github.com/cyberwarriorx/pseudosaturn)",
      "GPL-2.0"),
    C("Israel Jacquez (Yabause)", "code in Pseudo Saturn Kai's cheat bootstrap, kept verbatim (https://yabause.org/)", "MIT"),
    C("tpunix", "SAROO (https://github.com/tpunix/SAROO)", "GPL-3.0"),
    C("Slinga", "Save Game Copier (https://github.com/slinga-homebrew/Save-Game-Copier)", "GPL-3.0"),
    C("Daniel Hepper", "font8x8 (https://github.com/dhepper/font8x8)", "public domain"),
    C("Julieta Ulanovsky and the Montserrat Project Authors",
      "Montserrat typeface, drawn into the Aether logo (https://github.com/JulietaUla/Montserrat)", "OFL-1.1"),
    C("libretro-database contributors",
      "Sega Saturn DAT, game serials and titles (https://github.com/libretro/libretro-database)", "CC-BY-SA-4.0"),
    C("Felinni Marinas", "Saturn Rings cover project: cover art and title data", "none stated"),
    C("libretro-thumbnails contributors", "Sega Saturn box art (https://github.com/libretro-thumbnails/Sega_-_Saturn)",
      "none stated"),
};
const uint16_t om_pa_ncredits = sizeof om_pa_credits / sizeof om_pa_credits[0];

/* the longest prefix of s[0..n) that fits w px, at the best break: 0 never (one character at least) */
static int fit(const char *s, int n, int w, int kind, om_pa_width wf)
{
    int k, sp = 0, cut = 0;
    if (wf(kind, s, n) <= w) return n;
    for (k = 1; k < n && wf(kind, s, k) <= w; k++) {
        if (s[k] == ' ') sp = k;                                   /* break before this space */
        if (s[k - 1] == '/' || s[k - 1] == '-' || s[k - 1] == '.') cut = k;   /* or after a link's separator */
    }
    if (sp > 2 && s[sp - 1] == '|' && s[sp - 2] == ' ') sp -= 2;   /* never a "|" ending a line: it starts the next */
    if (sp) return sp;
    if (cut) return cut;
    return k > 1 ? k - 1 : 1;
}

static void wrap(om_pa *a, const char *s, int kind, int w, om_pa_width wf)
{
    int n = (int)strlen(s);
    while (n > 0 && a->n < OM_PA_LINES_MAX) {
        int k = fit(s, n > 255 ? 255 : n, w, kind, wf);
        a->line[a->n].s = s;
        a->line[a->n].len = (uint8_t)k;
        a->line[a->n].kind = (uint8_t)kind;
        a->n++;
        s += k; n -= k;
        while (n > 0 && *s == ' ') { s++; n--; }
    }
}

void om_pa_open(om_pa *a, int width, uint16_t rows, om_pa_width wf)
{
    uint16_t i;
    memset(a, 0, sizeof *a);
    a->rows = rows ? rows : 1u;
    for (i = 0; i < om_pa_ncredits; i++) {
        wrap(a, om_pa_credits[i].who, OM_PA_NAME, width, wf);
        wrap(a, om_pa_credits[i].what, OM_PA_WHAT, width, wf);
    }
}

uint16_t om_pa_max_top(const om_pa *a) { return a->n > a->rows ? (uint16_t)(a->n - a->rows) : 0; }

uint32_t om_pa_key(om_pa *a, uint16_t k)
{
    uint16_t top = a->top, last = om_pa_max_top(a);
    if (k & OM_PA_K_B) return OM_PA_CLOSE;
    if ((k & OM_PA_K_U) && top) top--;
    else if ((k & OM_PA_K_D) && top < last) top++;
    else if (k & OM_PA_K_L) top = top > a->rows ? (uint16_t)(top - a->rows) : 0;
    else if (k & OM_PA_K_R) top = (uint16_t)(top + a->rows < last ? top + a->rows : last);
    if (top == a->top) return 0;
    a->top = top;
    return OM_PA_MOVED;
}
