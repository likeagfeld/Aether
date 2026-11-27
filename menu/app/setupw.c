/* menu/app/setupw.c -- see setup.h. Portable (host tests run it over fake_satiator.c). BSD-3-Clause. */
#include "setup.h"
#include "setupid.h"
#include "sd.h"
#include "om_fmt.h"

int om_su_save(const char *text, uint32_t len, uint8_t *buf, uint32_t cap)
{
    uint32_t old = 0;
    int rc = om_sdw_read_file(OM_SU_PATH, buf, cap, &old);   /* its recovery rule: an interrupted replace's .NEW */
    if (rc == 0 && om_su_same((const char *)buf, old, text, len))
        return 0;
    if (rc != 0 && rc != OM_SDW_NOFILE && rc != OM_E_FULL)
        return rc;
    rc = om_sd_replace(OM_SU_PATH, text, len);                /* write, verify, rename over (sdwrite.h) */
    return rc ? rc : 1;
}
