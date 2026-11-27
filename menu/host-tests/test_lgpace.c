/* menu/host-tests/test_lgpace.c -- the launch's SEGA-eye pacing during the save swap (hardware report,
 * FW163: "slow or doesn't even start until the game has started loading a bit, then slowly starts and then jumps to
 * full speed right at the last half second as the game actually loads"; a measured swap of 953 ms, ~29 ms a path
 * command on the user's card).
 *
 * The loading screen's eye (lgfx.c om_lg_pump) moves only when the launch offers it a chance, and reads LOADBG.BIN (the
 * launch adopts the menu's last frame and reads on from the next entry: nothing is buffered) only at those chances.
 * This test runs the REAL save swap (pgsrun.c, pgsbak.c, sdwrite.c, savegen.c) over fake_satiator.c on a card shaped
 * like test_pgscost's (the user's), and the REAL LOADBG.BIN reads (lgread.c) from the same fake card, on a model clock:
 * a path command (an open, a named stat, an opendir, a mkdir, a rename, an unlink, a chdir) PATH_US, any other command
 * 1 ms, a read or a write its bytes at 296 KB/s, a backup-library call 3 ms, a write gap to the next V-blank. A chance
 * is the launcher's hooks as lseq.c wires them: om_yield (yield.h), env.gap (gap_beat), env.prog (swap_prog) and
 * env.say (pgs_say). The eye's pump is modelled on lgfx.c: its read-ahead and flip decisions are lgread.c's own.
 * Gates: at most one path command between two chances, no stretch between chances over two V-blanks but a single
 * path command; the eye moves at the rate its share of the card reads (lgread.h OM_LGR_SHARE), never held longer than
 * its largest frame takes to read; the swap at most 1 / (1 - share) as long; and the card the same after the
 * sequence with the hook and without it. The old wiring (no yield hook, one 2 KB read only between files, one phase a
 * pump) is measured alongside: its path commands back to back are the hardware report's cause. BSD-3-Clause. */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "th.h"
#include "om_fmt.h"
#include "satiator.h"
#include "bupfile.h"
#include "loadbg.h"
#include "lgread.h"
#include "pgsrun.h"
#include "pgsbak.h"
#include "yield.h"

extern int fake_mkdir_calls, fake_reads, fake_nopath, fake_r1_on, fake_addonly;
extern int fake_c_open, fake_c_stat, fake_c_dirent, fake_c_opendir, fake_c_write, fake_c_rename, fake_c_unlink,
    fake_c_close, fake_c_chdir;
extern long fake_rbytes, fake_wbytes;
extern int fake_bram_calls;
int s_chdir(const char *f);
void fake_reboot(void);
void fake_bram_reset(void);
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date);
void fake_bram_del(const char *name);

#define PATH_US   29000u        /* hardware: the swap's ROOT/REL timing on the user's card */
#define CMD_US    1000u
#define BPS       296000u       /* the diag page's read rate, FW163 */
#define BRAM_US   3000u
#define VBLANK_US 16683u
#define DEC_US    3000u         /* the eye's decode of one frame (lgfx.c DEC_US caps a pump's share at 4 ms) */
#define CHUNK     2048u

static char root[] = "/tmp/omsat_lgpace_XXXXXX";
static char s_card[64];                                     /* this sequence's card: a fresh folder in root */
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];
static om_pgs_env env;
static uint32_t s_nonce;
static uint32_t nonce(void) { return 0xC0570000u + ++s_nonce; }

