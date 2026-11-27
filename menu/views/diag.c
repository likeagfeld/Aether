/* menu/views/diag.c -- see diag.h. Measures everything once at entry (so the screen never changes under the
 * user mid-read), then just redraws on a page flip and polls for the dismiss. Nothing here submits a VDP1
 * command or touches a VDP2 register: like menu/app/fatal.c's screens, NBG0 text (priority 7, om_v2_init) draws
 * over whatever the Folders view left on screen, so there is no display state to restore on exit. BSD-3-Clause.
 */
#include <string.h>
#include "sgl.h"
#include "satiator.h"
#include "om_fmt.h"         /* OM_E_FIELD/OM_E_SHORT/OM_E_CRC and om_strerror, for named rc's on a FAIL line */
#include "sd.h"
#include "audio.h"         /* om_audio_boot_vblanks */
#include "bootplay.h"
#include "boottime.h"      /* om_boot_timing_get */
#include "version.h"
#include "folders.h"        /* om_folders_miss_total (Task 16a's own tiny getter) */
#include "loadbg.h"         /* OM_LBG_E_DMA: the loading background's DMA-timeout rc (om_load_out.bg_rc) */
#include "msgfmt.h"         /* om_msg_rc: a "prefix + reason + rc" line that never truncates the rc itself, only
                             * the reason -- MEASURED this session (emulator capture) that a plain strcat chain
                             * can push the actual OK/FAIL past column 40 and off the visible 40x28 screen */
#include "pad.h"
#include "text.h"
#include "art.h"
#include "omlayout.h"       /* OM_SNDRAM (HWF-33), OM_VRAM_UI/_END (uitext.c's pool) */
#include "uitext.h"         /* om_ut_pool_used */
#include "v1.h"             /* om_v1_count/om_v1_dropped (VDP1 command list, not drawn into from here) */
#include "vdp2theme.h"      /* om_v2_hwstat (the 2026-11-19 hardware flicker instrument) */
#include "diag.h"
#include "biosid.h"         /* the BIOS line on page 1 */
#include "safearea.h"       /* g_om_safe, the inset of the pages */
#include "interstitial.h"   /* om_istl_logo (BIGLOGO.BIN in LWRAM) */
#include "fwcheck.h"        /* the firmware verdict */
#include "fwver.h"
#ifdef OMSAT_SDTEST             /* page 4, the SD stress test, on the H2 test disc only (sbuild --sdtest) */
#define OM_DIAG_LAST 3
#else
#define OM_DIAG_LAST 2
#endif

/* --- page 2's "last loading run" snapshot, captured by folders.c right after every om_loading_run ---------- */

static struct { int have; uint32_t bg_late, bg_wraps, bg_frames; int bg_rc; } s_load;

void om_diag_record_load(const om_load_out *out)
{
    s_load.have = 1;
    s_load.bg_late = out->bg_late;
    s_load.bg_wraps = out->bg_wraps;
    s_load.bg_frames = out->bg_frames;
    s_load.bg_rc = out->bg_rc;
}

/* --- Task 13d's boot-step timings (menu/app/boottime.h, om_boot_timing_get) and Task 14/15's SOUND.BIN boot load
 * (om_audio_boot_vblanks): this page is their one display (Ruling M3-R45 (b); 13d's fake-only NBG0 row is gone). */
static void put_boot_steps(char *line, const om_boot_timing *t, int ms)
{   /* "VBL a b c d e f" or "MS a b c d e f" (M6 Task 5: the total moved to a line of its own, so each fits 34
     * columns) */
    char n1[12];
    uint32_t k;
    strcpy(line, ms ? "MS " : "VBL");
    for (k = 0; k < OM_BT_N; k++) {
        strcat(line, " ");
        if (!(t->done & (1u << k))) { strcat(line, "-"); continue; }
        strcat(line, om_itoa(n1, (int)(ms ? (t->us[k] + 500u) / 1000u : t->vbl[k])));
    }
}

/* --- SD read throughput: /aether/ART.PAK, or the first of a short, fixed fallback list that actually opens
 * (never a full card scan from inside a diagnostic screen) --------------------------------------------------- */

#define OM_DIAG_READ_MS   1500u                  /* brief: "for about 1.5 s" */
#define OM_DIAG_CHUNK     2048u                  /* S_MAXBUF: <= 2048 B per s_read (satiator.h) */
#define OM_DIAG_CHUNK_CAP 4000u                  /* hard backstop: 4000 * 2 KB = 8 MB, far past any 1.5 s read;
                                                   * guarantees termination independent of the elapsed-time math */

typedef struct { int rc; uint32_t bytes, us; const char *path; int fake; } om_diag_sd;

/* A single FRC diff never spans more than one chunk read, so it can never see more than one 16-bit wrap
 * (om_ut_frc_us's own contract -- see menu/ui/uitext.c's identical per-chunk accumulation). Accumulating many
 * short diffs into a uint32_t microsecond total (which itself wraps only after ~71 minutes) is the same
 * technique menu/views/loading.c uses for s_bg.dec_us/st_us: measuring the SUM of short brackets, never one
 * long bracket, to avoid the "does my bracket enclose the whole thing it names" trap. */
