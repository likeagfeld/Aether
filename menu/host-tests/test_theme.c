/* menu/host-tests/test_theme.c -- THEME.INI model, built-ins, RGB555, CC ratio. BSD-3-Clause. */
#include "th.h"
#include "om_fmt.h"
#include "theme.h"

int main(void)
{
    om_theme t;
    uint16_t c;
    const char *ini = "name=Neon\nauthor=Me\ntext=FFFFFF\ntext_hi=#FF0000\npanel=000080\npanel_alpha=40\n"
                      "cursor=00FF00\nbg=BG.BIN\nfont=FONT.BIN\nfuture_key=1\n";
    CHECK_INT(om_rgb555(0xFF0000), 0x001F);                       /* R in bits 4-0 (HWF-30, B<<10 G<<5 R) */
    CHECK_INT(om_rgb555(0x00FF00), 0x03E0);
    CHECK_INT(om_rgb555(0x0000FF), 0x7C00);
    CHECK_INT(om_rgb555(0x080808), 0x0421);                       /* 8 >> 3 = 1 per channel */
    CHECK_INT(om_hex_rgb555("ffffff", &c), OM_OK); CHECK_INT(c, 0x7FFF);
    CHECK_INT(om_hex_rgb555("FFFFF", &c), OM_E_FIELD);
    CHECK_INT(om_hex_rgb555("FFFFFFF", &c), OM_E_FIELD);
    CHECK_INT(om_hex_rgb555("GG0000", &c), OM_E_FIELD);
    om_theme_builtin(&t, OM_THEME_OPENMENU);          /* the constant name stays internal */
    CHECK_STR(t.name, "aether"); CHECK_INT(t.builtin, 1); CHECK_INT(t.panel_alpha, 80);
    CHECK_STR(om_theme_builtin_name(OM_THEME_SATURN), "saturn");
    om_theme_builtin(&t, 99);                                     /* out of range -> saturn */
    CHECK_STR(t.name, "saturn");
    CHECK_INT(om_theme_parse(&t, ini, (uint32_t)strlen(ini)), OM_OK);
    CHECK_STR(t.name, "Neon"); CHECK_STR(t.author, "Me"); CHECK_STR(t.bg, "BG.BIN");
    CHECK_INT(t.text, 0x7FFF); CHECK_INT(t.text_hi, 0x001F); CHECK_INT(t.panel, 0x4000); CHECK_INT(t.cursor, 0x03E0);
    CHECK_INT(t.panel_alpha, 40); CHECK_INT(t.builtin, 0);
    CHECK_INT(om_theme_parse(&t, "panel_alpha=101\n", 16), OM_E_FIELD);
    CHECK_INT(om_theme_parse(&t, "panel_alpha=\n", 13), OM_E_FIELD);
    CHECK_INT(om_theme_parse(&t, "text=red\n", 9), OM_E_FIELD);
    CHECK_INT(om_cc_ratio(100), 0);                               /* fully opaque panel (HWF-31) */
    CHECK_INT(om_cc_ratio(0), 31);                                /* fully see-through */
    CHECK_INT(om_cc_ratio(50), 16);
    CHECK_INT(om_cc_ratio(200), 0);                               /* clamps */
    return th_done("test_theme");
}
