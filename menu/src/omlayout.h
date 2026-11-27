/* menu/src/omlayout.h -- memory and VRAM layout (docs/ledger.md "M2 memory plan"). BSD-3-Clause. */
#ifndef OMLAYOUT_H
#define OMLAYOUT_H
#define OM_CT(a)               ((a) | 0x20000000u)    /* cache-through alias, HWF-12 */
#define OM_HW_IMAGE_START      0x06004000u            /* 1st Read address, HWF-20 */
#define OM_HW_END_MAX          0x060F0000u            /* SGL work area start (menu/runtime/workarea.c), HWF-12 */
#define OM_LW_GAMESDB          0x00200000u            /* HWF-12 */
#define OM_LW_GAMESDB_MAX      0x0003C000u            /* 0x40000 - the 16 KB VRTILES region above it */
#define OM_LW_VRTILES          0x0023C000u            /* HWF-12; docs/ledger.md: VRTILES.BIN, with GAMES.DB */
#define OM_LW_VRTILES_MAX      0x00004000u            /* ends at OM_LW_ARTTAB; reloaded with it after a launch */
#define OM_LW_ARTTAB           0x00240000u            /* HWF-12 */
#define OM_LW_ARTTAB_MAX       0x0001E000u            /* 0x20000 - the 8 KB BIGLOGO region above it */
#define OM_LW_BIGLOGO          0x0025E000u            /* HWF-12; docs/ledger.md: BIGLOGO.BIN, from the disc */
#define OM_LW_BIGLOGO_MAX      0x00002000u            /* ends at OM_LW_STAGE; read once a boot, never reloaded */
#define OM_LW_STAGE            0x00260000u            /* HWF-12 */
#define OM_LW_STAGE_MAX        0x0001F000u
#define OM_LW_LBG              0x00268800u            /* docs/ledger.md: LOADBG.BIN's LW region, the */
#define OM_LW_LBG_MAX          0x00016800u            /* stage's top 92,160 B (+0x8800..+0x1F000), checked by a hash */
#define OM_LW_HANDOFF          0x0027F000u            /* HWF-12 */
#define OM_LW_RETURN           0x0027FC00u            /* handoff.h om_retblk: the hand-off block's page, past the L1
                                                         harness's words (+0x800..+0x81F, l1gate.h) */
#define OM_SND_RETURN          0x25A60000u            /* handoff.h om_retblk across the BIOS boot: sound RAM, cache-through,
                                                         16-bit access (HWF-33); outside the music, SFX and DSP regions
                                                         (audio.c). The loading screen borrows 0x40000-0x77FFF, so the
                                                         block lives there only from a tool's exit to the menu's first
                                                         instruction (main.c copies it out before any audio runs) */
#define OM_LW_SNAPSHOT         0x00280000u            /* HWF-12 */
#define OM_LAUNCHER_LOAD       0x00200000u            /* HWF-12 */
#define OM_LAUNCHER_MAX        0x0007F000u            /* load address .. hand-off block */
#define OM_LAUNCHER_BODY_CAP   (OM_LW_LBG - OM_LAUNCHER_LOAD)   /* the menu reads no body past LOADBG's LW */
#define OM_LAUNCHER_HOP_MIN    0x00201000u            /* get_mpeg_rom(2,2,0x200000) overwrites 4 KB, HWF-19 */
#define OM_LAUNCHER_STACK_TOP  0x0027F000u            /* HWF-12 */
#define OM_VDP1_VRAM           0x05C00000u            /* HWF-12 */
#define OM_VDP1_BYTES          0x00080000u            /* 512 KB, HWF-23 */
#define OM_VRAM_HERO_A         0x10000u               /* CGADDRESS, HWF-08 */
#define OM_VRAM_HERO_B         0x1C800u
#define OM_VRAM_JP_A           0x29000u
#define OM_VRAM_JP_B           0x29A00u
#define OM_VRAM_LUT            0x2A400u               /* 16 x 16-bit, HWF-04 */
#define OM_VRAM_PLACEHOLDER    0x2A420u               /* SENTINEL ONLY since review-13a: not uploaded; its range is now BOOTANIM, VERTAG, FOOTLOGO */
#define OM_VRAM_BOOTANIM       0x2A420u               /* Task 13b, boot only: 2 x 176x128 8 bpp = 45,056 B, ends 0x35420 */
#define OM_VRAM_SAVER          OM_VRAM_BOOTANIM       /* the saver's 152x101 RGB555 logo (30,704 B); shared with the
                                                         boot animation, the interstitial and the loading screen's reflection,
                                                         all idle while the menu runs; re-uploaded at every saver start */