static void sd_read_bench(om_diag_sd *r, uint8_t *buf)
{
    static const char *const cand[] = { OM_PATH_ARTPAK, OM_PATH_LOADBG, OM_PATH_LIBIDX, OM_PATH_GAMESDB };
    uint32_t i, size, chunks;
    int fd = OM_SD_NOFILE;         /* default: nothing on the short candidate list even has nonzero size */
    r->bytes = r->us = 0;
    r->path = "(none)";
    r->fake = 0;
#ifdef OMSAT_FAKECARD
    r->fake = 1;
#endif
    for (i = 0; i < sizeof cand / sizeof cand[0]; i++) {
        if (om_sd_size(cand[i], &size) == 0 && size > 0) {
            fd = om_sd_open(cand[i]);
            if (fd >= 0) { r->path = cand[i]; break; }
            /* fd keeps this candidate's own rc; the next candidate (if any) may still overwrite it with success */
        }
    }
    if (fd < 0) { r->rc = fd; return; }
    r->rc = 0;
    for (chunks = 0; chunks < OM_DIAG_CHUNK_CAP; chunks++) {
        uint16_t t0 = om_ut_frc();
        int n = s_read(fd, buf, (int)OM_DIAG_CHUNK);
        r->us += om_ut_frc_us((uint16_t)(om_ut_frc() - t0));
        if (n < 0) { r->rc = om_sd_err(n); break; }
        if (n == 0) {                              /* EOF (the fake card's files are far under 1.5 s of KB/s):
                                                      * wrap and keep reading the same bytes -- correct on hardware
                                                      * too, and the point here is sustained throughput, not EOF */
            if (s_seek(fd, 0, C_SEEK_SET) < 0) break;
            continue;
        }
        r->bytes += (uint32_t)n;
        if (r->us >= OM_DIAG_READ_MS * 1000u) break;
    }
    s_close(fd);
}

/* --- SD write sanity: /aether/DIAG.TMP, 4 KB through the one write path (sdwrite.h: 2,044 B synced chunks,
 * read back and compared), then deleted (s_unlink: a plain buffer_write + simplecall, satiator.c). buf: the 2 KB
 * scratch, then the 4 KB of data. --------------------------------------------------------------------------------- */

#define OM_DIAG_TMP_PATH "/aether/DIAG.TMP"

static int sd_write_check(uint8_t *buf)
{
    uint8_t *data = buf + OM_DIAG_CHUNK;
    uint32_t i;
    int rc, k;
    for (i = 0; i < 2u * OM_DIAG_CHUNK; i++)
        data[i] = (uint8_t)((i >> 11) * 97u + i);             /* the same pattern as before Task 21 */
    rc = om_sdw_write_file(OM_DIAG_TMP_PATH, data, 2u * OM_DIAG_CHUNK, 1, buf, om_sd_gap, 0);
    k = s_unlink(om_sd_path(OM_DIAG_TMP_PATH));               /* review-final-m3 M2: never left behind */
    return rc ? rc : k < 0 && k != -FR_NO_FILE ? om_sd_err(k) : 0;
}

/* --- SCSP sound RAM: one scratch word inside the unclaimed 0x40000-0x77FFF (ledger D3; menu/app/audio.h /
 * menu/views/loading.c cite the same range for the same reason -- it is only EVER borrowed during an active
 * loading-screen run, which cannot be concurrent with the Folders view this screen is entered from). Never
 * 0x30000-0x3FFFF (built-in SFX) or 0x78000+ (SGL's own PCM_Work). Write, read back two patterns, restore. --- */

#define OM_DIAG_SCSP_OFF 0x40000u

static int scsp_check(void)
{
    volatile uint16_t *p = (volatile uint16_t *)(OM_SNDRAM + OM_DIAG_SCSP_OFF);
    uint16_t orig = *p;
    int ok;
    *p = 0x5A5Au;
    ok = (*p == 0x5A5Au);
    *p = 0xA5A5u;
    ok = ok && (*p == 0xA5A5u);
    *p = orig;
    return ok && *p == orig;
}

/* --- firmware/version strings -------------------------------------------------------------------------------- */

static char *hex8(char *buf, uint32_t v)
{
    static const char d[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 8; i++) buf[i] = d[(v >> (28 - 4 * i)) & 15u];
    buf[8] = 0;
    return buf;
}

static const char *area_name(uint8_t a)
{
    switch (a) {
    case SMPC_AREA_JP: return "JAPAN";
    case SMPC_AREA_AJ_NTSC: return "ASIA NTSC";
    case SMPC_AREA_NA: return "NORTH AMERICA";
    case SMPC_AREA_SA_NTSC: return "S AMERICA NTSC";
    case SMPC_AREA_KO: return "KOREA";
    case SMPC_AREA_AJ_PAL: return "ASIA PAL";
    case SMPC_AREA_EU_PAL: return "EUROPE";
    case SMPC_AREA_SA_PAL: return "S AMERICA PAL";
    default: return "RESERVED/UNKNOWN";
    }
}

/* --- free HWRAM / VDP1 pool ----------------------------------------------------------------------------------
 * `end` (zero leading underscores in C) reaches app.ld's literal `_end = .;` symbol: this toolchain adds one
 * leading underscore to every C-level global (confirmed this session via `nm` on a built ELF: C `om_v2_rects_end`
 * links as `_om_v2_rects_end`), so a C name with none is the classic newlib `extern char end[];` idiom for a
 * linker-defined symbol already spelled with one underscore. 0x060F0000 is app.ld's own ASSERT ceiling and
 * tools/sbuild.py's HW_END_MAX (the SGL work area start; HWF-12). */
extern char end[];
#define OM_DIAG_HW_END_MAX 0x060F0000u

/* --- rendering -------------------------------------------------------------------------------------------------
 * Three pages of plain NBG0 text (menu/ui/text.h), measured once at entry (so nothing on screen changes while the user
 * is reading it) and redrawn only on a page flip. The pages follow the Safe area (an NBG0 inset of margin/8
 * cells, so 34 columns and 22 rows at 24 px); every line is built to fit 34 columns, and putw wraps one that would not
 * at a space onto the next row rather than cut it. Page 2's last row counts what was cut anyway (om_text_cut_count). */

