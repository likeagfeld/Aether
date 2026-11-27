/* menu/src/sdwrite.h -- the only SD write path in M6 (Global Constraints, "SD writes"; M6 Task 21). Menu and launcher.
 * Portable: host tests run it over menu/host-tests/fake_satiator.c.
 *
 * Every write: one file open at a time; chunks of OM_SDW_CHUNK (2,044 B: a multiple of 4 below libsatiator's
 * 2,048 B S_MAXBUF, HWF-22, until hardware session H2 shows exact 2,048 B writes are safe on FW163, rings_vs_kai §4
 * test 3); every chunk except the last is a multiple of 4 (Kai v6.455's odd-length bug); s_sync after every chunk and
 * gap() between chunks (Kai's freeze on consecutive SD writes, PSKai v6.472; Rings' double V-blank); a failed chunk is
 * retried once
 * after a gap (seek back, write, sync), then the call fails with that rc. With verify, the file is read back in 2 KB
 * pieces into scratch2k and its length and CRC-32 compared with what was written (OM_E_CRC on a mismatch).
 *
 * Durability (unverified, from upstream's comment only): s_sync is libsatiator's s_seek(fd, 0, C_SEEK_CUR); whether
 * that flushes the firmware's FatFs buffers on FW163 is not known. s_close is what the atomic replace relies on, so
 * om_sdw_write_file (create/truncate in place) is NOT power-cut safe; om_sdw_replace is.
 *
 * The verify read-back proves the firmware's view of the file, which its own cache can satisfy; that it reached the
 * card rests on s_close (FatFs f_close -> f_sync, unverified on FW163) as above (review M8).
 *
 * om_sdw_replace, the write-then-replace (a power cut keeps the previous file):
 *   0. finish an interrupted replace, only when path is absent (s_stat): s_rename("<path>.NEW", path); any error
 *      other than "absent" ends the call before anything is written (fix round 1, review I1/I3: so the rule never
 *      depends on how the firmware treats rename-onto-existing, and a failed recovery never truncates the only copy);
 *   1. make sure path exists (FA_OPEN_ALWAYS, never truncates), so path is present for the whole write;
 *   2. write and verify "<path>.NEW" (a failure unlinks it; path untouched);
 *   3. gap, s_unlink(path);   4. gap, s_rename("<path>.NEW", path).
 * RECOVERY RULE (om_sdw_read_file, and step 0 of the next replace): path wins whenever it exists -- a "<path>.NEW"
 * next to it is an interrupted write and is overwritten by the next replace. path absent means step 3 completed, which
 * happens only after .NEW verified, so "<path>.NEW" is then the latest complete file and is read instead.
 * The guarantee holds at API-call granularity (review M1): FAT has no journal, so a cut inside the firmware's own
 * f_rename/f_unlink (directory entry written, old one not yet removed) is a window this API cannot see or repair.
 *
 * Names: libsatiator walks file names as uint32_t* (satiator.c buffer_xfer), so each name is staged into scratch2k
 * (open/unlink/stat) or into buf (om_sdw_read_file) just before the call; a path over 250 B is refused (OM_E_FIELD). Error codes are
 * om_sd_err's: a libsatiator FRESULT fr becomes -100 + fr (OM_SDW_NOFILE: absent), plus OM_E_SHORT/CRC/FULL/FIELD.
 * BSD-3-Clause. */
#ifndef OM_SDWRITE_H
#define OM_SDWRITE_H
#include <stdint.h>

#define OM_SDW_CHUNK      2044u   /* bytes per s_write; one constant (H2 decides whether it becomes 2048) */
#define OM_SDW_PIECE_MAX  512u    /* a source's largest indivisible piece (a FAVS/RECENT line is <= 256 B) */
#define OM_SDW_ERR(fr)    ((fr) < 0 ? (fr) - 100 : (fr))
#define OM_SDW_NOFILE     (-104)  /* OM_SDW_ERR(-FR_NO_FILE); a read also returns it for FR_NO_PATH (a missing folder) */

/* sum_us: added to the produced interface so the stress page can print a mean per chunk */
typedef struct { uint32_t chunks, retries, verify_fail, last_rc, max_us, sum_us; } om_sdw_stats;
typedef void (*om_sdw_gap_fn)(void);          /* called between chunks: the menu waits one V-blank, the launcher too */
/* the next bytes into dst (at most cap); 0 = the end -- or, only while cap < OM_SDW_PIECE_MAX, "no room for my next
 * piece" (the chunk is flushed and the source asked again with more room); < 0 = an error, returned as is */
typedef int32_t (*om_sdw_src_fn)(void *ctx, uint8_t *dst, uint32_t cap);

int om_sdw_write_file(const char *path, const uint8_t *data, uint32_t len, int verify, uint8_t *scratch2k,
                      om_sdw_gap_fn gap, om_sdw_stats *st);   /* create/truncate, <= 2 KB chunks, s_sync each */
int om_sdw_write_src(const char *path, om_sdw_src_fn src, void *ctx, int verify, uint8_t *scratch2k,
                     om_sdw_gap_fn gap, om_sdw_stats *st);    /* the same, from a source (FAVS/RECENT) */
int om_sdw_replace(const char *path, const uint8_t *data, uint32_t len, uint8_t *scratch2k, om_sdw_gap_fn gap,
                   om_sdw_stats *st);          /* writes "<path>.NEW", verifies, renames over <path> (two-phase) */
/* buf: 4-aligned, cap >= 260 and >= the file's size + 4 (libsatiator reads whole words); OM_SDW_NOFILE when neither
 * path nor "<path>.NEW" exists */
int om_sdw_read_file(const char *path, uint8_t *buf, uint32_t cap, uint32_t *len);
/* om_sdw_read_file's rules, but a longer file gives its first (cap - 4) & ~3 bytes instead of
 * OM_E_FULL (the menu's ACTIVE.TXT read: only its first line) */
int om_sdw_read_head(const char *path, uint8_t *buf, uint32_t cap, uint32_t *len);
/* one piece of a file too big for one buffer (a cart-only save the import copies). off 0
 * creates or truncates path; off > 0 opens it (which must be exactly off bytes long: OM_E_RANGE otherwise) and
 * appends. Chunks and syncs as om_sdw_write_file (OM_SDW_CHUNK, the retry, gap between chunks; the data is copied into
 * scratch2k, so any alignment); no verify -- the caller compares the finished file with its source. Not power-cut
 * safe on its own: the caller writes a temporary name and renames it when it is whole. */
int om_sdw_write_at(const char *path, uint32_t off, const uint8_t *data, uint32_t len, uint8_t *scratch2k,
                    om_sdw_gap_fn gap, om_sdw_stats *st);
/* optional per-chunk timing (write + sync, retry included) into st->max_us/sum_us; (0, 0) turns it off */
void om_sdw_set_timer(uint16_t (*now)(void), uint32_t (*to_us)(uint16_t ticks));
#endif