/* --- the model clock: every command since the last look, at its cost --------------------------------------------- */
static uint64_t s_t;
static long s_last[16];
static int s_paths_since;
static long cnt(int k)
{
    switch (k) {
    case 0: return fake_c_open + fake_c_stat + fake_c_opendir + fake_mkdir_calls + fake_c_rename + fake_c_unlink
                   + fake_c_chdir;
    case 1: return fake_c_dirent + fake_c_close + fake_c_write;
    case 2: return fake_rbytes + fake_wbytes;
    default: return fake_bram_calls;
    }
}
static void tick(void)
{
    long d[4];
    int k;
    for (k = 0; k < 4; k++) { d[k] = cnt(k) - s_last[k]; s_last[k] = cnt(k); }
    s_t += (uint64_t)d[0] * PATH_US + (uint64_t)d[1] * CMD_US + (uint64_t)d[2] * 1000000u / BPS
           + (uint64_t)d[3] * BRAM_US;
    s_paths_since += (int)d[0];
}
static void to_vblank(void) { s_t = (s_t / VBLANK_US + 1u) * VBLANK_US; }   /* om_lgap: the next V-blank's start */
static int in_vblank(uint64_t t) { return (t % VBLANK_US) >= 224u * VBLANK_US / 263u; }

/* --- the eye (lgfx.c om_lg_pump, modelled) ------------------------------------------------------------------------- */
static uint8_t *s_file;
static struct {
    om_lgr c;
    om_lbg g;
    uint32_t frame, frames;
    int ph, old;
    uint64_t us0, dma_t, flip_t, max_flip, at;
    int32_t credit;
} eye;
static uint64_t s_eye_us, s_wait_us, s_dec_us;
static int s_rd_wr;
static int s_trace;
static const char *s_stage = "start";
enum { PH_IDLE, PH_DMA, PH_READY };
static uint32_t next_of(uint32_t f) { return f + 1u >= eye.g.n ? 0u : f + 1u; }   /* om_lbg_next */

