/* menu/host-tests/test_lgread.c -- the loading screen's LOADBG.BIN reads (menu/launcher/lgread.c) over fake_satiator.c,
 * which aborts on an unaligned s_read buffer as the console does (a CPU address error in s_read's word copy).
 * Hardware 2026-11-25 (Mortal Kombat II, FW163): "EXC: CPU address error PC=00218E08", step 1 -- the launch adopted the
 * menu's last eye frame and read on from the next frame's entry, an unaligned offset, straight into OM_LG_FILE + off.
 * Here: the release LOADBG.BIN, each of its frames adopted in turn (as lgfx.c om_lg_start), read to the end and wrapped
 * back to the head (as om_lg_pump): every buffer aligned, the whole file in RAM at its own offsets, every frame's entry
 * counted as in (om_lgr_has, lgfx.c entry_in) -- with s1 taken down to a word, the frame before
 * the adopted one ends past s1 and is in only once the wrap has joined the two parts. BSD-3-Clause. */
#include <stdlib.h>
#include <unistd.h>
#include "th.h"
#include "loadbg.h"
#include "om_fmt.h"
#include "satiator.h"
#include "../launcher/lgread.c"

#define CHUNK 2048u

static uint32_t s_name[76];
static const char *al(const char *p)
{
    memset(s_name, 0, sizeof s_name);
    strncpy((char *)s_name, p, sizeof s_name - 1);
    return (const char *)s_name;
}

