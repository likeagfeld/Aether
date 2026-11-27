/* menu/host-tests/test_istlgeom.c -- the start-up interstitial shows before SETTINGS.INI is read, so all
 * of it sits inside the largest margin (24 px), whatever overscan= says. BSD-3-Clause. */
#include <math.h>
#include <stdlib.h>
#include "th.h"
#include "istlgeom.h"
#include "safearea.h"

int main(void)
{
    const int lo = OM_SAFE_BOOT, hx = 319 - OM_SAFE_BOOT, hy = 223 - OM_SAFE_BOOT;
    int a, maxx = 0, maxy = 0;
    /* the comet's orbit, tilted (the Aether rename, D9): every point the menu draws (its own integer maths) */
    for (a = 0; a < 256; a++) {
        double t = a * 6.283185307179586 / 256.0;
        int x = (int)(OM_ISTL_RX * cos(t)), y = (int)(OM_ISTL_RY * sin(t));
        int tx = OM_ISTL_TX(x, y), ty = OM_ISTL_TY(x, y);
        if (abs(tx) > maxx) maxx = abs(tx);
        if (abs(ty) > maxy) maxy = abs(ty);
    }
    CHECK(maxx <= OM_ISTL_HALF_X && maxy <= OM_ISTL_HALF_Y);
    CHECK(OM_ISTL_CX - OM_ISTL_HALF_X - 2 >= lo && OM_ISTL_CX + OM_ISTL_HALF_X + 2 <= hx);   /* + the comet's head */
    CHECK(OM_ISTL_CY - OM_ISTL_HALF_Y - 1 >= lo && OM_ISTL_CY + OM_ISTL_HALF_Y + 2 <= hy);
    CHECK(OM_ISTL_CY + OM_ISTL_HALF_Y + 2 < OM_ISTL_LAB_MID - 6);                         /* clear of the label */
    CHECK(OM_ISTL_TY(OM_ISTL_RX, 0) < 0);                     /* the right end up, as the logo's ring */
    CHECK(OM_ISTL_CX - OM_ISTL_LOGO_W / 2 >= lo && OM_ISTL_CX + OM_ISTL_LOGO_W / 2 <= hx);
    CHECK(OM_ISTL_CY - OM_ISTL_LOGO_H / 2 >= lo && OM_ISTL_CY + OM_ISTL_LOGO_H / 2 <= hy);
    CHECK(OM_ISTL_BAR_X0 >= lo && OM_ISTL_BAR_X1 <= hx && OM_ISTL_BAR_Y + 2 <= hy);
    CHECK(OM_ISTL_CNT_MID + 6 <= hy);
    return th_done("test_istlgeom");
}