static int s_cols = 40;              /* 40 - 2 * the inset */
static uint32_t s_cut0;              /* om_text_cut_count at entry */
static int s_pad_row = 11;           /* where page 1 put the live PAD ID line */

static int putw(int y, const char *s)
{   /* s from row y, wrapped at spaces over as many rows as it needs (continuations indented 2); returns the next row */
    char row[41];
    const char *p = s;
    int first = 1;
    while (*p || first) {
        int room = first ? s_cols : s_cols - 2, len = (int)strlen(p), cut = len, k = 0;
        if (len > room) {
            for (cut = room; cut > 0 && p[cut] != ' '; cut--)
                ;
            if (cut <= 0) cut = room;                     /* one word wider than a row: split it, lose nothing */
        }
        if (!first) { row[k++] = ' '; row[k++] = ' '; }
        memcpy(row + k, p, (uint32_t)cut);
        row[k + cut] = 0;
        om_text_put(0, y++, row, 40);
        for (p += cut; *p == ' '; p++)
            ;
        first = 0;
    }
    return y;
}

static void banner(int page)
{
    char line[40];
    strcpy(line, "AETHER DIAGNOSTICS  PAGE ");
    line[27] = (char)('1' + page); line[28] = '/'; line[29] = (char)('1' + OM_DIAG_LAST); line[30] = 0;
    om_text_put(0, 0, line, 40);
    om_text_put(0, 1, "L/R: PAGE    OTHER BUTTON: EXIT", 40);
}

/* CRC-32 of the 512 KB boot ROM, read cache-through (HWF-12), measured once at entry. The time is
 * the sum of short FRC brackets (one per 4 KB slice, so each bracket sees at most one 16-bit wrap -- sd_read_bench's
 * rule). */
static uint32_t s_bios_crc, s_bios_us;
static void bios_measure(void)
{
    uint32_t o, crc = 0;
    s_bios_us = 0;
    for (o = 0; o < OM_BIOS_ROM_LEN; o += 0x1000u) {
        uint16_t t0 = om_ut_frc();
        crc = om_crc32(crc, (const void *)(OM_BIOS_ROM_CT + o), 0x1000u);
        s_bios_us += om_ut_frc_us((uint16_t)(om_ut_frc() - t0));
    }
    s_bios_crc = crc;
}

/* Task 19 Step 1 pulled forward for hardware session H1: the peripheral ID byte SGL's INTBACK put in
 * its table for the first device on each port, raw (PerDigital.id, SL_DEF.H; per SMPC_Manual.txt Table 3.10 the
 * high nibble is the type, the low the data size: 0x02 = standard digital pad), its extension size byte, and how many
 * devices SGL counts on the port. SGL keeps 15 table entries per port (a multitap's maximum): port 2's first device
 * is entry 15 -- measured on Mednafen 1.32.1 with a pad in each port (task-20a-report.md): entries 0 and 15 read
 * 0x02, entries 1-14 0xFF. Redrawn every frame, on its own row (s_pad_row), at most 34 columns. */
static void draw_pads(void)
{
    char line[48], n1[12];
    int port;
    strcpy(line, "PAD ID");
    for (port = 0; port < 2; port++) {
        uint8_t n = port ? Per_Connect2 : Per_Connect1;
        strcat(line, port ? " P2 " : " P1 ");
        /* H1 hardware photo, M6 Task 19 Minor A: "N0" at the row's right edge reads as the word "NO" on a
         * real TV -- spell it out instead of packing a digit against the N */
        if (!n) { strcat(line, "-- NONE"); continue; }
        {
            const PerDigital *d = &Smpc_Peripheral[port ? 15 : 0];
            hex8(n1, d->id);
            strcat(line, n1 + 6);
            strcat(line, " X");
            hex8(n1, d->ext);
            strcat(line, n1 + 6);
            strcat(line, " N");
            strcat(line, om_itoa(n1, (int)n));
        }
    }
    om_text_put(0, s_pad_row, line, 40);
}

