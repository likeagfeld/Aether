/* menu/src/handoff.h -- menu -> LAUNCHER.BIN hand-off block (at OM_LW_HANDOFF) and the LAUNCHER.BIN
 * 4 KB file header (spec §7.1 launch steps 1-2). Native SH-2 struct; the header is big-endian. BSD-3-Clause. */
#ifndef HANDOFF_H
#define HANDOFF_H
#include <stdint.h>
#include "om_fmt.h"
#include "gameid.h"

#define OM_HANDOFF_MAGIC       "OMSATHND"
/* version 2 carries the resolved loader and the per-game options. A menu and a LAUNCHER.BIN built from
 * different versions refuse each other's block by name (om_handoff_check -> OM_E_VERSION; Rings issue #13). */
/* version 3 adds `ckey`, the game's GAMES.DB key (its CHEATS.PAK row): LAUNCHER.BIN loads the cheats
 * and runs the cheat editor, so the menu links no CHEATS.PAK code (HWRAM). */
/* 2026-11-24 (multi-disc, the lid swap): version 5 adds the set's discs (ndisc, cur, discs[]). With them, dir is the
 * discs' common parent folder and image the chosen disc's path from it; LAUNCHER.BIN writes out1..out<ndisc>.desc there
 * and emulates out<cur>.desc, so opening and closing the console's lid cycles the discs (Satiator firmware 161+). */
#define OM_HANDOFF_VERSION     5u
#define OM_HO_DISCS_MAX        9u      /* one digit in out<n>.desc */
#define OM_HO_DISC_POOL        1400u   /* the block stays under OM_LW_HANDOFF + 0x800 (the L1 harness's words) */
#define OM_HO_TEST_NO_SATIATOR 0x1u   /* l1_lbtest only: skip s_* calls, boot the disc in the drive */
#define OM_HO_TEST_ERRSCREEN   0x2u   /* l1_lbtest only: go straight to the error screen */
#define OM_HO_SPIKE            0x4u   /* Task 1's SPIKE.INI was honored (diagnostics only) */
/* the in-game reset (A+B+C+Start in a game returns to Aether) is armed unless this bit is set.
 * folders.c sets it from om_igr_want's own result (menu/src/loader.h): the global "Soft reset" setting, overridden
 * by an explicit per-game choice, overridden by GAMES.DB's OM_CF_NOIGR (gamesdb.h). Since v2 the block's own `igr`
 * byte is what the launcher reads: om_handoff_fill2 clears it when this flag is set. */
#define OM_HO_NO_IGR           0x8u
/* M6 Task 22, l1_lbtest only (emulator): step 1 runs the backup-RAM listing with its write/read/delete round trip
 * (menu/launcher/bramtest.h) and continues by itself. The menu never sets it. Only a launcher
 * built with OM_L1_HARNESS (the copy the L1 harness embeds, menu/launcher/out_l1) knows it; om_handoff_check in a
 * release LAUNCHER.BIN refuses a block carrying it, or any other bit outside OM_HO_RELEASE_FLAGS (OM_E_FIELD). */
#define OM_HO_TEST_BRAM        0x10u
/* user decision: the Kai/Rings import is optional: no game -- LAUNCHER.BIN runs the copy-only
 * import (pgsbak.h) with its progress and result, then resets to Aether. Settings > Import sets it. */
#define OM_HO_IMPORT           0x20u
/* Tidy saves is gone in 0.1: the flag stays only so LAUNCHER.BIN (lseq.c) refuses an older menu's Tidy request with a
 * message instead of treating it as a launch. Nothing sets it. */
#define OM_HO_TIDY             0x40u
/* Launch Options > Cheats -- LAUNCHER.BIN runs the cheat editor for `ckey` (image = the
 * game's shown name), saves CHEATS.PAK, then resets to Aether. No disc is booted. */
#define OM_HO_CHEATS           0x80u
/* Manage saves (Launch Options or Settings): LAUNCHER.BIN's save manager, then a reset to Aether. With a game id
 * (gid) it lists that game's saves only; with none, every save. No disc is booted. */
#define OM_HO_SAVEMGR          0x100u
#define OM_HO_RELEASE_FLAGS    (OM_HO_TEST_NO_SATIATOR | OM_HO_TEST_ERRSCREEN | OM_HO_SPIKE | OM_HO_NO_IGR | OM_HO_IMPORT                                 | OM_HO_TIDY | OM_HO_CHEATS | OM_HO_SAVEMGR)
#ifdef OM_L1_HARNESS
#define OM_HO_KNOWN_FLAGS      (OM_HO_RELEASE_FLAGS | OM_HO_TEST_BRAM)
#else
#define OM_HO_KNOWN_FLAGS      OM_HO_RELEASE_FLAGS
#endif

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t flags;
    char dir[256];          /* game folder, absolute, "/" for the root */
    char image[256];        /* image file name inside dir */
    uint8_t area_code;      /* SMPC area code (HWF-15) */
    uint8_t loader;         /* OM_LDR_CWX or OM_LDR_JHL (settings.h): resolved, never AUTO */
    uint8_t igr;            /* 1 = arm the in-game reset */
    uint8_t saves;          /* OM_HO_SAVES_*: per-game saves for this launch, and Move or Share */
    uint8_t slot;           /* 0..9 */
    uint8_t cheats;         /* 1 = arm the game's CHEATS.PAK record */
    uint8_t netlink;        /* 1 = carry SAVES/_NETLINK */
    uint8_t savetool;       /* 1 = a save tool: restore _GENERAL, arm nothing */
    char gid[OM_GID_MAX];   /* "<product>@<areas>" (gameid.h); "" when the IP is unknown */
    char ckey[17];          /* v3: the GAMES.DB key, NUL-terminated ("" = no key): the game's CHEATS.PAK row */
    uint8_t art_w, art_h;   /* v4: the game's cover in VDP1 VRAM (RGB555, w x h) the launcher's screens copy; 0 = none */
    uint16_t sty[3];        /* v4: the theme's text, highlight and accent (RGB555) for the launcher's screens */
    uint32_t art;           /* v4: that cover's VDP1 VRAM offset */
    uint8_t ndisc;          /* v5: 0 = one disc (out.desc); 2..9 = a multi-disc set (src/discset.h) */
    uint8_t cur;            /* v5: the chosen disc, 1..ndisc (0 with ndisc 0) */
    char discs[OM_HO_DISC_POOL];   /* v5: ndisc NUL-terminated paths from dir, in disc order; zeros after */
    uint32_t crc;           /* CRC-32 of every byte before this field */
} om_handoff;
typedef struct { uint8_t loader, igr, saves, slot, cheats, netlink, savetool; char gid[OM_GID_MAX]; char ckey[17];
                 uint8_t art_w, art_h; uint16_t sty[3]; uint32_t art;
                 const void *set_lib; uint16_t set_idx; }   /* v5: an om_libidx and the entry, when it is in a set */
    om_launch_opts;
