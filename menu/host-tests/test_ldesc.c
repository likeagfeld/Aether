/* menu/host-tests/test_ldesc.c -- the launcher's descriptors (launcher/ldesc.h): out.desc for one disc; for a set,
 * out1..out<n>.desc, the stale out<n+1>..out9.desc deleted, out<cur>.desc emulated; a disc that does not parse
 * launches the chosen one alone. Real cue2desc on a temp folder; s_unlink is unlink() here. */
#include <unistd.h>
#include <sys/stat.h>
#include "th.h"
#include "ldesc.h"
#include "satiator.h"
#include "settings.h"
#include <stddef.h>

static int unlinks;
static char s_root[64];
int s_unlink(const char *f) { unlinks++; return unlink(f) == 0 ? 0 : -FR_NO_FILE; }
int s_chdir(const char *f)                               /* the card's "/" is the temp folder */
{
    char p[512];
    snprintf(p, sizeof p, "%s%s", s_root, f);
    return chdir(p) == 0 ? 0 : -FR_NO_PATH;
}

static void file(const char *name, const char *s) { FILE *f = fopen(name, "w"); fputs(s, f); fclose(f); }
static int exists(const char *name) { struct stat st; return stat(name, &st) == 0; }

static void set_block(om_handoff *h, const char *image, uint8_t n, uint8_t cur, const char *const *discs)
{
    om_launch_opts o;
    uint32_t k, off = 0;
    memset(&o, 0, sizeof o);
    o.loader = OM_LDR_JHL;
    CHECK_INT(om_handoff_fill2(h, "/", image, 1, 0, &o), OM_OK);
    h->ndisc = n; h->cur = cur;
    for (k = 0; k < n; k++) { strcpy(h->discs + off, discs[k]); off += (uint32_t)strlen(discs[k]) + 1; }
    h->crc = om_crc32(0, h, (uint32_t)offsetof(om_handoff, crc));
    CHECK_INT(om_handoff_check(h), OM_OK);
}

int main(void)
{
    char root[] = "/tmp/omsat_ldesc_XXXXXX", name[12], dir[256];
    static const char *const d3[3] = { "G (Disc 1)/g.iso", "G (Disc 2)/g.iso", "G (Disc 3)/g.iso" };
    static const char *const bad[3] = { "G (Disc 1)/g.iso", "G (Disc 2)/missing.cue", "G (Disc 3)/g.iso" };
    om_handoff h;
    int alone;
    CHECK(mkdtemp(root) != NULL);
    CHECK_INT(chdir(root), 0);
    strcpy(s_root, root);
    CHECK_INT(mkdir("G (Disc 1)", 0755), 0); CHECK_INT(mkdir("G (Disc 2)", 0755), 0); CHECK_INT(mkdir("G (Disc 3)", 0755), 0);
    file("G (Disc 1)/g.iso", "1"); file("G (Disc 2)/g.iso", "2"); file("G (Disc 3)/g.iso", "3");
    file("out4.desc", "stale"); file("out9.desc", "stale"); file("solo.iso", "s");
    /* one disc: out.desc, nothing deleted */
    set_block(&h, "solo.iso", 0, 0, 0);
    CHECK_INT(om_ldesc_write(&h, name, dir, &alone), 0);
    CHECK_STR(name, "out.desc"); CHECK_INT(alone, 0); CHECK_INT(unlinks, 0); CHECK_STR(dir, "/");
    CHECK(exists("out.desc") && exists("out4.desc") && exists("out9.desc"));
    /* a set of three, disc 2 chosen: out1..out3, the stale out4..out9 gone, out2.desc emulated */
    set_block(&h, d3[1], 3, 2, d3);
    CHECK_INT(om_ldesc_write(&h, name, dir, &alone), 0);
    CHECK_STR(name, "out2.desc"); CHECK_INT(alone, 0); CHECK_STR(dir, "/");
    CHECK(exists("out1.desc") && exists("out2.desc") && exists("out3.desc"));
    CHECK(!exists("out4.desc") && !exists("out9.desc"));
    CHECK_INT(unlinks, 6);                                  /* out4..out9, there or not */
    /* a disc that does not parse: the chosen disc alone, as out.desc */
    unlink("out.desc");
    set_block(&h, bad[0], 3, 1, bad);
    CHECK_INT(om_ldesc_write(&h, name, dir, &alone), 0);
    CHECK_STR(name, "out.desc"); CHECK_INT(alone, 2);
    CHECK_STR(dir, "/G (Disc 1)");                          /* from the chosen disc's own folder */
    CHECK(exists("out.desc") && exists("g.iso"));          /* the working folder is that folder now */
    CHECK_INT(chdir(root), 0);
    /* the chosen disc itself does not parse: an error, as a one-disc launch */
    set_block(&h, bad[1], 3, 2, bad);
    CHECK(om_ldesc_write(&h, name, dir, &alone) != 0);
    CHECK_INT(alone, 2);
    return th_done("test_ldesc");
}