int main(void)
{
    static char root[] = "/tmp/lgreadXXXXXX";
    char path[96];
    uint32_t size, head, nx, off, len, unaligned = 0, e, eoff, elen;
    uint8_t *want = th_load("../app/assets/LOADBG.BIN", &size), *ram = NULL;
    FILE *f;
    om_lbg g;
    om_lgr c;
    int n, k, fd;
    static uint8_t seen[256];

    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(path, sizeof path, "%s/LOADBG.BIN", root);
    f = fopen(path, "wb");
    CHECK(f && fwrite(want, 1, size, f) == size);
    fclose(f);
    head = om_lbg_head_bytes(want, size);
    CHECK(head % 4u == 0u);                                  /* 592 + 12 (n + 1): word-aligned by the format */
    CHECK(om_lbg_open(&g, want, size, size) == OM_OK);
    for (nx = 0; nx < g.n; nx++) {
        om_lbg_entry_of(&g, nx, &off, &len);
        unaligned += (off & 3u) != 0u;
    }
    CHECK(unaligned > 0u);                                   /* the case that faulted: still in the release file */
    if (posix_memalign((void **)&ram, 16, size + 8u) != 0 || !ram) return 2;

    for (nx = 0; nx < g.n; nx++) {
        memset(ram, 0xEE, size + 8u);
        memset(&c, 0, sizeof c);
        c.file = ram;
        c.size = size;
        /* lgfx.c file_head: the first 2 KB, then the rest of the head */
        CHECK((fd = s_open(al("/LOADBG.BIN"), FA_READ | FA_OPEN_EXISTING)) >= 0);
        c.fd = fd;
        CHECK((n = s_read(fd, ram, (int)CHUNK)) >= 64);
        c.rp = (uint32_t)n;
        while (c.rp < head && c.rp < size)
            CHECK(om_lgr_read(&c, head, CHUNK) > 0);
        CHECK(om_lbg_open(&g, ram, c.rp, size) == OM_OK);
        c.s1 = 0;
        c.e1 = c.rp;
        /* om_lg_start's adopt: read on from the next frame's entry, then wrap */
        om_lbg_entry_of(&g, nx, &off, &len);
        CHECK_INT(om_lgr_from(&c, off), 0);
        CHECK(c.rp % 4u == 0u && c.rp <= off && off - c.rp < 4u);
        CHECK(om_lgr_has(&c, head, off, len) == 0 || len == 0);      /* nothing of the frame is in yet */
        memset(seen, 0, sizeof seen);
        for (k = 0; k < 4096 && (n = om_lgr_some(&c, CHUNK)) == 1; k++)
            for (e = 0; e <= g.n && e < 256u; e++) {          /* an entry counted as in has its bytes in RAM */
                const uint8_t *x = want + om_be32(want + 0x10) + 12u * e;
                eoff = om_be32(x);
                elen = om_be32(x + 4);
                if (!seen[e] && om_lgr_has(&c, head, eoff, elen)) {
                    seen[e] = 1;
                    if (memcmp(ram + eoff, want + eoff, elen)) { CHECK(0); printf("  frame %u: entry %u early\n", nx, e); }
                }
            }
        CHECK_INT(n, 0);
        CHECK(c.wrap == (c.s1 > head));
        CHECK(memcmp(ram, want, size) == 0);
        for (e = 0; e <= g.n; e++) {
            const uint8_t *x = want + om_be32(want + 0x10) + 12u * e;
            eoff = om_be32(x);
            elen = om_be32(x + 4);
            if (!om_lgr_has(&c, head, eoff, elen)) { CHECK(0); printf("  frame %u: entry %u not in\n", nx, e); break; }
        }
        s_close(fd);
    }

    /* hardware 2026-11-27 (the launch's eye stalled on the wrap): w0 = entry 1's offset, so the wrap after an adopted start
     * skips the 45 KB keyframe a launch never shows. Every loop entry is in (and right) once the reads end; entry 0 is
     * never counted as in, and its bytes are never read */
    {
        uint32_t k0off, k0len, e1off, e1len;
        const uint8_t *x0 = want + om_be32(want + 0x10);
        k0off = om_be32(x0); k0len = om_be32(x0 + 4);
        memset(ram, 0xEE, size + 8u);
        memset(&c, 0, sizeof c);
        c.file = ram;
        c.size = size;
        CHECK((c.fd = s_open(al("/LOADBG.BIN"), FA_READ | FA_OPEN_EXISTING)) >= 0);
        CHECK((n = s_read(c.fd, ram, (int)CHUNK)) >= 64);
        c.rp = (uint32_t)n;
        while (c.rp < head) CHECK(om_lgr_read(&c, head, CHUNK) > 0);
        c.s1 = 0;
        c.e1 = c.rp;
        om_lbg_entry_of(&g, 10, &off, &len);
        CHECK_INT(om_lgr_from(&c, off), 0);
        om_lbg_entry_of(&g, 0, &e1off, &e1len);                /* frame 0's entry: index entry 1 */
        CHECK(e1off == k0off + k0len);
        c.w0 = e1off;
        while ((n = om_lgr_some(&c, CHUNK)) == 1) ;
        CHECK_INT(n, 0);
        CHECK(c.wrap == 1u && c.ws == (e1off & ~3u));
        CHECK(om_lgr_has(&c, head, k0off, k0len) == 0);          /* the keyframe: not in */
        for (e = 1; e <= g.n; e++) {
            const uint8_t *x = x0 + 12u * e;
            eoff = om_be32(x);
            elen = om_be32(x + 4);
            CHECK(om_lgr_has(&c, head, eoff, elen) == 1);
            if (memcmp(ram + eoff, want + eoff, elen)) { CHECK(0); printf("  w0: entry %u wrong\n", e); }
        }
        CHECK(memcmp(ram + k0off + k0len - 64u, want + k0off + k0len - 64u, 64) != 0);   /* its end: never read */
        CHECK(om_lgr_ahead(&c, &g, 10, 63) == 63u);
        s_close(c.fd);
    }
    CHECK_INT(om_lgr_flip(1000, 32000, 1), OM_LGR_LATER);          /* before the period: never */
    CHECK_INT(om_lgr_flip(33000, 32000, 1), OM_LGR_FLIP);          /* in the V-blank: now */
    CHECK_INT(om_lgr_flip(33000, 32000, 0), OM_LGR_LATER);         /* outside it: a later pump */
    CHECK_INT(om_lgr_flip(32000 + OM_LGR_LATE_US, 32000, 0), OM_LGR_WAIT);   /* late: wait for it */
    CHECK_INT(om_lgr_credit(0, 100000), (int32_t)(100000 * OM_LGR_SHARE / 100) < OM_LGR_CREDIT_MAX
              ? (int32_t)(100000 * OM_LGR_SHARE / 100) : OM_LGR_CREDIT_MAX);
    CHECK_INT(om_lgr_credit(-5000, 4000), -5000 + (int32_t)(4000 * OM_LGR_SHARE / 100));
    CHECK_INT(om_lgr_credit(OM_LGR_CREDIT_MAX, 1000000), OM_LGR_CREDIT_MAX);

    /* an odd start (a short read left rp inside a word): the next read goes back to the word, never unaligned */
    memset(&c, 0, sizeof c);
    c.file = ram;
    c.size = size;
    CHECK((c.fd = s_open(al("/LOADBG.BIN"), FA_READ | FA_OPEN_EXISTING)) >= 0);
    c.rp = 7u;
    CHECK(s_seek(c.fd, 7, C_SEEK_SET) == 7);
    CHECK((n = om_lgr_read(&c, size, CHUNK)) == (int)CHUNK);
    CHECK_INT(c.rp, 4u + CHUNK);
    CHECK(memcmp(ram + 4, want + 4, CHUNK) == 0);
    s_close(c.fd);

    unlink(path);
    rmdir(root);
    printf("test_lgread: %d frames adopted (%u at an unaligned entry), %d passed, %d failed\n", (int)g.n, unaligned,
           th_pass, th_fail);
    return th_fail || !th_pass;
}
