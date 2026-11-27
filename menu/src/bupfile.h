/* menu/src/bupfile.h -- the per-game save file: one backup-RAM record as a Pseudo Saturn Kai ".BUP" file
 * (contract/SPEC.md §5.16; M6 Task 22). File-compatible with Kai and Save Game Copier: a 64-byte big-endian "Vmem"
 * header, then the record's data. The layout is Kai's vmem_bup_header_t (cafe-alpha, pseudosat_kai/vmem/vmem_defs.h:
 * 82-135, re-implemented from docs/research/m6-research.md §5, not copied), checked against a real Kai file there.
 * The directory fields are the BIOS backup library's BupDir (HWF-72), so a record listed by BUP_Dir encodes as is.
 *
 * Slots (Tasks 23-24): ten per game, Kai's layout (m6-research.md §1.3): slot 0 in the game's folder, slots 1-9 in
 * sub-folders "01".."09" (om_bup_slot_path). Portable C99, host-tested (test_bupfile.c); the launcher's BRAM layer
 * (menu/launcher/bram.h) and, later, the menu share it. BSD-3-Clause. */
#ifndef OM_BUPFILE_H
#define OM_BUPFILE_H
#include <stdint.h>

#define OM_BUP_HDR     64u
#define OM_BUP_MAGIC   "Vmem"
#define OM_BUP_NAME    12u           /* 11 characters + NUL (HWF-72) */
#define OM_BUP_COMMENT 11u           /* 10 characters + NUL, raw bytes (Shift-JIS in real files) */
#define OM_BUP_SLOTS   10u           /* slot 0..9 */

/* The library's return codes (HWF-72; SEGA_BUP.H:41-48), so portable code can name them */
#define OM_BUP_NON         1         /* not connected */
#define OM_BUP_UNFORMAT    2
#define OM_BUP_WPROTECT    3
#define OM_BUP_NOMEM       4         /* not enough memory: BUP_Write on a full device (SMPBUP1.C:389) */
#define OM_BUP_NOTFOUND    5
#define OM_BUP_FOUND       6         /* a same-name record exists (BUP_Write with owsw ON) */
#define OM_BUP_NOMATCH     7         /* BUP_Verify: the data differ */
#define OM_BUP_BROKEN      8

/* Field order and sizes are BupDir's (SEGA_BUP.H:71-78): bram.c checks the layouts are identical. blocks is
 * informational and can be 0 in a file (13 of Kai's release files): size a restore with BUP_Stat
 * (om_bram_info_get(datasize)), never with blocks. */
typedef struct { char name[12]; char comment[11]; uint8_t language; uint32_t date, datasize; uint16_t blocks; } om_bup_dir;

/* OM_E_FIELD when d->name fails om_bup_name_norm (the name is written NUL-padded). save_id/stats/unused are 0; the
 * date goes to 0x28 and 0x34. */
int om_bup_encode_hdr(const om_bup_dir *d, uint8_t out[OM_BUP_HDR]);
/* in: the whole file (len bytes). OM_E_SHORT: under 64 B, or under 64 + datasize; OM_E_MAGIC: not "Vmem";
 * OM_E_FIELD: the name is empty or not printable ASCII. Bytes after the name's NUL, and byte 11 (Save Game Copier
 * writes '.' there after an 11-character name), are not checked and come back as NUL. The date at 0x34 is not compared with 0x28. */
int om_bup_decode_hdr(const uint8_t *in, uint32_t len, om_bup_dir *d);
int om_bup_name_ok(const char name[12]);      /* 1: 1..11 printable ASCII (0x20-0x7E), then NUL to byte 11 */
/* 1 when in holds 1..11 printable bytes up to a NUL or byte 11 (whatever follows): out = that name, NUL-padded (out may be
 * in). A record listed by the BIOS goes through this before it is written anywhere. */
int om_bup_name_norm(char out[12], const char in[12]);

/* "<base>/<folder>/<name>.BUP" for slot 0, "<base>/<folder>/0<slot>/<name>.BUP" for 1..9. OM_E_RANGE: slot > 9;
 * OM_E_FIELD: a bad name (om_bup_name_norm) or a name byte FAT forbids (" * / : < > ? \ |); OM_E_FULL: over cap. */
int om_bup_slot_path(char *out, uint32_t cap, const char *base, const char *folder, uint32_t slot,
                     const char name[12]);

/* SPEC §5.16: a reader keys a folder's files on the record name in their headers, never on
 * the file name (89 of the 326 files in Kai's release SAVES tree are named otherwise). When two files in one folder
 * hold the same record, 1 = file a (header a, file name fa) wins over b: the later record date (0x28, the game's own
 * BUP_SetDate stamp); then the higher save_id (0x04, Kai's counter; 0 from Save Game Copier and from us); then the
 * file named "<record>.BUP" (ASCII case-insensitive); then the smaller file name in byte order. Both headers must have
 * decoded (om_bup_decode_hdr == OM_OK). The same rule as tools/omsat/bupfile.py prefer. */
int om_bup_prefer(const uint8_t *a, const char *fa, const uint8_t *b, const char *fb);

/* The launcher's error-screen line: "BRAM: NOT FORMATTED", "BRAM: FULL", ... for the codes above, else
 * "BRAM: <op> FAILED rc=<rc>". Always NUL-terminated within cap. */
void om_bup_msg(char *out, uint32_t cap, const char *op, int rc);
/* The BIOS Memory Manager's "Size" of a record (TechBulletins #9: ceil((bytes + 32) / 64)) */
uint32_t om_bup_mm_size(uint32_t bytes);
#endif