static void eye_start(uint32_t adopt)
{
    static uint32_t nm[16];
    uint32_t head, off, len;
    int n;
    memset(&eye.c, 0, sizeof eye.c);
    eye.c.file = s_file;
    strcpy((char *)nm, "/aether/LOADBG.BIN");
    CHECK((eye.c.fd = s_open((const char *)nm, FA_READ | FA_OPEN_EXISTING)) >= 0);
    CHECK((n = s_read(eye.c.fd, s_file, (int)CHUNK)) >= 64);
    eye.c.rp = (uint32_t)n;
    {
        s_stat_t st[4];
        CHECK(s_stat((const char *)nm, st, (int)sizeof st - 4) >= 0);
        eye.c.size = st[0].size;
    }
    head = om_lbg_head_bytes(s_file, (uint32_t)n);
    while (eye.c.rp < head) CHECK(om_lgr_read(&eye.c, head, CHUNK) > 0);
    CHECK(om_lbg_open(&eye.g, s_file, eye.c.rp, eye.c.size) == OM_OK);
    eye.frame = adopt;                                       /* lgfx.c adopt(): the menu's last frame is up */
    om_lbg_entry_of(&eye.g, next_of(adopt), &off, &len);
    CHECK_INT(om_lgr_from(&eye.c, off), 0);
    if (!eye.old) om_lbg_entry_of(&eye.g, 0, &eye.c.w0, &len);   /* lgfx.c: the wrap skips the keyframe */
    eye.ph = PH_IDLE;
    eye.frames = 0;
    eye.max_flip = 0;
    tick();
    eye.flip_t = eye.us0 = eye.at = s_t;
    eye.credit = OM_LGR_CREDIT_MAX;
}
static int next_in(void)
{
    uint32_t off, len;
    om_lbg_entry_of(&eye.g, next_of(eye.frame), &off, &len);
    return om_lgr_has(&eye.c, om_lbg_head_bytes(s_file, eye.c.size), off, len);
}
static void eye_read(int may_read)
{
    int rc;
    eye.credit = om_lgr_credit(eye.credit, (uint32_t)(s_t - eye.at));
    eye.at = s_t;
    if (eye.c.fd < 0 || !may_read) return;
    if (eye.old) {                                           /* before: one 2 KB read a pump, none while decoding */
        if ((rc = om_lgr_some(&eye.c, CHUNK)) <= 0) { s_close(eye.c.fd); eye.c.fd = -1; }
        tick();
        eye.at = s_t;
        return;
    }
    while (eye.credit > 0 && om_lgr_ahead(&eye.c, &eye.g, eye.frame, OM_LGR_AHEAD) < OM_LGR_AHEAD) {
        uint64_t t0 = s_t;
        if (om_yield_wr) s_rd_wr++;                          /* a LOADBG read while a file is open for write */
        rc = om_lgr_some(&eye.c, CHUNK);
        tick();
        eye.credit -= (int32_t)(s_t - t0);
        s_eye_us += s_t - t0;
        eye.at = s_t;
        if (rc <= 0) { s_close(eye.c.fd); eye.c.fd = -1; break; }
    }
}
static void flip(void)
{
    if (s_t - eye.flip_t > eye.max_flip) {
        eye.max_flip = s_t - eye.flip_t;
        if (s_trace) printf("      frame gap %llu ms at %s (t %llu ms, %u in RAM ahead)\n", (unsigned long long)(eye.max_flip / 1000u),
                            s_stage, (unsigned long long)(s_t / 1000u), om_lgr_ahead(&eye.c, &eye.g, eye.frame, 64));
    }
    eye.flip_t = s_t;
    eye.frame = next_of(eye.frame);
    eye.frames++;
    eye.ph = PH_IDLE;
}
static void play(void);
static void eye_pump(int may_read)
{
    uint32_t period = eye.g.fps == 60u ? 16000u : 32000u;
    tick();
    if (eye.old) {                                           /* lgfx.c before: a read, then one phase a call */
        eye_read(may_read);
        switch (eye.ph) {
        case PH_IDLE:
            if (!next_in()) return;
            s_t += DEC_US;
            eye.dma_t = s_t + 1000u;
            eye.ph = PH_DMA;
            return;
        case PH_DMA:
            if (s_t >= eye.dma_t) eye.ph = PH_READY;
            return;
        default:
            if (s_t - eye.flip_t < period) return;
            if (!in_vblank(s_t)) {
                if (s_t - eye.flip_t < period + 40000u) return;
                to_vblank();
            }
            flip();
            return;
        }
    }
    play();                                                  /* lgfx.c pump: the phases first, then the reads */
    eye_read(may_read);
}
static void play(void)
{
    uint32_t period = eye.g.fps == 60u ? 16000u : 32000u;
    for (;;) switch (eye.ph) {
    case PH_IDLE:
        if (!next_in()) return;
        s_t += DEC_US;
        s_dec_us += DEC_US;
        eye.dma_t = s_t + 1000u;
        eye.ph = PH_DMA;
        /* fall through */
    case PH_DMA:
        if (s_t < eye.dma_t) return;
        eye.ph = PH_READY;
        /* fall through */
    default:
        switch (om_lgr_flip((uint32_t)(s_t - eye.flip_t), period, in_vblank(s_t))) {
        case OM_LGR_LATER: return;
        case OM_LGR_WAIT: { uint64_t w = s_t; to_vblank(); s_wait_us += s_t - w; } break;
        default: break;
        }
        flip();
        break;                                               /* on to the next frame's decode */
    }
}

/* --- the chances, as lseq.c wires them ----------------------------------------------------------------------------- */
static uint64_t s_c_last, s_c_max;
static int s_p_max, s_chances, s_newer;
static char s_c_at[24];
static long s_tr_last[4];
static void chance(int may_read)
{
    tick();
    if (s_trace && (s_t - s_c_last > PATH_US + 2u * VBLANK_US || s_paths_since > 1)) {
        int k;
        printf("      gap %5llu ms at %-12s", (unsigned long long)((s_t - s_c_last) / 1000u), s_stage);
        for (k = 0; k < 4; k++) printf(" %ld", cnt(k) - s_tr_last[k]);
        printf("  (paths, cmds, bytes, bram)\n");
    }
    if (s_t - s_c_last > s_c_max) { s_c_max = s_t - s_c_last; snprintf(s_c_at, sizeof s_c_at, "%s", s_stage); }
    if (s_paths_since > s_p_max) s_p_max = s_paths_since;
    eye_pump(may_read);
    tick();
    { int k; for (k = 0; k < 4; k++) s_tr_last[k] = cnt(k); }
    s_paths_since = 0;
    s_c_last = s_t;
    s_chances++;
}
static int s_wr_bad;
static void wr_check(void) { if (om_yield_wr > 1u) s_wr_bad++; }   /* one write open at a time, balanced */
/* om_yield: the launcher's pacer (lseq.c): no read while a file is open for write (yield.h om_yield_wr) */
static void y_hook(void) { wr_check(); chance(!om_yield_wr); }
static void gap_hook(void) { chance(s_newer && !om_yield_wr); to_vblank(); }   /* gap_beat: om_lgap after the pump */
static void say_hook(const char *s) { (void)s; chance(s_newer && !om_yield_wr); }
static void prog_hook(const om_pgs_prog *p)
{
    s_stage = p->stage;
    chance(s_newer ? !om_yield_wr : !p->file_total || p->file_done >= p->file_total);
}