static void draw_page0(uint8_t area, const om_diag_sd *rd, int wr_rc)
{
    /* 96, not 41: several of the strcat chains below can run past 40 characters (a long firmware string) before putw
     * wraps them -- the C buffer itself must hold the whole string. */
    char line[96], n1[12];
    uint32_t kbps;
    int y;

    om_text_clear();
    banner(0);
    /* "Aether <version> <git short SHA> <commit date>" (version.h; SHA and date from tools/sbuild.sh: the
     * commit's own date rather than __DATE__, so one commit always builds the same bytes) */
    y = putw(3, OM_BUILD_STRING);
    {   /* "FW" and "BL" (were SATIATOR FW / BOOTLDR), each on its own row */
        char fw[32];
        int rc = s_get_fw_version(fw, sizeof fw);
        uint32_t bl;
        int blrc = s_get_bootloader_version(&bl);
        strcpy(line, "FW ");
        strcat(line, rc < 0 ? "n/a" : fw);
        {   /* the boot's verdict against the tested minimum (fwcheck.c), e.g. "FW 163 ... OK (MIN 161)" */
            char n2[12];
            strcat(line, " ");
            strcat(line, om_fw_word(om_fw_status()));
            strcat(line, " (MIN ");
            strcat(line, om_itoa(n2, OM_FW_MIN_TESTED));
            strcat(line, ")");
        }
        y = putw(y, line);
        strcpy(line, "BL ");
        strcat(line, blrc < 0 ? "n/a" : hex8(n1, bl));
        y = putw(y, line);
    }
    {   /* AREA=<code> and TV first (the code is the ground truth), the decoded name last */
        strcpy(line, "AREA=");
        om_itoa(n1, area);
        strcat(line, n1);
        strcat(line, " TV ");
        strcat(line, om_tv_is_pal() ? "PAL" : "NTSC");
        strcat(line, " (");
        strcat(line, area_name(area));
        strcat(line, ")");
        y = putw(y, line);
    }
    {   /* "BIOS <crc8> <ms>MS <name or ?>": the CRC is the ground truth, the name a convenience */
        const om_bios_id *b = om_bios_lookup(s_bios_crc);
        strcpy(line, "BIOS ");
        strcat(line, hex8(n1, s_bios_crc));
        strcat(line, " ");
        om_itoa(n1, (int)(s_bios_us / 1000u));
        strcat(line, n1);
        strcat(line, "MS ");
        strcat(line, b ? b->name : "?");
        y = putw(y, line);
    }
    {   /* the file read (a path that does not fit wraps; its tail, the file name, is never lost) */
#ifdef OMSAT_FAKECARD                              /* no fake-build wording in --app's bytes */
        strcpy(line, rd->fake ? "SD READ 1.5 S [FAKE] " : "SD READ 1.5 S ");
#else
        strcpy(line, "SD READ 1.5 S ");
#endif
        strcat(line, rd->path);
        y = putw(y + 1, line);
    }
    if (rd->rc < 0) {
        om_msg_rc(line, 35, "  FAIL: ", om_strerror(rd->rc), rd->rc);   /* the rc survives truncation; the name
                                                                         * (never the number) is cut if it must be */
    } else {
        uint32_t ms = rd->us / 1000u;                  /* 32-bit only: bytes * 1000 can overflow at the CHUNK_CAP
                                                         * ceiling (~8 MB), so divide by ms, not multiply by 1000 --
                                                         * also avoids a libgcc 64-bit divide (menu/ui/uitext.c's
                                                         * own note on SH-2 division being a library call) */
        kbps = ms ? rd->bytes / ms : 0u;               /* B/ms == KB/s */
        strcpy(line, "  ");
        om_itoa(n1, (int)rd->bytes);
        strcat(line, n1);
        strcat(line, " B ");
        om_itoa(n1, (int)ms);
        strcat(line, n1);
        strcat(line, " MS = ");
        om_itoa(n1, (int)kbps);
        strcat(line, n1);
        strcat(line, " KB/S");
    }
    y = putw(y, line);

    /* the path (OM_DIAG_TMP_PATH) is deliberately not echoed: the line exists to show OK/FAIL (measured, emulator
     * capture: with the path the verdict fell off the screen) */
    if (wr_rc == 0) {
        strcpy(line, "SD WRITE+READ 4KB: OK");
#ifdef OMSAT_FAKECARD
        strcat(line, " [FAKE]");
#endif
    } else {
        om_msg_rc(line, 35, "SD WRITE+READ 4KB: FAIL: ", om_strerror(wr_rc), wr_rc);
    }
    y = putw(y + 1, line);
    /* H1 hardware photo, M6 Task 19 Minor A: a blank row before PAD ID, matching the gap every other
     * section here keeps -- without it the line sat directly under SD WRITE+READ with no separation */
    s_pad_row = y + 1;
    draw_pads();                                                    /* om_diag_run redraws it every frame */
}

static void draw_page2(void)
{   /* the boot animation's lines moved here from page 1, so page 1 fits 22 rows at 34 columns */
    char line[41];
    int y;
    om_text_clear();
    banner(2);
    om_text_put(0, 3, "BOOT ANIMATION (13b, g_bootplay):", 40);
    om_bootplay_line(line, 1); y = putw(4, line);
    om_bootplay_line(line, 2); y = putw(y, line);
    om_bootplay_line(line, 3); y = putw(y, line);
    if (g_bootplay.rc) { om_bootplay_line(line, 0); y = putw(y, line); }
    om_bootplay_line(line, 4); if (line[0]) y = putw(y, line);   /* restart / re-sync detail, when there was any */
    {   /* the big logo's source (BIGLOGO.BIN from the disc, its CRC checked again) and a CRC-32 of the 152 x 101
         * words VDP1 VRAM holds now (OM_VRAM_BOOTANIM, 16-bit reads, HWF-23): the interstitial's upload, or the saver's */
        volatile const uint16_t *v = (volatile const uint16_t *)(OM_CT(OM_VDP1_VRAM) + OM_VRAM_BOOTANIM);
        const uint8_t *t;
        uint32_t tl, i, crc = 0;
        uint8_t be[2];
        char n1[12];
        int rc = om_istl_logo(&t, &tl);
        for (i = 0; i < 152u * 101u; i++) {
            uint16_t w = v[i];
            be[0] = (uint8_t)(w >> 8);
            be[1] = (uint8_t)w;
            crc = om_crc32(crc, be, 2);
        }
        strcpy(line, "BIGLOGO rc ");
        strcat(line, om_itoa(n1, rc));
        strcat(line, " VRAM ");
        strcat(line, hex8(n1, crc));
        putw(y + 1, line);
    }
}

