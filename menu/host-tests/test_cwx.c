/* menu/host-tests/test_cwx.c -- the pure half of the CWX boot (menu/launcher/cwxplan.h). BSD-3-Clause. */
#include "th.h"
#include "cwxplan.h"

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* one ECMA-119 §9.1 directory record at r: length, extent (both-endian), data length (both-endian), flags, name */
static uint32_t rec(uint8_t *r, uint32_t lba, uint32_t size, uint8_t flags, const char *name, uint8_t nl)
{
    uint32_t len = 33u + nl + (nl % 2 == 0);
    memset(r, 0, len);
    r[0] = (uint8_t)len;
    le32(r + 2, lba); be32(r + 6, lba);
    le32(r + 10, size); be32(r + 14, size);
    r[25] = flags;
    r[32] = nl;
    memcpy(r + 33, name, nl);
    return len;
}

static uint8_t ip[0x8000], dir[4096];   /* ip: the launcher's 16-sector read */

static void mk_ip(const char *hdr, uint32_t ip_size, uint32_t first)
{
    memset(ip, 0, sizeof ip);
    memset(ip, ' ', 0x100);
    memcpy(ip, hdr, 16);
    be32(ip + 0xE0, ip_size);
    be32(ip + 0xF0, first);
}

int main(void)
{
    uint32_t lba = 0, size = 0, o;
    om_cwx_plan p;
    /* headers */
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06004000);
    CHECK_INT(om_cwx_header(ip), 0);
    mk_ip("PSEUDO SATURN   ", 0x1800, 0x06004000);
    CHECK_INT(om_cwx_header(ip), 0);
    mk_ip("SEGA SEGASATURN!", 0x1800, 0x06004000);
    CHECK_INT(om_cwx_header(ip), OM_CWX_E_HEADER);
    /* the 1st Read File: file identifier [2], the third record */
    memset(dir, 0, sizeof dir);
    o = rec(dir, 20, 2048, 2, "\0", 1);
    o += rec(dir + o, 20, 2048, 2, "\1", 1);
    rec(dir + o, 25, 123456, 0, "0.BIN;1", 7);
    CHECK_INT(om_cwx_first(dir, 2048, &lba, &size), 0);
    CHECK_INT(lba, 25); CHECK_INT(size, 123456);
    CHECK_INT(om_cwx_first(dir, (uint32_t)o, &lba, &size), OM_CWX_E_FIRST);      /* only "." and ".." */
    memset(dir, 0, sizeof dir);
    o = rec(dir, 20, 4096, 2, "\0", 1);
    o += rec(dir + o, 20, 4096, 2, "\1", 1);
    rec(dir + o, 30, 2048, 2, "SUBDIR", 6);
    CHECK_INT(om_cwx_first(dir, 4096, &lba, &size), OM_CWX_E_FIRST);            /* a folder is not a program */
    memset(dir, 0, sizeof dir);                                                   /* records continue in sector 2 */
    o = rec(dir, 20, 4096, 2, "\0", 1);
    rec(dir + o, 20, 4096, 2, "\1", 1);
    rec(dir + 2048, 31, 777, 0, "A.BIN;1", 7);
    CHECK_INT(om_cwx_first(dir, 4096, &lba, &size), 0);
    CHECK_INT(lba, 31); CHECK_INT(size, 777);
    CHECK_INT(om_cwx_first(dir, 2048, &lba, &size), OM_CWX_E_FIRST);
    dir[0] = 200;                                                                 /* a record past the buffer */
    CHECK_INT(om_cwx_first(dir, 150, &lba, &size), OM_CWX_E_FIRST);
    /* the plan: HWRAM program read in place */
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06004000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x10000, &p), 0);
    CHECK_INT(p.exec, 0x06004000); CHECK_INT(p.data, 0x06004000); CHECK_INT(p.size, 0x10000);
    CHECK_INT(p.fad, 175); CHECK_INT(p.ip_size, 0x1800);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, OM_CWX_HW_END - 0x06004000, &p), 0);      /* up to the end of HWRAM */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, OM_CWX_HW_END - 0x06004000 + 4, &p), OM_CWX_E_SIZE);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x26004000);                               /* the cache-through alias */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), 0);
    CHECK_INT(p.data, 0x26004000);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06003800);                               /* right after a 6 KB IP */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), 0);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06003400);                               /* over its zero last 1 KB */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), 0);
    memset(ip + 0x1000, 0xA5, 0x500);                                            /* used up to +0x1500 */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), OM_CWX_E_SIZE);
    memset(ip + 0x1400, 0, 0x100);                                               /* now used up to +0x1400 */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), 0);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06003200);
    memset(ip + 0x1000, 0xA5, 0x400);                                            /* used up to +0x1400 */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), OM_CWX_E_SIZE);
    CHECK_INT(om_cwx_plan_make(ip, 0x1200, 25, 0x1000, &p), 0);                 /* only ip_len bytes are looked at */
    CHECK_INT(om_cwx_plan_make(ip, 0x1203, 25, 0x1000, &p), 0);                 /* a partial long is not looked at */
    CHECK_INT(om_cwx_plan_make(ip, 0x204, 25, 0x1000, &p), 0);                  /* under 1000H: 1000H */
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06003400);
    ip[0x1200] = 1;                                                              /* one byte of a long: that long */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), 0);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06003200);
    ip[0x1203] = 1;                                                              /* its last byte: up to +0x1204 */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), OM_CWX_E_SIZE);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06003800);
    memset(ip + 0x1800, 0xA5, 0x100);                                            /* past the IP size: not the IP */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), 0);
    mk_ip("SEGA SEGASATURN ", 0x1800, 0x06002C00);                               /* inside the first 1000H */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), OM_CWX_E_SIZE);
    mk_ip("SEGA SEGASATURN ", 0x8000, 0x06002FFC);                               /* one long short of it */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 25, 0x1000, &p), OM_CWX_E_SIZE);
    /* Daytona USA (USA), MK-81200 V1.000 (hardware 2026-11-25: step 9 rc -94): IP size 8000H, 1st Read Address
     * 0x06003000, the IP's last non-zero byte at +0xE8A, APROG.BIN (the third root record) at LBA 21, 394,896 B */
    mk_ip("SEGA SEGASATURN ", 0x8000, 0x06003000);
    memcpy(ip + 0x10, "SEGA ENTERPRISESMK-81200  V1.000", 32);
    memset(ip + 0x100, 0xA5, 0xE8B - 0x100);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 21, 394896, &p), 0);
    CHECK_INT(p.exec, 0x06003000); CHECK_INT(p.data, 0x06003000); CHECK_INT(p.size, 394896);
    CHECK_INT(p.fad, 171); CHECK_INT(p.ip_size, 0x8000);
    be32(ip + 0xF0, 0x26003000);                                                 /* the cache-through alias */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 21, 394896, &p), 0);
    CHECK_INT(p.data, 0x26003000);
    ip[0x1000] = 1;                                                              /* an IP byte under the program */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 21, 394896, &p), OM_CWX_E_SIZE);
    be32(ip + 0xF0, 0x06003000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 21, 394896, &p), OM_CWX_E_SIZE);
    /* LWRAM program: read to OM_CWX_HW_LO, copied by the tail, never reaching the tail */
    mk_ip("SEGA SEGASATURN ", 0x1000, 0x00200000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 0x20000, &p), 0);
    CHECK_INT(p.exec, 0x00200000); CHECK_INT(p.data, OM_CWX_HW_LO); CHECK_INT(p.fad, 190);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, OM_CWX_TAIL - 0x00200000, &p), OM_CWX_E_SIZE);   /* fits LWRAM, not HWRAM */
    mk_ip("SEGA SEGASATURN ", 0x1000, 0x002F0000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, OM_CWX_TAIL - 0x002F0000, &p), 0);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, OM_CWX_TAIL - 0x002F0000 + 4, &p), OM_CWX_E_SIZE);
    mk_ip("SEGA SEGASATURN ", 0x8000, 0x00200000);                               /* the copy at 0x06004000 is */
    memset(ip + 0x1F00, 0xA5, 0x100);                                            /* zeroed before the IP runs: */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 0x1000, &p), 0);              /* the IP must end by +0x2000 */
    ip[0x2000] = 1;
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 0x1000, &p), OM_CWX_E_SIZE);
    /* addresses */
    mk_ip("SEGA SEGASATURN ", 0x1000, 0x06004002);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_ADDR);
    mk_ip("SEGA SEGASATURN ", 0x1000, 0x00100000);                               /* not work RAM */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_ADDR);
    mk_ip("SEGA SEGASATURN ", 0x1000, 0x06100000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_ADDR);
    mk_ip("SEGA SEGASATURN ", 0x1000, 0);                                        /* no 1st Read File: nothing read */
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 999, &p), 0);
    CHECK_INT(p.exec, 0); CHECK_INT(p.size, 0);
    /* IP size */
    mk_ip("SEGA SEGASATURN ", 0, 0x06004000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_IPSIZE);
    mk_ip("SEGA SEGASATURN ", 0x8004, 0x0600C000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_IPSIZE);
    mk_ip("SEGA SEGASATURN ", 0x1002, 0x06004000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_IPSIZE);
    mk_ip("JUNK JUNK JUNK J", 0x1000, 0x06004000);
    CHECK_INT(om_cwx_plan_make(ip, sizeof ip, 40, 16, &p), OM_CWX_E_HEADER);
    CHECK_STR(om_cwx_strerror(OM_CWX_E_SIZE), "CWX: program does not fit in work RAM");
    CHECK(om_cwx_strerror(-1) == NULL);
    return th_done("test_cwx");
}
