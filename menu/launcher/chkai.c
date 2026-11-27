/* menu/launcher/chkai.c -- see chkai.h. BSD-3-Clause. */
#include <string.h>
#include "chkai.h"
#include "om_fmt.h"

#define HW_LO   0x06000000u
#define HW_HI   0x06100000u
#define LW_LO   0x00200000u
#define LW_HI   0x00300000u
#define STUB    0x06002800u            /* igr.h OM_IGR_STUB .. OM_IGR_STUB_END */
#define STUB_E  0x06002D48u
#define LOW_E   0x06004000u            /* BIOS work area, vectors, the IGR guard, the dispatcher, stacks, the IP */
#define PLACE_LO 0x06000B00u           /* Kai's lowest location: just past the BIOS dispatcher table (0x06000900+4v) */
#define STACKS_LO 0x06000D00u          /* the BIOS's slave and master stacks, up to the IP: the CWX tail and the security
                                        * code run on them, so no block goes there */

static int in(uint32_t a, uint32_t lo, uint32_t hi) { return a >= lo && a < hi; }
static int over(uint32_t a, uint32_t n, uint32_t lo, uint32_t hi) { return a < hi && a + n > lo; }

int om_chk_code_ok(uint32_t addr, uint8_t cmd)
{
    if (cmd != 0x00 && cmd != 0x01 && cmd != 0x03 && cmd != 0x0D)
        return OM_CHV_CMD;
    if (!in(addr, HW_LO, HW_HI) && !in(addr, LW_LO, LW_HI))
        return OM_CHV_RANGE;
    if (cmd != 0x03 && (addr & 1u))
        return OM_CHV_ODD;
    if (in(addr, OM_RES_BASE, STUB_E))
        return OM_CHV_BLOB;
    if (in(addr, HW_LO, LOW_E))
        return OM_CHV_GUARD;
    return OM_CHV_OK;
}

const char *om_chk_why(int v)
{
    switch (v) {
    case OM_CHV_OK:    return "OK";
    case OM_CHV_CMD:   return "TYPE MUST BE 00, 01, 03 OR 0D";
    case OM_CHV_RANGE: return "NOT GAME RAM (06xxxxxx OR 002xxxxx)";
    case OM_CHV_ODD:   return "16-BIT CODE NEEDS AN EVEN ADDRESS";
    case OM_CHV_GUARD: return "06000000-06003FFF IS BIOS/BOOT AREA";
    case OM_CHV_BLOB:  return "IN GAME RESET/CHEAT AREA, NOT GAME";
    default:           return "BAD CODE";
    }
}

uint32_t om_chk_place(uint32_t location)
{
    uint32_t a = location;
    if (!a || in(a, STUB, STUB_E))
        return OM_RES_BASE;
    /* Kai's ct_config_t starts with its bootstrap, whose two mov.l @(disp,pc) literals sit at +40/+44: the block needs
     * long-word alignment only (the brief asked for 16; 4 keeps Kai's 0x0602B308, Virtual Hydlide JP) */
    if ((a & 3u) || a < PLACE_LO || a + OM_CHK_MAX > HW_HI || over(a, OM_CHK_MAX, STACKS_LO, 0x06002000u))
        return 0;
    if (a != OM_RES_BASE && over(a, OM_CHK_MAX, 0x06002000u, LOW_E))   /* the IP and the stub: only our block */
        return 0;
    return a;
}

uint16_t om_chk_master_opc(uint16_t value)
{
    return (value & 0xFFF0u) == 0xC300u ? value : (uint16_t)0xC305u;   /* trapa #0..15: the vector is value & 0xF */
}

uint32_t om_chk_cap(const om_ch_file *f)
{
    uint32_t place = om_chk_place(f->location), kai = 4u * f->cache4;
    if (place == OM_RES_BASE || kai >= OM_CH_MAX_ACTIVE)
        return OM_CH_MAX_ACTIVE;
    return kai;
}

uint32_t om_chk_active(const om_ch_file *f)
{
    uint32_t i, n = 0;
    for (i = 0; i < f->ncheats; i++)
        if (f->cheat[i].flags & OM_CH_ON)
            n += f->cheat[i].count;
    return n;
}

void om_chk_default_master(om_ch_file *f)
{
    if (f->flags & OM_CP_G_MASTER)
        return;
    f->master_addr = OM_CHK_DEF_MASTER;
    f->master_value = OM_CHK_DEF_OPC;
    f->location = 0;
    f->cache4 = 8;
    f->flags |= OM_CP_G_MASTER | OM_CP_G_CWX;
}

