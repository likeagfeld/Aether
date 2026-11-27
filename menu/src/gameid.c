/* menu/src/gameid.c -- see gameid.h (SPEC §5.14). BSD-3-Clause. */
#include "gameid.h"
#include "om_fmt.h"

int om_gameid_ini(const uint8_t product[10], const uint8_t areas[10], char out[OM_GID_MAX])
{
    uint32_t n = 0, i, k = 0;
    for (; n < OM_GID_PRODUCT && product[n] > ' '; n++) {
        uint8_t c = product[n];
        if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
        out[n] = (char)(((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? c : '_');
    }
    if (!n)
        return OM_E_FIELD;
    out[n++] = '@';
    for (i = 0; i < 10 && areas[i] && k < OM_GID_AREAS; i++)
        if (areas[i] >= 'A' && areas[i] <= 'Z')
            out[n + k++] = (char)areas[i];
    out[n + k] = 0;
    return OM_OK;
}

int om_gameid_check(const char *id)
{
    uint32_t p = 0, k = 0;
    while ((id[p] >= 'A' && id[p] <= 'Z') || (id[p] >= '0' && id[p] <= '9') || id[p] == '-' || id[p] == '_')
        p++;
    if (!p || p > OM_GID_PRODUCT || id[p] != '@')
        return OM_E_FIELD;
    while (id[p + 1 + k] >= 'A' && id[p + 1 + k] <= 'Z')
        k++;
    return (k <= OM_GID_AREAS && !id[p + 1 + k]) ? OM_OK : OM_E_FIELD;
}

int om_gameid_dir(const char *id, char out[OM_GID_MAX + 1])
{
    uint32_t i = 0, o = 0;
    if (om_gameid_check(id))
        return OM_E_FIELD;
    while (id[i] != '@')
        out[o++] = id[i++];
    if (id[++i]) {
        out[o++] = '[';
        while (id[i])
            out[o++] = id[i++];
        out[o++] = ']';
    }
    out[o] = 0;
    return OM_OK;
}
