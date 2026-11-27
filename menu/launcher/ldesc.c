/* menu/launcher/ldesc.c -- see ldesc.h. BSD-3-Clause. */
#include <string.h>
#include "ldesc.h"
#include "cdparse.h"
#include "satiator.h"
#include "yield.h"

static void numbered(char out[12], uint32_t k)
{
    memcpy(out, "out1.desc", 10);
    out[3] = (char)('0' + k);
}

int om_ldesc_write(const om_handoff *h, char name[12], char dir[256], int *alone)
{
    static uint32_t nm[3];                                        /* 4-aligned for s_unlink (satiator.c buffer_xfer) */
    static uint32_t dn[64];                                       /* 4-aligned for s_chdir */
    const char *sl;
    uint32_t k, n;
    int rc;
    *alone = 0;
    memcpy(name, "out.desc", 9);
    strcpy(dir, h->dir);
    if (h->ndisc >= 2 && h->ndisc <= OM_HO_DISCS_MAX) {
        for (k = 1; k <= h->ndisc && !*alone; k++) {
            numbered((char *)nm, k);
            if (image2desc(om_handoff_disc(h, k), (const char *)nm) != 0)
                *alone = (int)k;
        }
        if (!*alone) {
            for (k = h->ndisc + 1u; k <= OM_HO_DISCS_MAX; k++) {  /* not there is fine: nothing to delete */
                numbered((char *)nm, k);
                om_yield();                                /* the launch's eye (yield.h) */
                (void)s_unlink((const char *)nm);
            }
            numbered(name, h->cur);
            return 0;
        }
        if ((sl = strrchr(h->image, '/')) != 0) {                /* alone: from the chosen disc's own folder */
            n = (uint32_t)strlen(dir);
            if (n > 1) dir[n++] = '/';
            if (n + (uint32_t)(sl - h->image) > 255) return -1;
            memcpy(dir + n, h->image, (size_t)(sl - h->image));
            dir[n + (uint32_t)(sl - h->image)] = 0;
            strcpy((char *)dn, dir);
            om_yield();
            if ((rc = s_chdir((const char *)dn)) < 0) return rc;
            return image2desc(sl + 1, "out.desc");
        }
    }
    return image2desc(h->image, "out.desc");
}