static int master_ok(uint32_t a, uint32_t place)
{
    if ((a & 1u) || (!in(a, HW_LO, HW_HI) && !in(a, LW_LO, LW_HI)))
        return 0;
    if (in(a, HW_LO, 0x06000400u) || in(a, 0x06002000u, LOW_E))      /* vectors, cells, the IGR guard, IP, stub */
        return 0;
    return !in(a, place, place + OM_CHK_MAX);
}

int om_chk_plan_make(const om_ch_file *f, om_chk_plan *p)
{
    uint32_t i, j, cap = om_chk_cap(f), used = 0;
    memset(p, 0, sizeof *p);
    if (!(f->flags & OM_CP_G_MASTER))
        return p->status = OM_CHS_NOREC;
    if (f->flags & OM_CP_G_JHL)
        return p->status = OM_CHS_JHL_ONLY;
    if (!(p->place = om_chk_place(f->location)) || !master_ok(f->master_addr, p->place))
        return p->status = OM_CHS_NOTSUP;
    p->master = f->master_addr;
    p->opc = om_chk_master_opc(f->master_value);
    p->vec = (uint8_t)(p->opc & 0xFu);
    for (i = 0; i < f->ncheats; i++) {
        const om_ch_cheat *c = &f->cheat[i];
        int ok = 1;
        if (!(c->flags & OM_CH_ON) || !c->count)
            continue;
        if (used + c->count > cap)
            break;                                     /* Kai: the cache is full, no later cheat goes in */
        for (j = 0; j < c->count; j++) {
            const om_ch_code *k = &f->code[c->first + j];
            if (om_chk_code_ok(k->addr, k->cmd) || in(k->addr, p->place, p->place + OM_CHK_MAX))
                ok = 0;
        }
        if (!ok)
            continue;                                  /* a refused code drops its whole cheat */
        for (j = 0; j < c->count; j++) {
            const om_ch_code *k = &f->code[c->first + j];
            if (k->cmd == 0x00)
                p->boot[p->nboot++] = *k;
            else if (k->cmd == 0x0D) {
                p->en_addr = k->addr;                  /* only the last enable code counts (cheat.c:2691-2702) */
                p->en_val = k->value;
            } else
                p->cache[p->ncache++] = *k;
        }
        used += c->count;
    }
    if (!p->ncache && !p->nboot)
        return p->status = OM_CHS_NONE;
    p->len = OM_CHK_CFG + 8u * p->ncache + OM_CHK_HDLR;
    return p->status = OM_CHS_ARMED;
}

uint32_t om_chk_build(uint8_t *img, uint32_t cap, const om_chk_plan *p, const uint8_t *boot, uint32_t boot_len,
                      const uint8_t *hdlr, uint32_t hdlr_len)
{
    uint32_t i, len = OM_CHK_CFG + 8u * p->ncache + OM_CHK_HDLR, h = OM_CHK_CFG + 8u * p->ncache;
    if (len > cap || boot_len != OM_CHK_BOOT || hdlr_len > OM_CHK_HDLR || p->ncache > OM_CH_MAX_ACTIVE)
        return 0;
    memset(img, 0, len);
    memcpy(img, boot, boot_len);
    om_put16(img + OM_CHK_SLOT, p->saved);
    om_put32(img + OM_CHK_P_HDLR, p->place + h);
    om_put32(img + OM_CHK_P_CFG, p->place);
    om_put32(img + OM_CHK_EN_ADDR, p->en_addr);
    om_put16(img + OM_CHK_EN_VAL, p->en_val);
    om_put16(img + OM_CHK_COUNT, p->ncache);
    for (i = 0; i < p->ncache; i++) {
        om_put32(img + OM_CHK_CFG + 8u * i, p->cache[i].addr);
        om_put16(img + OM_CHK_CFG + 8u * i + 4u, p->cache[i].value);
        img[OM_CHK_CFG + 8u * i + 6u] = p->cache[i].cmd;
    }
    memcpy(img + h, hdlr, hdlr_len);
    return len;
}

uint32_t om_chk_xlat(uint32_t a, uint32_t exec, uint32_t data, uint32_t size)
{
    if (exec != data && size && a >= exec && a - exec < size)
        return data + (a - exec);
    return a;
}