/* the saves byte. On = per-game saves (pgsrun.h); Move/Share is SETTINGS.INI saves_first (open decision 9:
 * Share gives a game's first use a copy of the general set). The layout and OM_HANDOFF_VERSION are unchanged: v2 already
 * carried the byte, only its value range grew (om_handoff_check refuses > 2). */
enum { OM_HO_SAVES_OFF = 0, OM_HO_SAVES_MOVE = 1, OM_HO_SAVES_SHARE = 2 };

/* Fills a version-2 block. OM_E_FIELD: a path over 255 B, o NULL, o->loader not CWX/JHL, o->slot > 9, o->saves > 2
 * or an unterminated o->gid or o->ckey. flags & OM_HO_NO_IGR clears igr. */
int om_handoff_fill2(om_handoff *h, const char *dir, const char *image, uint8_t area_code, uint32_t flags,
                     const om_launch_opts *o);
int om_handoff_check(const om_handoff *h);
/* v5: the k-th disc's path (1-based) in a checked block, NULL when there is none. */
const char *om_handoff_disc(const om_handoff *h, uint32_t k);   /* OM_E_MAGIC, OM_E_VERSION (another version), OM_E_CRC, OM_E_FIELD
                                              * (also: a flag outside OM_HO_KNOWN_FLAGS) */

/* 2026-11-25 (hardware: B in the cheat editor reset the console, so the user sat through the whole boot again): the
 * return block, at OM_LW_RETURN in LWRAM (outside the hand-off block, the L1 harness's words and every LW region the
 * menu loads at boot; LWRAM survives the BIOS boot and SYSRES). The menu writes it, unarmed, when it hands off to a tool
 * (the cheat editor, Manage saves, the import): where the list was and which panel row was open. The tool arms it on
 * its way back (om_ret_arm) and copies it to sound RAM (OM_SND_RETURN): the BIOS's CD boot clears both work RAMs
 * (measured on Mednafen: LWRAM and HWRAM all zero once the BIOS loads the disc) and leaves sound RAM above 0x40000 as
 * it was. The menu's next start copies it out, clears it there, and (om_ret_take) armed and intact skips the intro and
 * reopens there. Anything else -- no block, a bad CRC, unarmed (a game launch's in-game reset, the console's Reset
 * button, a tool that never returned) -- is an ordinary start. */
#define OM_RET_MAGIC   "OMSATRET"
#define OM_RET_VERSION 1u
#define OM_RET_LOCS    12u          /* the browse stack (tree.h OM_MAX_DEPTH + 2) and the location itself */
typedef struct { uint8_t kind, vroot; uint16_t arg; } om_ret_loc;   /* browse.h om_loc, field for field */
typedef struct {
    char magic[8];
    uint32_t version;
    uint8_t armed;                  /* 0: written at the hand-off; 1: the tool returned */
    uint8_t ov_kind, ov_page;       /* overlay.h: the panel open at the hand-off (OM_OV_GAME or OM_OV_SETTINGS) */
    uint8_t depth;                  /* loc[0..depth-1]: the browse stack; loc[depth]: the location */
    uint16_t ov_row, ov_top;
    uint16_t entry;                 /* the game's LIBRARY.IDX index (0xFFFF: none) */
    uint16_t cur, top;              /* the list's cursor and first row */
    char key[17];                   /* the game's GAMES.DB key ("": none), checked against the entry on return */
    uint8_t pad;
    om_ret_loc loc[OM_RET_LOCS];
    uint32_t crc;                   /* CRC-32 of every byte before this field */
} om_retblk;
void om_ret_seal(om_retblk *r);                 /* magic, version, CRC */
int om_ret_ok(const om_retblk *r);              /* 1: magic, version, depth and CRC hold */
int om_ret_arm(om_retblk *r);                   /* the tool's way back: 1 armed (resealed), 0 no intact block */
/* the menu's start: a copy in *out and the block cleared; 1 = intact and armed (reopen there), else 0 */
int om_ret_take(om_retblk *blk, om_retblk *out);
/* the block to or from sound RAM, one 16-bit word at a time (HWF-33); src NULL: zeros */
void om_ret_copy16(volatile void *dst, const volatile void *src);

#define OM_LNCH_MAGIC "OMSATLNC"
#define OM_LNCH_HDR   0x1000u
typedef struct { uint32_t body_size, body_crc; } om_lnch_info;
int om_lnch_parse(const uint8_t *hdr, uint32_t file_size, om_lnch_info *out);
void om_lnch_make(uint8_t *hdr, uint32_t body_size, uint32_t body_crc);
#endif