static void draw_page1(void)
{
    char line[96], n1[12];   /* see draw_page0's comment on this size */
    uint32_t total, headroom, cap, used;
    char tag;
    uint32_t worst;
    int y;
    const om_boot_timing *bt = om_boot_timing_get();

    om_text_clear();
    banner(1);

    om_text_put(0, 2, "LOADING BG, LAST RUN:", 40);    /* 13c's background, the last loading run this session */
    if (!s_load.have)
        y = putw(3, "  NO LAUNCH YET THIS SESSION");
    else {
        /* 13c's own fps (s_bg.g.fps) is file-static in loading.c and not in om_load_out, so it is not shown; LATE and
         * the DMA timeout come first (the brief asks for them by name) */
        strcpy(line, "  LATE ");
        om_itoa(n1, (int)s_load.bg_late);
        strcat(line, n1);
        strcat(line, " VBL DMA TO ");
        strcat(line, s_load.bg_rc == OM_LBG_E_DMA ? "YES" : "no");
        y = putw(3, line);
        strcpy(line, "  FRAMES ");
        om_itoa(n1, (int)s_load.bg_frames);
        strcat(line, n1);
        strcat(line, " WRAPS ");
        om_itoa(n1, (int)s_load.bg_wraps);
        strcat(line, n1);
        strcat(line, " RC ");
        om_itoa(n1, s_load.bg_rc);
        strcat(line, n1);
        y = putw(y, line);
    }

    y = putw(y, "BOOT: MODE LIB BG CFG SORT RAS");   /* boottime.h's six, in order */
    put_boot_steps(line, bt, 0);
    y = putw(y, line);
    put_boot_steps(line, bt, 1);
    y = putw(y, line);
    strcpy(line, "TOTAL ");
    strcat(line, (bt->done & (1u << OM_BT_RASTER)) ? om_itoa(n1, (int)bt->total_vbl) : "-");
    strcat(line, bt->ui_shown ? " VBL UI" : " VBL FAST");
    strcat(line, " SOUND ");                             /* SOUND.BIN's load at boot */
    strcat(line, om_itoa(n1, (int)om_audio_boot_vblanks()));
    strcat(line, " VBL");
    y = putw(y, line);

    om_folders_miss_total(&total, &tag, &worst);
    strcpy(line, "MISSES ");
    om_itoa(n1, (int)total);
    strcat(line, n1);
    strcat(line, " VBL (WORST ");
    {
        char t1[2]; t1[0] = tag; t1[1] = 0;
        strcat(line, t1);
    }
    strcat(line, "=");
    om_itoa(n1, (int)worst);
    strcat(line, n1);
    strcat(line, ")");
    y = putw(y, line);

    headroom = OM_DIAG_HW_END_MAX - (uint32_t)end;      /* _end to the SGL work area (HWF-12) */
    strcpy(line, "FREE HWRAM ");
    om_itoa(n1, (int)headroom);
    strcat(line, n1);
    strcat(line, " B");
    y = putw(y, line);

    used = om_ut_pool_used();
    cap = OM_VRAM_UI_END - OM_VRAM_UI;
    strcpy(line, "UI POOL ");                           /* VDP1 text/icon pool */
    om_itoa(n1, (int)used);
    strcat(line, n1);
    strcat(line, "/");
    om_itoa(n1, (int)cap);
    strcat(line, n1);
    strcat(line, " B ART ");                       /* cover cache slots (5, or 4 if C2 was refused) */
    om_itoa(n1, om_art_cache_slots());
    strcat(line, n1);
    y = putw(y, line);

    strcpy(line, "VDP1 CMDS ");                         /* this frame; dropped since boot */
    om_itoa(n1, (int)om_v1_count());
    strcat(line, n1);
    strcat(line, "/F DROPPED ");
    om_itoa(n1, (int)om_v1_dropped());
    strcat(line, n1);
    y = putw(y, line);

    strcpy(line, "SCSP RAM @0x40000: ");   /* write/read/restore, per this file's scsp_check() comment */
    strcat(line, scsp_check() ? "OK" : "FAIL");
    y = putw(y, line);

    {   /* 2026-11-19 hardware flicker (hw-flicker-report.md): the V-blank hook's NBG2 map writes, rects_end's budget
         * and VDP1 transfer-over, since the menu started (vdp2theme.h om_v2_stat) */
        const om_v2_stat *s = om_v2_hwstat();
        y = putw(y, "NBG2/VDP1 SINCE MENU START:");
        strcpy(line, "VBL ");
        strcat(line, om_itoa(n1, (int)s->vbl));
        strcat(line, " LATE IN ");
        strcat(line, om_itoa(n1, (int)s->late_in));
        strcat(line, " OUT ");
        strcat(line, om_itoa(n1, (int)s->late_out));
        y = putw(y, line);
        strcpy(line, "MAXW ");
        strcat(line, om_itoa(n1, (int)s->max_w));
        strcat(line, " CPUFL ");
        strcat(line, om_itoa(n1, (int)s->cpu_flush));
        strcat(line, " DIRECT ");
        strcat(line, om_itoa(n1, (int)s->direct));
        y = putw(y, line);
        strcpy(line, "CARRY ");
        strcat(line, om_itoa(n1, (int)s->carry));
        strcat(line, " RUN ");
        strcat(line, om_itoa(n1, (int)s->carry_run));
        y = putw(y, line);
        strcpy(line, "VDP1 OVER ");
        strcat(line, om_itoa(n1, (int)s->v1_over));
        y = putw(y, line);
    }
    /* characters any page cut since this screen opened (0 = every line fits the Safe area) */
    strcpy(line, "TXT CUT ");
    strcat(line, om_itoa(n1, (int)(om_text_cut_count() - s_cut0)));
    putw(y + 1, line);
}

#ifdef OMSAT_SDTEST
/* --- page 4: the SD stress test, rings_vs_kai §4 tests 2, 3 and 6 for hardware session H2 (the
 * --sdtest disc only: it does not fit the shipping image's HWRAM floor, docs/ledger.md Task 21) ---------------------
 * Runs only on request (A, then A again), writes only /aether/SDTEST/ and deletes it afterwards. Counters are
 * redrawn every 10 writes straight into NBG0 (slPrint, no slSynch), so condition GAP0 really has no wait between
 * writes; each condition's V-blank total (om_v2_hwstat, the V-blank hook's own count) is printed with it, so the gap
 * is measured, not assumed. A firmware stall that exec_cmd's bounded wait recovers from is counted as T (rc -120,
 * OM_SD_TIMEOUT), apart from other failures (F); a true freeze is a counter that stops. */

