/* menu/src/padkind.c -- see padkind.h. BSD-3-Clause. */
#include "padkind.h"
#include "gamesdb.h"

#define PER_ID_NOT_CONNECT 0xFFu   /* SGL's PER_ID_NotConnect, build/sgl/inc/SL_DEF.H:1247 */
#define PER_ID_STD_PAD     0x02u   /* SGL's PER_ID_StnPad; also the Sega 3D Control Pad in digital mode (HWF-77) */
#define PER_ID_3D_ANALOG   0x16u   /* the Sega 3D Control Pad in analog mode (HWF-77); not an SL_DEF.H constant */

uint8_t om_pad_kind(uint8_t sgl_id, uint8_t data_size)
{
    (void)data_size;
    if (sgl_id == PER_ID_NOT_CONNECT) return OM_PAD_NONE;
    if (sgl_id == PER_ID_3D_ANALOG) return OM_PAD_3D_ANALOG;
    if (sgl_id == PER_ID_STD_PAD) return OM_PAD_STD;
    return OM_PAD_UNKNOWN;
}

int om_pad_conflict(uint8_t kind, uint8_t compat_flags)
{
    return OM_PADKIND_TRUSTED && kind == OM_PAD_3D_ANALOG && (compat_flags & OM_CF_3DPAD) ? 1 : 0;
}