/* The master's instruction runs out of place, in Kai's bootstrap: after the handler (T clobbered), with r15 on the
 * trapa's frame, at another PC, and an rte puts the game's SR (its T) back. So it must not read or set T or SR, use
 * r15 or the PC, branch, or sit in a branch's delay slot (prev: the instruction before it). SH-2 opcode map,
 * SH7604 programming manual. 1 = safe. */
int om_chk_op_safe(uint16_t op, uint16_t prev)
{
    uint32_t hi = op >> 12, n = (op >> 8) & 15u, m = (op >> 4) & 15u, lo = op & 15u, lb = op & 0xFFu;
    uint32_t ph = prev >> 12, pl = prev & 0xFFu;
    if (ph == 0xA || ph == 0xB || (ph == 4 && (pl == 0x0B || pl == 0x2B)) || prev == 0x000B || prev == 0x002B ||
        (ph == 0 && (pl & 0xDFu) == 0x03u) || (prev >> 8) == 0x8D || (prev >> 8) == 0x8F)
        return 0;                                          /* a delayed branch: op is its slot */
    switch (hi) {
    case 0x0:
        if (n == 15 || m == 15 || lo == 3 || lb == 0x0B || lb == 0x2B || lb == 0x1B || lb == 0x08 || lb == 0x18 ||
            lb == 0x19 || lb == 0x29 || lb == 0x02)
            return 0;
        return 1;
    case 0x1: case 0x5:
        return n != 15 && m != 15;
    case 0x2:
        return n != 15 && m != 15 && lo != 7 && lo != 8 && lo != 0xC;
    case 0x3:
        return n != 15 && m != 15 && (lo == 5 || lo == 8 || lo == 0xC || lo == 0xD);
    case 0x4:
        if (lo == 0xF) return n != 15 && m != 15;          /* mac.w */
        if (n == 15) return 0;
        switch (lb) {
        case 0x00: case 0x01: case 0x04: case 0x05: case 0x10: case 0x11: case 0x15: case 0x1B: case 0x20: case 0x21:
        case 0x24: case 0x25: case 0x0B: case 0x2B: case 0x0E: case 0x07: case 0x03:
            return 0;
        default:
            return 1;
        }
    case 0x6:
        return n != 15 && m != 15 && lo != 0xA;
    case 0x7: case 0xE:
        return n != 15;
    case 0x8:
        if (n == 0 || n == 1) return m != 15;              /* mov.b/w R0,@(disp,Rn) */
        if (n == 4 || n == 5) return m != 15;              /* mov.b/w @(disp,Rm),R0 */
        return 0;                                          /* cmp/eq #imm (T), the conditional branches */
    case 0xC:
        return n != 3 && n != 7 && n != 8 && n != 0xC;     /* not trapa, mova, tst #imm, tst.b */
    default:
        return 0;                                          /* 9, D: PC-relative; A, B: branches; F: none on the SH-2 */
    }
}

uint32_t om_chk_arm(om_chk_plan *p, const om_chk_mem *m, uint32_t exec, uint32_t data, uint32_t size, uint8_t *img,
                    uint32_t cap, const uint8_t *boot, uint32_t boot_len, const uint8_t *hdlr, uint32_t hdlr_len)
{
    uint32_t i, n;
    uint32_t ma = om_chk_xlat(p->master, exec, data, size);
    if (p->status != OM_CHS_ARMED)
        return 0;
    p->saved = m->rd16(ma);
    if (!om_chk_op_safe(p->saved, m->rd16(ma - 2u)) ||                      /* the master's instruction runs moved */
        over(p->place, p->len, exec, exec + size) || (data != exec && over(p->place, p->len, data, data + size))) {
        p->status = OM_CHS_NOTSUP;                     /* no trapa, no block, no code written */
        return 0;
    }
    for (i = 0; i < p->nboot; i++) {                   /* cheat_codes.c:372-440: once, at boot */
        uint32_t a = p->boot[i].addr;
        /* LWRAM holds the launcher until the CWX tail clears it: only an LWRAM program's own bytes take a code there;
         * an HWRAM code inside an LWRAM program's temporary copy would move with it */
        if (in(a, LW_LO, LW_HI) ? !(in(exec, LW_LO, LW_HI) && a - exec < size && a >= exec)
                                : (data != exec && a >= data && a - data < size))
            continue;
        m->wr16(om_chk_xlat(a, exec, data, size), p->boot[i].value);
    }
    if (!(n = om_chk_build(img, cap, p, boot, boot_len, hdlr, hdlr_len)))
        p->status = OM_CHS_ERR;
    return n;
}