#define OM_SDT_DIR "/aether/SDTEST"
#define OM_SDT_N   500u

static struct {
    uint8_t ran, armed;
    uint32_t t2[3][4];               /* per gap condition: done, failed, timed out, V-blanks */
    int x2048, xrep, xren, clean;    /* exact 2048: 0 OK, > 0 the first differing offset + 1, < 0 an rc */
    uint32_t odd[3];                 /* odd-length reads: data errors, round-up pad bytes, guard damage */
    uint32_t max_us, mean_us, frc_ms, wall_ms;
} s_sdt;

static void gap1(void) { om_sd_gap(); }                 /* main.c's: one V-blank by the hook's count */
static void gap2(void) { om_sd_gap(); om_sd_gap(); }

static char *sdt_name(char *b, const char *leaf)
{
    strcpy(b, OM_SDT_DIR "/");
    return strcat(b, leaf);
}

static void sdt_fill(uint8_t *b, uint32_t n, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(i * 13u + seed * 7u + 1u);
}

typedef struct { uint32_t left, i; } sdt_pat;
static int32_t sdt_src(void *ctx, uint8_t *dst, uint32_t cap)
{   /* the timing file's bytes, generated (no 32 KB buffer) */
    sdt_pat *q = (sdt_pat *)ctx;
    uint32_t n = q->left < cap ? q->left : cap, k;
    for (k = 0; k < n; k++) dst[k] = (uint8_t)(q->i++ * 13u + 1u);
    q->left -= n;
    return (int32_t)n;
}

static void cat_u(char *line, const char *label, uint32_t v)
{
    char n1[12];
    strcat(line, label);
    strcat(line, om_itoa(n1, (int)v));
}

static void draw_t2(int c, int y)
{
    char line[64];                   /* review I2 audit: room for every field at 10 digits (61 B) */
    strcpy(line, "T2 GAP");
    line[6] = (char)('0' + c); line[7] = 0;
    cat_u(line, " ", s_sdt.t2[c][0]);
    cat_u(line, "/500 F", s_sdt.t2[c][1]);
    cat_u(line, " T", s_sdt.t2[c][2]);
    cat_u(line, " VBL ", s_sdt.t2[c][3]);
    om_text_put(0, y, line, 40);
}

static int sdt_cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{   /* -1 when equal, else the first differing offset */
    uint32_t i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return (int)i;
    return -1;
}

static const char *const s_leaves[] = { "T3.BIN", "T6.BIN", "TM.BIN", "R.INI", "R.INI.NEW", "R2.BIN" };

static void sdt_clean(char *name, char *leaf, uint32_t n)
{   /* the first n of: T2_0..9, the six leaves, then the folder (FatFs refuses a non-empty one); absent is fine */
    uint32_t i;
    int rc;
    for (i = 0; i < n; i++) {
        if (i < 10) { strcpy(leaf, "T2_0.BIN"); leaf[3] = (char)('0' + i); sdt_name(name, leaf); }
        else if (i < 16) sdt_name(name, s_leaves[i - 10]);
        else strcpy(name, OM_SDT_DIR);
        rc = s_unlink(om_sd_path(name));
        if (rc < 0 && rc != -FR_NO_FILE && !s_sdt.clean) s_sdt.clean = om_sd_err(rc);
    }
}

