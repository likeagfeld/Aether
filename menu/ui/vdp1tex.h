/* menu/ui/vdp1tex.h -- the SGL texture table for the fixed VDP1 slots in omlayout.h. BSD-3-Clause. */
#ifndef OM_VDP1TEX_H
#define OM_VDP1TEX_H
#include "sgl.h"
enum { OM_TEX_HERO_A_SQ, OM_TEX_HERO_A_TALL, OM_TEX_HERO_B_SQ, OM_TEX_HERO_B_TALL, OM_TEX_JP_A, OM_TEX_JP_B,
       OM_TEX_PLACEHOLDER, OM_TEX_LOGO, OM_TEX_COUNT };
#define OM_TEX_HERO(slot, tall) ((slot) * 2 + (tall))
#define OM_TEX_JP(slot)         (OM_TEX_JP_A + (slot))
extern TEXTURE om_tex[OM_TEX_COUNT] __attribute__((aligned(4)));   /* see vdp1tex.c */
#endif
