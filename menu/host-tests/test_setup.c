/* menu/host-tests/test_setup.c -- /aether/DIAG.TXT is written only when the setup differs (menu/app/setupw.c through
 * the real sd.c/sdwrite.c over fake_satiator.c, which enforces 4-byte alignment and libsatiator's 2 KB per call): the
 * same setup on a later boot (only DATE= moved) issues no write, unlink or rename; a changed field or a missing file
 * is written and verified; a failed read writes nothing; nothing outside /aether/DIAG.TXT is touched. BSD-3-Clause. */
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include "th.h"
#include "setup.h"
#include "setupid.h"
#include "sd.h"
#include "satiator.h"

extern int fake_c_write, fake_c_rename, fake_c_unlink;
extern int fake_fail_op, fake_fail_skip, fake_fail_times, fake_fail_rc;

static uint32_t s_buf32[2048 / 4 + 1];
static uint32_t s_txt32[OM_SU_CAP / 4];
#define BUF ((uint8_t *)s_buf32)
#define TXT ((char *)s_txt32)

static long slurp(const char *root, const char *rel, char *out, long cap)
{
    char p[512];
    FILE *f;
    long n;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    if (!(f = fopen(p, "rb"))) return -1;
    n = (long)fread(out, 1, (size_t)cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

static int count(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

static uint32_t make(const char *rtc_s, uint32_t bram_free)
{   /* a setup's text into TXT (4-aligned, as setuphw.c's is) */
    static const uint8_t bios[16] = { 'B','T','R','_','1','.','0','1','9','1','9','4','1','2','2','8' };
    om_su_raw r;
    memset(&r, 0, sizeof r);
    r.aether = "0.1 test";
    r.fw = "163 2505030818";
    r.bios = bios;
    r.area = 1;
    r.cart_id = 0xFF;
    r.smpc_cond = 0xC0;
    memcpy(r.rtc, rtc_s, 7);
    r.nport[0] = 1; r.port[0][0] = 0x02;
    r.bram.total = 512; r.bram.free = bram_free; r.bram.kb = 32;
    r.cart_bram.rc = 1;
    r.games = 12;
    return om_su_text(&r, TXT, OM_SU_CAP);
}

static int writes(void) { return fake_c_write + fake_c_rename + fake_c_unlink; }

int main(void)
{
    char root[] = "/tmp/omsat_setup_XXXXXX", p[600], got[4096];
    uint32_t n;
    int w0;
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root);
    mkdir(p, 0755);

    /* boot 1: no file yet -> written, byte for byte */
    n = make("\x20\x26\x3B\x25\x14\x03\x05", 509);
    w0 = writes();
    CHECK_INT(om_su_save(TXT, n, BUF, sizeof s_buf32), 1);
    CHECK(writes() > w0);
    CHECK_INT(slurp(root, "aether/DIAG.TXT", got, sizeof got), (long)n);
    CHECK(!memcmp(got, TXT, n));
    CHECK(strstr(got, "DATE=2026-11-25 14:03:05\n") != NULL);
    CHECK_INT(count(p), 1);                            /* no DIAG.TXT.NEW left beside it */
    CHECK_INT(count(root), 1);                         /* the card root holds only aether/ */

    /* boot 2: the same setup, a later clock -> no write, no unlink, no rename; the old DATE stays */
    n = make("\x20\x26\x3B\x26\x09\x30\x00", 509);
    w0 = writes();
    CHECK_INT(om_su_save(TXT, n, BUF, sizeof s_buf32), 0);
    CHECK_INT(writes(), w0);
    slurp(root, "aether/DIAG.TXT", got, sizeof got);
    CHECK(strstr(got, "DATE=2026-11-25 14:03:05\n") != NULL);

    /* boot 3: a save was written since (BRAM free 508) -> rewritten, with this boot's DATE */
    n = make("\x20\x26\x3B\x27\x10\x00\x00", 508);
    w0 = writes();
    CHECK_INT(om_su_save(TXT, n, BUF, sizeof s_buf32), 1);
    CHECK(writes() > w0);
    CHECK_INT(slurp(root, "aether/DIAG.TXT", got, sizeof got), (long)n);
    CHECK(!memcmp(got, TXT, n));
    CHECK(strstr(got, "BRAM=508/512 32KB\n") != NULL);
    CHECK_INT(count(p), 1);

    /* a read that fails (not "absent") writes nothing: a misbehaving card is left alone */
    n = make("\x20\x26\x3B\x27\x10\x00\x00", 400);
    fake_fail_op = 'T'; fake_fail_skip = 0; fake_fail_times = 1; fake_fail_rc = -1;   /* its s_stat: FR_DISK_ERR */
    w0 = writes();
    CHECK(om_su_save(TXT, n, BUF, sizeof s_buf32) < 0);
    CHECK_INT(writes(), w0);
    fake_fail_op = 0;
    slurp(root, "aether/DIAG.TXT", got, sizeof got);
    CHECK(strstr(got, "BRAM=508/512 32KB\n") != NULL);

    /* a file too big for the buffer (not ours) is replaced */
    {
        FILE *f;
        snprintf(p, sizeof p, "%s/aether/DIAG.TXT", root);
        f = fopen(p, "wb");
        for (int i = 0; i < 3000; i++) fputc('x', f);
        fclose(f);
        CHECK_INT(om_su_save(TXT, n, BUF, sizeof s_buf32), 1);
        CHECK_INT(slurp(root, "aether/DIAG.TXT", got, sizeof got), (long)n);
    }

    printf("test_setup: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