static void sdt_run(uint8_t *w)
{   /* w: >= 8 KB of the card work area -- scratch2k at 0, data at 2 KB, read-back at 4 KB */
    static const char *const *const leaves = s_leaves;
    static const uint8_t lens[6] = { 1, 2, 3, 5, 6, 7 };
    static const om_sdw_gap_fn gaps[3] = { 0, gap1, gap2 };
    uint8_t *scr = w, *dat = w + 2048, *rb = w + 4096;
    char name[40], leaf[12];          /* "T2_0.BIN" + NUL = 9 B (was 8) */
    uint32_t c, i, len, v0;
    int rc, fd;
    om_sdw_stats st;
    memset(&s_sdt, 0, sizeof s_sdt);
    s_sdt.ran = 1;
    om_text_put(0, 5, "RUNNING (MUSIC STOPPING FIRST)", 40);
    for (i = 0; i < 600 && !om_audio_music_faded(); i++) slSynch();   /* the ring starves once sd_quiet closed it */
    s_mkdir(om_sd_path(OM_SDT_DIR));                                   /* FR_EXIST is fine */
    for (c = 0; c < 3; c++) {                                          /* test 2: 500 x 1,000 B per gap condition */
        v0 = om_v2_hwstat()->vbl;
        draw_t2((int)c, 7 + (int)c);                                   /* on entry: a hang in writes 0-8 reads 0/500 */
        for (i = 0; i < OM_SDT_N; i++) {
            strcpy(leaf, "T2_0.BIN");
            leaf[3] = (char)('0' + i % 10u);
            sdt_fill(dat, 1000, i + c * OM_SDT_N);
            if (gaps[c]) gaps[c]();
            rc = om_sdw_write_file(sdt_name(name, leaf), dat, 1000, 1, scr, gaps[c], 0);
            s_sdt.t2[c][rc == 0 ? 0 : rc == OM_SD_TIMEOUT ? 2 : 1]++;
            if (i % 10u == 9u || i + 1 == OM_SDT_N) {
                s_sdt.t2[c][3] = om_v2_hwstat()->vbl - v0;
                draw_t2((int)c, 7 + (int)c);
            }
        }
    }
    sdt_clean(name, leaf, 10);                                         /* test 2's files go before the rest */
    /* test 3: one raw s_write of exactly 2,048 B (not the helper), read back, diffed */
    sdt_fill(dat, 2048, 0x5A);
    rc = s_open(om_sd_path(sdt_name(name, leaves[0])), FA_WRITE | FA_CREATE_ALWAYS);
    if ((fd = rc) >= 0) {
        rc = s_write(fd, dat, 2048);
        if (rc == 2048) rc = s_sync(fd);
        s_close(fd);
    }
    rc = rc < 0 ? om_sd_err(rc) : 0;                                  /* a short write shows as a diff below */
    if (!rc && (rc = om_sdw_read_file(name, rb, 4096, &len)) == 0)
        rc = len != 2048 ? OM_E_SHORT : sdt_cmp(rb, dat, 2048) + 1;
    s_sdt.x2048 = rc;
    /* the write-then-replace twice (LFN ".INI.NEW" included), then a rename onto an existing name */
    sdt_fill(dat, 2048, 3);
    rc = om_sdw_replace(sdt_name(name, leaves[3]), dat, 2048, scr, gap1, 0);
    sdt_fill(dat, 2048, 4);
    if (!rc) rc = om_sdw_replace(name, dat, 2048, scr, gap1, 0);
    if (!rc && (rc = om_sdw_read_file(name, rb, 4096, &len)) == 0)
        rc = len != 2048 || sdt_cmp(rb, dat, 2048) >= 0 ? OM_E_CRC : 0;
    s_sdt.xrep = rc;
    om_sdw_write_file(sdt_name(name, leaves[5]), dat, 16, 0, scr, gap1, 0);
    {
        char other[40];
        rc = s_rename(name, sdt_name(other, leaves[3]));               /* expected: refused (-FR_EXIST) */
        s_sdt.xren = rc < 0 ? om_sd_err(rc) : 0;
    }
    /* timing: 32 KB through the helper, no gap, each 2 KB chunk bracketed (FRT); the V-blank count over the same run
     * is the bracket's positive control (a chunk longer than one FRC period would read short) */
    {
        sdt_pat q = { 16u * OM_SDW_CHUNK, 0 };
        memset(&st, 0, sizeof st);
        om_sdw_set_timer(om_ut_frc, om_ut_frc_us);
        v0 = om_v2_hwstat()->vbl;
        om_sdw_write_src(sdt_name(name, leaves[2]), sdt_src, &q, 0, scr, 0, &st);
        s_sdt.wall_ms = (om_v2_hwstat()->vbl - v0) * 16683u / 1000u;
        om_sdw_set_timer(0, 0);
        s_sdt.max_us = st.max_us;
        s_sdt.mean_us = st.chunks ? st.sum_us / st.chunks : 0;
        s_sdt.frc_ms = st.sum_us / 1000u;
    }
    /* test 6 (last: an odd-length read could upset the transfer state): reads of 1, 2, 3, 5, 6, 7 B at file offsets
     * 0..3 into an aligned buffer with 16 guard bytes each side. libsatiator moves whole words, so up to 3 bytes past
     * the length are rewritten by design (PAD, sd.c's 4-byte slack); GUARD counts damage beyond that */
    sdt_clean(name, leaf, 16);                                         /* everything so far, before test 6 */
    sdt_fill(dat, 64, 6);
    if (om_sdw_write_file(sdt_name(name, leaves[1]), dat, 64, 1, scr, gap1, 0) == 0
        && (fd = s_open(om_sd_path(name), FA_READ | FA_OPEN_EXISTING)) >= 0) {
        for (i = 0; i < 24; i++) {
            uint32_t n = lens[i % 6], off = i / 6, k, r4 = (n + 3u) & ~3u;
            memset(rb, 0xEE, 48);
            if (s_seek(fd, (int)off, C_SEEK_SET) < 0 || s_read(fd, rb + 16, (int)n) != (int)n) {
                s_sdt.odd[0]++;
                continue;
            }
            for (k = 0; k < 48; k++) {
                if (k >= 16 && k < 16 + n) s_sdt.odd[0] += rb[k] != dat[off + k - 16];
                else if (k >= 16 + n && k < 16 + r4) s_sdt.odd[1] += rb[k] != 0xEE;
                else s_sdt.odd[2] += rb[k] != 0xEE;
            }
        }
        s_close(fd);
    } else
        s_sdt.odd[0] = 24;
    sdt_clean(name, leaf, 17);
}

static void draw_page3(void)
{
    char line[64];                   /* review I2 audit: the ODD READ line reaches 56 B at 10 digits */
    int c;
    om_text_clear();
    banner(3);
    putw(3, "SD STRESS: WRITES /aether/SDTEST/");
    om_text_put(0, 5, !s_sdt.armed ? "A: ARM THE TEST" : "A AGAIN TO START (ABOUT 1 MIN)", 40);
    if (!s_sdt.ran) return;
    for (c = 0; c < 3; c++) draw_t2(c, 7 + c);
    strcpy(line, "EXACT 2048: ");
    if (!s_sdt.x2048) strcat(line, "OK");
    else if (s_sdt.x2048 > 0) cat_u(line, "DIFF AT ", (uint32_t)(s_sdt.x2048 - 1));
    else cat_u(line, "RC -", (uint32_t)-s_sdt.x2048);
    om_text_put(0, 11, line, 40);
    strcpy(line, "REPLACE ");
    if (s_sdt.xrep) cat_u(line, "RC -", (uint32_t)-s_sdt.xrep); else strcat(line, "OK");
    if (s_sdt.xren) cat_u(line, " RENAME-OVER -", (uint32_t)-s_sdt.xren); else strcat(line, " RENAME-OVER 0!");
    om_text_put(0, 12, line, 40);
    strcpy(line, "ODD READ DATA");
    cat_u(line, " ", s_sdt.odd[0]);
    cat_u(line, " PAD ", s_sdt.odd[1]);
    cat_u(line, " GUARD ", s_sdt.odd[2]);
    om_text_put(0, 13, line, 40);
    strcpy(line, "2KB CHUNK US");
    cat_u(line, " MAX ", s_sdt.max_us);
    cat_u(line, " MEAN ", s_sdt.mean_us);
    om_text_put(0, 14, line, 40);
    strcpy(line, "FRC SUM");
    cat_u(line, " ", s_sdt.frc_ms);
    cat_u(line, " MS WALL ", s_sdt.wall_ms);
    strcat(line, " MS");
    om_text_put(0, 15, line, 40);
    strcpy(line, "CLEANUP ");
    if (s_sdt.clean) cat_u(line, "RC -", (uint32_t)-s_sdt.clean); else strcat(line, "OK");
    cat_u(line, " GAP LATE ", om_sd_gap_late);                         /* 0 expected */
    om_text_put(0, 16, line, 40);
}
#endif

