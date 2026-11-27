/* menu/src/cheatpak.h -- CHEATS.PAK (contract/SPEC.md §5.15): every game's cheats, the user's on/off
 * choices and user-added cheats in one file, /aether/CHEATS.PAK. The data was converted once from cafe-alpha's
 * Pseudo Saturn Kai v6.545 database (tools/omsat/kaicheats.py; credited in CREDITS.md); this code is the project's
 * own (BSD-3-Clause). The PC side is tools/omsat/cheatpak.py.
 *
 * Layout: the §5.1 header (magic OMSATCHT), at 64 an index of 24-byte rows sorted by key (key[16], u32 record offset,
 * u32 record length), then the records (each 4-aligned, CRC-32 in its first word). Two rows may share a record.
 * The menu reads the header, the index in OM_CP_PIECE pieces (CRC-checked, scanned for the key) and one record, so it
 * needs OM_CP_STAGE bytes of 4-aligned staging whatever the number of games.
 *
 * om_ch_file is about 4.3 KB: callers keep it off the stack (OM_LW_STAGE in the menu, the launcher's heap). */
#ifndef OM_CHEATPAK_H
#define OM_CHEATPAK_H
#include <stdint.h>

#define OM_CH_MAX_CHEATS 64u
#define OM_CH_MAX_CODES  256u
#define OM_CH_MAX_NAME   30u
/* The universal cheat engine's hand-off cap: at most 30 enabled codes per launch, 8 B each (the editor enforces it). */
#define OM_CH_MAX_ACTIVE 30u

#define OM_CP_ROW      24u
#define OM_CP_REC_HDR  20u
#define OM_CP_MAX_REC  (OM_CP_REC_HDR + OM_CH_MAX_CHEATS * (3u + OM_CH_MAX_NAME) + OM_CH_MAX_CODES * 8u)  /* 4,180 B */
#define OM_CP_MAX_FILE 0x1C000u      /* 114,688 B: the editor holds the whole file in OM_LW_STAGE to save it */
#define OM_CP_PIECE    (85u * OM_CP_ROW)                   /* 2,040 B of index per read (libsatiator: <= 2 KB) */
#define OM_CP_STAGE    (OM_CP_MAX_REC + 4u)                /* om_cheats_load's staging (libsatiator reads words) */

/* record flags */
#define OM_CP_G_MASTER 0x01u         /* master_addr/master_value hold Kai's master code */
#define OM_CP_G_CWX    0x02u         /* the Kai-method cheats need the CWX loader */
#define OM_CP_G_JHL    0x04u         /* PSKai's autoset forces this game to JHL (so its Kai-method cheats cannot run) */
/* cheat flags */
#define OM_CH_ON       0x01u
#define OM_CH_USER     0x02u         /* added by the user, not from Kai */
#define OM_CH_HEADER   0x04u         /* a label row from Kai's menu: no codes, never on */

typedef struct {
    uint32_t addr;
    uint16_t value;
    uint8_t cmd;
} om_ch_code;

typedef struct {
    char name[OM_CH_MAX_NAME + 1];
    uint8_t flags;                 /* OM_CH_* */
    uint16_t first, count;         /* first/count index into om_ch_file.code[] */
} om_ch_cheat;

typedef struct {
    uint32_t master_addr, location;
    uint16_t master_value, cache4;  /* cache4: Kai's resident code cache, in 4-code units */
    uint8_t flags;                 /* OM_CP_G_* */
    uint16_t ncheats, ncodes;
    om_ch_cheat cheat[OM_CH_MAX_CHEATS];
    om_ch_code code[OM_CH_MAX_CODES];
} om_ch_file;

typedef struct { uint32_t count, data_off, data_end, table_crc; } om_cp_hdr;

/* The first 64 bytes: magic, version, layout (index at 64, OM_CP_ROW a row, data straight after), file <=
 * OM_CP_MAX_FILE. OM_OK or OM_E_MAGIC / OM_E_VERSION / OM_E_HDRSIZE / OM_E_TABLESIZE / OM_E_RANGE. */
int om_cp_header(const uint8_t *b, om_cp_hdr *h);
/* The row for key among n rows, or NULL. */
const uint8_t *om_cp_scan(const uint8_t *rows, uint32_t n, const char *key);
/* A row's record, checked against the header: OM_OK, or OM_E_RANGE (unaligned, outside the data, over OM_CP_MAX_REC). */
int om_cp_row(const om_cp_hdr *h, const uint8_t *row, uint32_t *off, uint32_t *len);
/* One record (len bytes) into *f: OM_OK; OM_E_CRC; OM_E_FIELD for a malformed one. *f is zeroed first. */
int om_cp_decode(const uint8_t *rec, uint32_t len, om_ch_file *f);
/* 2026-11-25: a record's cheats counted (the Launch Options row "<n> of <m> on"): *n the cheats (label rows not
 * counted), *on those on. The CRC and the cheats' table are checked as om_cp_decode checks them: OM_OK, OM_E_CRC or
 * OM_E_FIELD. */
int om_cp_count(const uint8_t *rec, uint32_t len, uint32_t *on, uint32_t *n);
/* *f as a record (for the editor): OM_OK, OM_E_FULL past cap, OM_E_FIELD for an invalid one. */
int om_cp_encode(const om_ch_file *f, uint8_t *out, uint32_t cap, uint32_t *n);
/* The 8-byte resident rows (address BE32, value BE16, command, 0) of every enabled cheat's codes, in order; at most cap. */
uint32_t om_ch_enabled_codes(const om_ch_file *f, uint8_t out8[][8], uint32_t cap);
/* Do cheats run for this launch: the launch panel's Cheats row (per_game, OM_PG_*) wins, else SETTINGS.INI cheats=. */
int om_ch_want(int global_on, uint8_t per_game);
#endif
