#include <unistd.h>
#include <sys/stat.h>
#include "th.h"
#include "cdparse.h"
#include "satiator.h"

typedef struct { uint32_t start, length, file_offset, fn_off; uint16_t flags, secsize; uint8_t track, index, q; } seg;

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int load_desc(const char *path, seg *s, int max, char names[][64])
{
    uint32_t n;
    uint8_t *d = th_load(path, &n);
    int k, nseg = le16(d);
    for (k = 0; k < nseg && k < max; k++) {
        const uint8_t *p = d + 2 + k * sizeof(seg_desc_t);
        s[k].start = le32(p); s[k].length = le32(p + 4); s[k].file_offset = le32(p + 8); s[k].fn_off = le32(p + 12);
        s[k].flags = le16(p + 16); s[k].secsize = le16(p + 18); s[k].track = p[20]; s[k].index = p[21]; s[k].q = p[22];
        memcpy(names[k], d + s[k].fn_off + 1, d[s[k].fn_off]);
        names[k][d[s[k].fn_off]] = 0;
    }
    free(d);
    return nseg;
}

static void zeros(const char *name, uint32_t n)
{
    FILE *f = fopen(name, "wb");
    static uint8_t z[2352];
    while (n >= sizeof z) { fwrite(z, 1, sizeof z, f); n -= sizeof z; }
    fwrite(z, 1, n, f);
    fclose(f);
}

static void text(const char *name, const char *s) { FILE *f = fopen(name, "w"); fputs(s, f); fclose(f); }

int main(void)
{
    char root[] = "/tmp/omsat_desc_XXXXXX", names[8][64];
    seg s[8];
    int n, k;
    uint32_t total = 0;
    CHECK(mkdtemp(root) != NULL);
    CHECK_INT(chdir(root), 0);                         /* the launcher s_chdir()s into the game folder */
    CHECK_INT(sizeof(seg_desc_t), 23);                 /* packed (satiator.h); measured, not assumed */
    zeros("game.iso", 2048u * 300u);
    CHECK_INT(image2desc("game.iso", "out.desc"), 0);
    n = load_desc("out.desc", s, 8, names);
    CHECK_INT(n, 1);
    CHECK_INT(s[0].start, 150); CHECK_INT(s[0].length, 300); CHECK_INT(s[0].secsize, 2048);
    CHECK_INT(s[0].track, 1); CHECK_INT(s[0].index, 1); CHECK_INT(s[0].q, 0x41);
    CHECK_STR(names[0], "game.iso");
    zeros("t1.bin", 2352u * 300u);
    zeros("t2.bin", 2352u * 450u);
    text("multi.cue", "FILE \"t1.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                      "FILE \"t2.bin\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n    INDEX 01 00:02:00\r\n");
    CHECK_INT(image2desc("multi.cue", "out2.desc"), 0);
    n = load_desc("out2.desc", s, 8, names);
    CHECK(n >= 2);
    CHECK_INT(s[0].start, 150);                         /* research §1.4: segments laid out from FAD 150 */
    for (k = 0; k < n; k++) {
        if (k) CHECK_INT(s[k].start, s[k - 1].start + s[k - 1].length);   /* contiguous */
        total += s[k].length;
        CHECK_INT(s[k].secsize, 2352);
    }
    CHECK_INT(total, 750);
    CHECK_INT(s[0].track, 1); CHECK_INT(s[0].q, 0x41); CHECK_STR(names[0], "t1.bin");
    CHECK_INT(s[n - 1].track, 2); CHECK_INT(s[n - 1].index, 1); CHECK_INT(s[n - 1].q, 0x01); CHECK_STR(names[n - 1], "t2.bin");
    printf("test_desc: 2-track cue -> %d segments (measured)\n", n);
    /* local change (launcher/UPSTREAM.md): a cue two folders down names its tracks from its own folder */
    CHECK_INT(mkdir("Set (Disc 1)", 0755), 0);
    CHECK_INT(mkdir("Set (Disc 1)/JP FILES", 0755), 0);
    zeros("Set (Disc 1)/JP FILES/d1.bin", 2352u * 100u);
    text("Set (Disc 1)/JP FILES/d1.cue", "FILE \"d1.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n");
    CHECK_INT(image2desc("Set (Disc 1)/JP FILES/d1.cue", "out1.desc"), 0);
    n = load_desc("out1.desc", s, 8, names);
    CHECK_INT(n, 1);
    CHECK_STR(names[0], "Set (Disc 1)/JP FILES/d1.bin");
    CHECK_INT(s[0].length, 100);
    {   /* an absolute cue path keeps its absolute folder */
        char abs[160], want[160];
        snprintf(abs, sizeof abs, "%s/Set (Disc 1)/JP FILES/d1.cue", root);
        snprintf(want, sizeof want, "%s/Set (Disc 1)/JP FILES/d1.bin", root);
        CHECK_INT(image2desc(abs, "out2.desc"), 0);
        n = load_desc("out2.desc", s, 8, names);
        CHECK_INT(n, 1);
        CHECK_STR(names[0], want);
    }
    CHECK(image2desc("missing.cue", "x.desc") != 0);
    CHECK_STR(cdparse_error_string, "Could not open specified cue file");
    CHECK(image2desc("game.zip", "x.desc") != 0);
    CHECK(cdparse_error_string && !strncmp(cdparse_error_string, "Unrecognised file extension", 27));
    return th_done("test_desc");
}