void om_diag_run(uint8_t area, void *work, uint32_t work_cap)
{
    uint8_t *scratch = (uint8_t *)work;
    om_diag_sd rd;
    int wr_rc;
    int page = 0;

    /* Measure once, before the screen is even drawn: a plain-text screen that could change under the reader is
     * worse than one that is slow to appear, and every call below is already individually time-bounded
     * (satiator.c's own exec_cmd HIRQ-wait cap; the chunk-count/elapsed-time caps above). */
    bios_measure();
    if (work_cap >= 4u * OM_DIAG_CHUNK) {            /* 2 KB scratch + 4 KB (sd_write_check); page 4 needs 8 KB */
        sd_read_bench(&rd, scratch);
        wr_rc = sd_write_check(scratch);
    } else {                                        /* never expected (card.h's work area is far larger), but a
                                                       * diagnostics screen must not assume -- it names the rc */
        rd.rc = OM_E_FIELD; rd.bytes = rd.us = 0; rd.path = "(scratch too small)"; rd.fake = 0;
        wr_rc = OM_E_FIELD;
    }

    /* MEASURED this session (emulator capture): without this, the entry combo's own L+R -- held long enough to
     * cross pad.c's OM_PAD_REPEAT_DELAY (18 frames) by the time this screen opens, since folders.c's hook keeps
     * holding them through the whole hold-then-Start sequence -- auto-repeats on this loop's very first
     * om_pad_poll and is read as a fresh R press, flipping straight to page 2 before the user ever sees page 1.
     * om_pad_prime() (pad.h: "a button held across a screen change is not a fresh press on the next screen's
     * first om_pad_poll") is the documented fix for exactly this, used the same way at every other screen change
     * in this codebase (menu/app/bootplay.c, and this file's own exit path below) -- it was missing here, on
     * entry, where it matters most since L+R are the two buttons this screen's own entry combo just used. */
    om_pad_prime();
    om_text_set_inset(om_safe_margin(g_om_safe.level) / 8);   /* the pages follow the Safe area */
    s_cols = 40 - 2 * (om_safe_margin(g_om_safe.level) / 8);
    s_cut0 = om_text_cut_count();
    draw_page0(area, &rd, wr_rc);
    /* om_pad_prime() stops the auto-repeat that was ALREADY past its delay from firing on this loop's first
     * poll, but a real person's fingers are not the fake script: someone who keeps holding L+R (or the whole
     * combo) past the 18-frame repeat delay AFTER page 1 is first shown would still auto-repeat R into an
     * unrequested page flip a moment later. `armed` closes that residual case: L and R are only honored once
     * both have been seen fully released at least once since this screen opened, so a repeat on a hold that
     * never actually released cannot be mistaken for the deliberate press the brief's "L/R page" means. The
     * `--diag page2` capture script's own 10-tick released gap before its scripted R press already satisfies this, so
     * it needed no change for this fix (confirmed by re-capturing after this change). */
    {
        int armed = 0;
        for (;;) {
            Uint16 p, held;
            slSynch();
            p = om_pad_poll();
            held = om_pad_held();
            if (!(held & (PER_DGT_TL | PER_DGT_TR)))
                armed = 1;
            if (armed && (p & (PER_DGT_TR | PER_DGT_TL))) {        /* three pages, R next, L back */
                int np = (p & PER_DGT_TR) ? (page < OM_DIAG_LAST ? page + 1 : OM_DIAG_LAST) : (page > 0 ? page - 1 : 0);
                if (np != page) {
                    page = np;
                    if (page == 0) draw_page0(area, &rd, wr_rc);
                    else if (page == 1) draw_page1();
                    else if (page == 2) draw_page2();
#ifdef OMSAT_SDTEST
                    else draw_page3();
#endif
                }
                continue;
            }
#ifdef OMSAT_SDTEST
            if (page == 3 && (p & PER_DGT_TA) && work_cap >= 4u * OM_DIAG_CHUNK) {   /* A arms, A runs */
                if (s_sdt.armed) sdt_run(scratch);
                else s_sdt.armed = 1;
                draw_page3();
                om_pad_prime();
                continue;
            }
#endif
            if (page == 0)
                draw_pads();                       /* live: a pad swapped while this page is up shows at once */
            if (p & (PER_DGT_TA | PER_DGT_TB | PER_DGT_TC | PER_DGT_TX | PER_DGT_TY | PER_DGT_TZ | PER_DGT_ST
                     | PER_DGT_KU | PER_DGT_KD | PER_DGT_KL | PER_DGT_KR))
                break;
        }
    }
    om_text_set_inset(0);                          /* NBG0 back to the full grid (fatal screens, fake HUD) */
}
