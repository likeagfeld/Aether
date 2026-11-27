/* menu/launcher/cwxplan.c -- see cwxplan.h. Pure C (host-tested by menu/host-tests/test_cwx.c). BSD-3-Clause. */
#include <string.h>
#include "cwxplan.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

int om_cwx_header(const uint8_t *ip)
{
    return memcmp(ip, "SEGA SEGASATURN ", 16) == 0 || memcmp(ip, "PSEUDO SATURN   ", 16) == 0 ? 0 : OM_CWX_E_HEADER;
}

/* ECMA-119 §9.1: byte 0 the record length, 2-5 the extent (LSB half of the both-endian field), 10-13 the data
 * length (LSB half), 25 the file flags (bit 1 = directory). A zero length byte ends the records of a sector
 * (records never cross a 2,048-byte boundary, §6.8.1.1), so the walk goes on at the next sector. */
int om_cwx_first(const uint8_t *dir, uint32_t len, uint32_t *lba, uint32_t *size)
{
    uint32_t at = 0, n = 0;
    while (at < len) {
        uint32_t rl = dir[at];
        if (rl == 0) {                                    /* the rest of this sector is padding */
            at = (at / 2048u + 1u) * 2048u;
            continue;
        }
        if (rl < 34 || at + rl > len)
            return OM_CWX_E_FIRST;                        /* malformed, or cut by the buffer */
        if (n++ == 2) {                                   /* file identifier [2] (ST-040-R4 §4.3) */
            if (dir[at + 25] & 0x02)
                return OM_CWX_E_FIRST;
            *lba = le32(dir + at + 2);
            *size = le32(dir + at + 10);
            return 0;
        }
        at += rl;
    }
    return OM_CWX_E_FIRST;
}

/* the IP's used bytes (cwxplan.h): OM_CWX_IP_MIN, or up to its last non-zero long in the first min(ip_len, ip_size) */
static uint32_t ip_used(const uint8_t *ip, uint32_t ip_len, uint32_t ip_size)
{
    uint32_t n = (ip_len < ip_size ? ip_len : ip_size) & ~3u;
    while (n > OM_CWX_IP_MIN && !(ip[n - 1] | ip[n - 2] | ip[n - 3] | ip[n - 4]))
        n -= 4;
    return n > OM_CWX_IP_MIN ? n : OM_CWX_IP_MIN;
}

int om_cwx_plan_make(const uint8_t *ip, uint32_t ip_len, uint32_t lba, uint32_t size, om_cwx_plan *p)
{
    uint32_t exec, a, ip_end;
    int rc;
    if ((rc = om_cwx_header(ip)) != 0)
        return rc;
    memset(p, 0, sizeof *p);
    p->ip_size = be32(ip + 0xE0);
    if (p->ip_size == 0 || p->ip_size > 0x8000u || (p->ip_size & 3u))
        return OM_CWX_E_IPSIZE;
    exec = be32(ip + 0xF0);
    if (exec == 0)
        return 0;                                         /* "Transfer is not performed when 0H" */
    if (exec & 3u)
        return OM_CWX_E_ADDR;
    a = exec & ~0x20000000u;                              /* either cache alias */
    ip_end = OM_CWX_IP + ip_used(ip, ip_len, p->ip_size);   /* not + ip_size: retail IPs declare 8000H (cwxplan.h) */
    if (a >= 0x06000000u && a < OM_CWX_HW_END) {
        if (a < ip_end || size > OM_CWX_HW_END - a)
            return OM_CWX_E_SIZE;
        p->data = exec;
    } else if (a >= OM_CWX_LW_LO && a < OM_CWX_TAIL_END) {
        if (size > OM_CWX_TAIL - a || size > OM_CWX_HW_END - OM_CWX_HW_LO || a >= OM_CWX_TAIL
            || ip_end > OM_CWX_HW_LO)                     /* the tail zeroes the copy at OM_CWX_HW_LO before the IP */
            return OM_CWX_E_SIZE;
        p->data = OM_CWX_HW_LO;
    } else
        return OM_CWX_E_ADDR;
    p->exec = exec;
    p->size = size;
    p->fad = lba + 150u;
    return 0;
}

const char *om_cwx_strerror(int rc)
{
    switch (rc) {
    case OM_CWX_E_HEADER: return "CWX: bad disc header. Bad image?";
    case OM_CWX_E_IPSIZE: return "CWX: bad IP size in the disc header";
    case OM_CWX_E_FIRST: return "CWX: no program file in the disc's root";
    case OM_CWX_E_ADDR: return "CWX: bad program address in the header";
    case OM_CWX_E_SIZE: return "CWX: program does not fit in work RAM";
    default: return NULL;
    }
}