#define OM_VRAM_VERTAG_L       0x35420u               /* the startup "FARKUS 0.1" tag (menu/ui/vertag.c): its LUT (32 B), */
#define OM_VRAM_VERTAG_T       0x35440u               /* then its 4 bpp texture; outside the boot-only range both boot */
#define OM_VRAM_VERTAG_END     0x35640u               /* screens overwrite (0x35420..0x36C1F was free: placeholder tail) */
#define OM_VRAM_LOGO           0x35640u               /* header logo, 40x25 RGB555 = 2,000 B, ..0x35E0F (was 32x22 at 0x36C20;
                                                         2026-11-25: the free gap up to FOOTLOGO holds the bigger mark;
                                                         0x35E10..0x3694F and 0x36C20..0x371FF are now free) */
#define OM_VRAM_FOOTLOGO       0x36950u               /* the loading footer's logo, 24x15 RGB555 = 720 B, ends 0x36C1F */
#define OM_VRAM_UI             0x37200u               /* Task 13a UI pool: text/icon textures + their LUTs (uitext.c) */
#define OM_VRAM_UI_END         0x60000u               /* 167,424 B; LUTs on 0x20 boundaries (VDP1_Manual.txt 5.2) */
#define OM_VRAM_GOUR           0x7FE00u               /* Gouraud tables, 8 B each on 8 B boundaries (VDP1_Manual 5.3);
                                                         moved from 0x60000 to just below SGL's GouraudRAM */
#define OM_VRAM_GOUR_N         32u
/* the cover cache's extra slots (menu/ui/art.c), each a 51,200 B hero + a 2,560 B JP bitmap, stacked down
 * from OM_VRAM_GOUR: C0 and C1 lie above the UI pool (0x60000) in free VRAM; C2 reaches into the pool's unused top and
 * is claimed at run time only if the pool ends at or below it (uitext.c om_ut_pool_cap). */
#define OM_VRAM_CACHE_SLOT     0xD200u                /* 51,200 + 2,560 */
#define OM_VRAM_CACHE0         0x72C00u               /* .. 0x7FDFF */
#define OM_VRAM_CACHE1         0x65A00u               /* .. 0x72BFF */
#define OM_VRAM_CACHE2         0x58800u               /* .. 0x659FF */
#define OM_VRAM_END            0x7FF00u               /* the menu ends here; SGL GouraudRAM 0x7FF00.. (HWF-08) untouched */
#define OM_HERO_PIXELS         25600u                 /* 160x160 = 128x200 */
#define OM_JP_CANVAS_W         160u
#define OM_JP_CANVAS_H         32u
#define OM_LOGO_W              40u                    /* multiple of 8 (HWF-06 HVsize); 2026-11-25: was 32x22, the mark */
#define OM_LOGO_H              25u                    /* lost its circle; 25 = the header's rows 8..32 above the hairline */
#define OM_FOOTLOGO_W          24u                    /* 22x15 (layout.h LD_LOGO) + 2 transparent columns (HWF-06) */
#define OM_FOOTLOGO_H          15u
#define OM_SNDRAM              0x25A00000u            /* sound RAM, cache-through, 512 KB, 16-bit access (HWF-33) */
#define OM_CRAM                0x25F00000u            /* VDP2 colour RAM, word n at +2n (HWF-09, HWF-30) */
#define OM_V2_N2CEL            0x25E00000u            /* VRAM A0: NBG2 panel cells 0..2, 32 B each (plan D4, HWF-29) */
#define OM_V2_N2MAP            0x25E02000u            /* A0 + 0x2000: NBG2 map, one 1-word 64x64 page (HWF-09) */
#define OM_V2_BMP              0x25E40000u            /* VRAM B0: NBG1 512x256 8-bpp bitmap, 0x20000 B (HWF-28) */
#define OM_V2_BMP_PAL          1u                     /* bitmap palette 1 -> CRAM words 256..511 (HWF-30) */
#define OM_V2_N2PAL            2u                     /* NBG2 palette 2 -> CRAM words 32..47 (HWF-30) */
#define OM_V2_CYC_A0           0x26EEEEEEu            /* N2 PN, N2 CP, CPU (HWF-29) */
#define OM_V2_CYC_A1           0xEEEEEEEEu            /* HWF-29 */
#define OM_V2_CYC_B0           0x55EEEEEEu            /* N1 CP x2 (256 colours), CPU (HWF-29) */
#define OM_V2_CYC_B1           0x044EEEEEu            /* N0 PN, N0 CP x2: SGL's NBG0 is 256-colour (HWF-29 corrected, HWF-41) */
#endif