/* --- the card (test_pgscost.c's: the user's) ----------------------------------------------------------------------- */
static uint8_t *content(uint32_t size, uint32_t seed)
{
    static uint8_t b[4][40000];
    static int k;
    uint8_t *o = b[k++ & 3];
    uint32_t i;
    for (i = 0; i < size; i++) o[i] = (uint8_t)((i * 31u + seed * 7u) ^ (i >> 5));
    return o;
}
static void mkdirs_abs(char *p)
{
    char *s;
    for (s = p + strlen(s_card) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}
static void sd_bup(const char *dir, const char *name, uint32_t size, uint32_t seed, uint32_t date)
{
    char p[700];
    uint8_t h[OM_BUP_HDR];
    om_bup_dir d;
    FILE *f;
    memset(&d, 0, sizeof d);
    strncpy(d.name, name, 11);
    memcpy(d.comment, "TEST", 5);
    d.language = 1;
    d.date = date;
    d.datasize = size;
    om_bup_encode_hdr(&d, h);
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", s_card, dir);
    mkdirs_abs(p);
    snprintf(p, sizeof p, "%s/aether/SAVES/%s/%s.BUP", s_card, dir, name);
    f = fopen(p, "wb");
    fwrite(h, 1, sizeof h, f);
    fwrite(content(size, seed), 1, size, f);
    fclose(f);
}
#define NGEN 24
static void gen_name(char out[12], int i) { snprintf(out, 12, "GAME%03d_SAV", i); }
static uint32_t gen_size(int i) { return 150u + (uint32_t)i * 60u; }
static void card(const uint8_t *lbg, uint32_t lbg_n, int w)
{
    char cmd[400], nm[12], line[4096];
    int i, n;
    FILE *f;
    snprintf(s_card, sizeof s_card, "%s/%d", root, w);
    mkdir(s_card, 0755);
    setenv("FAKE_SD_ROOT", s_card, 1);
    snprintf(cmd, sizeof cmd, "%s/aether/SAVES", s_card);
    mkdirs_abs(cmd);
    snprintf(cmd, sizeof cmd, "%s/Some Game (USA)", s_card);
    mkdirs_abs(cmd);
    snprintf(cmd, sizeof cmd, "%s/aether/LAUNCHER.BIN", s_card);
    if ((f = fopen(cmd, "wb"))) { fputs("OMSATLNC", f); fclose(f); }
    snprintf(cmd, sizeof cmd, "%s/aether/LOADBG.BIN", s_card);
    if ((f = fopen(cmd, "wb"))) { fwrite(lbg, 1, lbg_n, f); fclose(f); }
    fake_reboot();
    fake_bram_reset();
    for (i = 0; i < NGEN; i++) {
        gen_name(nm, i);
        fake_bram_put(nm, content(gen_size(i), (uint32_t)i), gen_size(i), 1000u + (uint32_t)i);
    }
    fake_bram_put("NIGHTS_DATA", content(1200, 500), 1200, 2000);
    n = snprintf(line, sizeof line, "\nI");
    for (i = 0; i < NGEN; i++) {
        uint32_t seed = i % 6 != 5 ? (uint32_t)i : 300u + (uint32_t)i, date = i % 6 != 5 ? 1000u + (uint32_t)i : 900u;
        char nn[12];
        gen_name(nm, i);
        sd_bup("_GENERAL", nm, gen_size(i), seed, date);
        memset(nn, 0, 12);
        memcpy(nn, nm, 11);
        n += snprintf(line + n, sizeof line - (size_t)n, " %08X",
                      om_pgs_rec_id(nn, date, gen_size(i), om_crc32(0, content(gen_size(i), seed), gen_size(i))));
    }
    snprintf(line + n, sizeof line - (size_t)n, "\n");
    snprintf(cmd, sizeof cmd, "%s/aether/SAVES/GENLIST.TXT", s_card);
    if ((f = fopen(cmd, "wb"))) { fputs(line, f); fclose(f); }
    sd_bup("MK-81020[U]", "NIGHTS_DATA", 1200, 400, 1500);
    sd_bup("GS-9031[U]", "GHEROES_001", 1000, 600, 1600);
    sd_bup("GS-9031[U]", "GHEROES_CFG", 300, 601, 1601);
    for (i = 0; i < 100; i++) {
        char d[32];
        snprintf(d, sizeof d, "T-%05dG[J]", 10000 + i);
        sd_bup(d, "OTHER__SAVE", 500, 700u + (uint32_t)i, 1700);
    }
}

/* the card as text: every file under aether/SAVES, then the cksum of each but ACTIVE.TXT and the backups (they hold
 * the session nonce) -- the eye's hook must not change what the swap writes */
static char *card_text(void)
{
    char cmd[600], *buf = (char *)malloc(1 << 20);
    FILE *f;
    size_t n;
    snprintf(cmd, sizeof cmd, "cd %s/aether/SAVES && find . -type f | LC_ALL=C sort && find . -type f ! -name ACTIVE.TXT"
             " ! -name '*.BKP' | LC_ALL=C sort | xargs -d '\n' cksum", s_card);
    f = popen(cmd, "r");
    n = f ? fread(buf, 1, (1 << 20) - 1, f) : 0;
    if (f) pclose(f);
    buf[n] = 0;
    return buf;
}

typedef struct { uint64_t ms, chance_max, flip_max; int paths_max, chances, frames; char at[24]; } run_t;
static run_t swap(const char *what, uint8_t saves, const char *gid, int newer, uint32_t adopt)
{
    static uint32_t game[16];
    om_pgs_result r;
    run_t o;
    uint64_t t0;
    int rc;
    strcpy((char *)game, "/Some Game (USA)");
    s_chdir((const char *)game);                             /* lseq.c step 2 */
    s_newer = newer;
    eye.old = !newer;
    om_yield_fn = newer ? y_hook : 0;                         /* lseq.c screens_on (the fix) */
    eye_start(adopt);
    tick();
    s_paths_since = 0;
    s_eye_us = s_wait_us = s_dec_us = 0;
    t0 = s_c_last = s_t;
    { int k; for (k = 0; k < 4; k++) s_tr_last[k] = cnt(k); }
    s_c_max = 0; s_p_max = 0; s_chances = 0; s_stage = "start"; s_c_at[0] = 0;
    rc = om_pgs_run(&env, saves, 0, gid, &r);
    CHECK_INT(om_yield_wr, 0);                               /* every write's open matched by its close */
    chance(1);                                               /* lseq.c: the next step's pump */
    om_yield_fn = 0;
    CHECK_INT(rc, 0);
    s_chdir((const char *)game);
    if (eye.c.fd >= 0) { s_close(eye.c.fd); eye.c.fd = -1; }
    o.ms = (s_t - t0) / 1000u;
    o.chance_max = s_c_max;
    o.flip_max = eye.max_flip;
    o.paths_max = s_p_max;
    o.chances = s_chances;
    o.frames = (int)eye.frames;
    snprintf(o.at, sizeof o.at, "%s", s_c_at);
    printf("  %-5s %-40s swap %5llu ms  chances %4d  longest gap %4llu ms (%s), %d path cmds  eye: %3d frames, "
           "longest frame %4llu ms (reads %llu, decodes %llu, waits %llu ms)\n", newer ? "fixed" : "old", what, (unsigned long long)o.ms, o.chances,
           (unsigned long long)(o.chance_max / 1000u), o.at, o.paths_max, o.frames,
           (unsigned long long)(o.flip_max / 1000u), (unsigned long long)(s_eye_us / 1000u),
           (unsigned long long)(s_dec_us / 1000u), (unsigned long long)(s_wait_us / 1000u));
    return o;
}

int main(void)
{
    static const char *const what[3] = { "NiGHTS, first launch (general set out)", "NiGHTS again, nothing changed",
                                          "Guardian Heroes (NiGHTS archived)" };
    static const char *const gid[3] = { "MK-81020@U", "MK-81020@U", "GS-9031@U" };
    uint32_t n;
    uint8_t *lbg = th_load("../app/assets/LOADBG.BIN", &n);
    run_t a[2][3];
    char *txt[2];
    int k, w;
    uint32_t big = 0;                                        /* the loop's largest entry (frame 0's, ~25 KB) */
    if (!mkdtemp(root)) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("FAKE_SD_ROOT", root, 1);
    if (posix_memalign((void **)&s_file, 16, n + 8u) || !s_file) return 2;
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    env.gap = gap_hook;
    env.prog = prog_hook;
    env.say = say_hook;
    env.nonce = nonce;
    env.backup = 1;                                          /* lseq.c: the always-on BRAM backup */
    env.date = "";
    env.rel = 1;                                             /* lseq.c: names from /aether */
    fake_nopath = 1;
    s_trace = getenv("LGPACE_TRACE") != NULL;
    fake_r1_on = 1;
    fake_addonly = 1;
    printf("test_lgpace: the save swap on a card like the user's, %u ms a path command (model clock)\n", PATH_US / 1000u);
    for (w = 0; w < 2; w++) {                                /* the same sequence, old wiring then the fixed one */
        card(lbg, n, w);
        s_nonce = 0;
        for (k = 0; k < 3; k++) {
            if (k == 2) {                                    /* NiGHTS saved: GH's swap archives it */
                fake_bram_del("NIGHTS_DATA");
                fake_bram_put("NIGHTS_DATA", content(1200, 501), 1200, 2100);
            }
            a[w][k] = swap(what[k], 1, gid[k], w, 10u + 7u * (uint32_t)k);
        }
        txt[w] = card_text();
    }
    CHECK(strlen(txt[0]) > 1000u);
    CHECK_INT(s_wr_bad, 0);
    CHECK_INT(s_rd_wr, 0);                                  /* never a read between a write's open and close */
    CHECK(!strcmp(txt[0], txt[1]));                          /* the same card after both: the hook only draws and reads */
    for (k = 1; k <= (int)eye.g.n; k++) {
        const uint8_t *x = lbg + om_be32(lbg + 0x10) + 12u * (uint32_t)k;
        if (om_be32(x + 4) > big) big = om_be32(x + 4);
    }
    for (k = 0; k < 3; k++) {
        const run_t *o = &a[0][k], *f = &a[1][k];
        CHECK(o->paths_max > 1);                             /* the old wiring: path commands back to back (the cause) */
        CHECK(f->paths_max <= 1);                            /* fixed: never two path commands between two chances */
        CHECK(f->chance_max <= PATH_US + 2u * VBLANK_US);    /* nor a stretch over 2 V-blanks but one command */
        CHECK(f->frames > o->frames);                        /* the eye moves during the swap */
        /* the eye's share of the card (lgread.h): ~SHARE x 296 KB/s of ~8 KB frames, less the frames read ahead, and
         * never stuck: no frame held over ten periods. The swap takes at most 1 / (1 - share) as long (+5%) */
        CHECK((uint64_t)(f->frames + (int)OM_LGR_AHEAD) * 10240u * 100u * 10u >= f->ms * OM_LGR_SHARE * 296u * 8u);
        CHECK(f->flip_max <= 6u * 32000u + (uint64_t)big * 1000000u * 100u / (BPS * OM_LGR_SHARE));
        CHECK(f->ms * (100u - OM_LGR_SHARE) <= o->ms * 105u);
    }
    return th_done("test_lgpace");
}
