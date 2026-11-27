#include <sys/stat.h>
#include "th.h"
#include "handoff.h"
#include "info.h"
#include "launch.h"
#include "omlayout.h"

extern int fake_reads;
static uint32_t hdr32[OM_LNCH_HDR / 4], body32[4096];
static om_handoff ho;

static void put(const char *root, const char *rel, const uint8_t *d, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

static void make_launcher(const char *root, int corrupt)
{
    uint8_t file[OM_LNCH_HDR + 1000];
    uint32_t i;
    for (i = 0; i < 1000; i++) file[OM_LNCH_HDR + i] = (uint8_t)(i * 7);
    om_lnch_make(file, 1000, om_crc32(0, file + OM_LNCH_HDR, 1000));
    if (corrupt) file[OM_LNCH_HDR + 500] ^= 1;
    put(root, "aether/LAUNCHER.BIN", file, sizeof file);
}

int main(void)
{
    char root[] = "/tmp/omsat_launch_XXXXXX", p[600], dir[260], img[260];
    om_launch_bufs b = { (uint8_t *)hdr32, (uint8_t *)body32, sizeof body32, &ho };
    om_launch_info li;
    om_lib_entry e;
    uint8_t sec[2048];
    int before;
    om_launch_opts opt;
    memset(&opt, 0, sizeof opt);
    opt.loader = 2;                                                 /* OM_LDR_JHL (settings.h): M6 Task 17 */
    opt.igr = 1;
    memcpy(opt.gid, "T-15015G@J", 11);
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root); mkdir(p, 0755);
    CHECK_INT(om_launch_split("RPG/Grandia/disc1.cue", dir, img), OM_OK);
    CHECK_STR(dir, "/RPG/Grandia"); CHECK_STR(img, "disc1.cue");
    CHECK_INT(om_launch_split("unknown.iso", dir, img), OM_OK);
    CHECK_STR(dir, "/"); CHECK_STR(img, "unknown.iso");
    CHECK(om_launch_prepare("a.iso", 4, 0, &opt, &b, &li) < 0);           /* LAUNCHER.BIN missing */
    CHECK_INT(li.clobbered, 0);
    make_launcher(root, 0);
    CHECK_INT(om_launch_prepare("RPG/Grandia/disc1.cue", 4, 0, &opt, &b, &li), OM_OK);
    CHECK_INT(li.clobbered, 1); CHECK_INT(li.body_size, 1000);
    CHECK_INT(om_handoff_check(&ho), OM_OK);
    CHECK_STR(ho.dir, "/RPG/Grandia"); CHECK_STR(ho.image, "disc1.cue"); CHECK_INT(ho.area_code, 4);
    CHECK_INT(ho.loader, 2); CHECK_INT(ho.igr, 1); CHECK_STR(ho.gid, "T-15015G@J");   /* the options */
    CHECK(om_launch_begin("x.iso", 4, 0, NULL, &b, &li) < 0);       /* no options: refused, never an unresolved loader */
    CHECK_INT(((uint8_t *)body32)[999], (uint8_t)(999 * 7));
    make_launcher(root, 1);
    CHECK_INT(om_launch_prepare("x.iso", 4, 0, &opt, &b, &li), OM_E_CRC);
    CHECK_INT(li.clobbered, 1);                                     /* LWRAM data must be reloaded */
    put(root, "aether/LAUNCHER.BIN", (const uint8_t *)"OMSATLNX", 8);
    CHECK(om_launch_prepare("x.iso", 4, 0, &opt, &b, &li) < 0);
    CHECK_INT(li.clobbered, 0);
    {   /* the phases the loading screen runs one per frame give the same results and error codes */
        om_launch_ld ld;
        int r, calls = 0;
        CHECK_INT(om_launch_begin("RPG/Grandia/disc1.cue", 4, 0, &opt, &b, &li), OM_OK);   /* step 1: the hand-off block */
        CHECK_INT(li.clobbered, 0);
        CHECK_INT(om_handoff_check(&ho), OM_OK);
        CHECK_STR(ho.image, "disc1.cue");
        CHECK(om_launch_open(&ld, &b, &li) < 0);                       /* LAUNCHER.BIN is 8 B: too short */
        CHECK_INT(ld.fd, -1);
        CHECK_INT(li.clobbered, 0);
        make_launcher(root, 0);
        memset(body32, 0, sizeof body32);
        CHECK_INT(om_launch_open(&ld, &b, &li), OM_OK);
        CHECK_INT(li.clobbered, 0);                                     /* nothing of the body read yet */
        while ((r = om_launch_read(&ld, &b, &li, 256)) > 0) calls++;   /* 4 KB header + 1000 B in 256 B steps */
        CHECK_INT(r, OM_OK);
        CHECK_INT(calls, 16 + 3);
        CHECK_INT(li.clobbered, 1);
        CHECK_INT(li.body_size, 1000);
        CHECK_INT(((uint8_t *)body32)[999], (uint8_t)(999 * 7));
        CHECK_INT(ld.fd, -1);                                           /* closed when done */
        make_launcher(root, 1);                                         /* a corrupt body: the CRC of the steps */
        CHECK_INT(om_launch_open(&ld, &b, &li), OM_OK);
        while ((r = om_launch_read(&ld, &b, &li, 256)) > 0) ;
        CHECK_INT(r, OM_E_CRC);
        CHECK_INT(ld.fd, -1);
        CHECK_INT(om_launch_begin("x.iso", 4, 0, &opt, &b, &li), OM_OK);
        CHECK_INT(om_launch_open(&ld, &b, &li), OM_OK);                 /* cancel part-way: the file is closed */
        CHECK_INT(om_launch_read(&ld, &b, &li, 256), 1);
        CHECK_INT(li.clobbered, 0);                                     /* only the header so far */
        om_launch_abort(&ld);
        CHECK_INT(ld.fd, -1);
        CHECK(om_launch_read(&ld, &b, &li, 256) < 0);
        CHECK(om_launch_begin("", 4, 0, &opt, &b, &li) < 0);                  /* an empty image name: step 1 fails */
        {   /* a 4 KB file with a bad magic: the header step names it, before any body byte is written */
            static uint8_t junk[OM_LNCH_HDR + 16];
            memset(junk, 0x55, sizeof junk);
            put(root, "aether/LAUNCHER.BIN", junk, sizeof junk);
            CHECK_INT(om_launch_begin("x.iso", 4, 0, &opt, &b, &li), OM_OK);   /* begin resets li */
            CHECK_INT(om_launch_open(&ld, &b, &li), OM_OK);
            while ((r = om_launch_read(&ld, &b, &li, 2048)) > 0) ;
            CHECK(r < 0);
            CHECK_INT(li.clobbered, 0);
            CHECK_INT(ld.fd, -1);
        }
    }
    /* IP.BIN details cache */
    memset(sec, ' ', 0xD0); memset(sec + 0xD0, 0, sizeof sec - 0xD0);
    memcpy(sec, "SEGA SEGASATURN ", 16); memcpy(sec + 0x20, "T-15015G", 8); memcpy(sec + 0x60, "FIST", 4);
    memcpy(sec + 0x30, "19961001", 8);
    put(root, "fist.iso", sec, sizeof sec);
    memset(&e, 0, sizeof e);
    e.path = "fist.iso"; e.title = "";
    om_info_reset();
    CHECK(om_info_find(7) == NULL);
    CHECK_INT(om_info_load(&e, 7), OM_OK);
    CHECK(om_info_find(7) != NULL);
    CHECK_STR(om_info_find(7)->title, "FIST");
    before = fake_reads;
    CHECK_INT(om_info_load(&e, 7), OM_OK);                          /* cached: no second read */
    CHECK_INT(fake_reads, before);
    e.path = "nope.iso";
    CHECK(om_info_load(&e, 8) < 0);
    CHECK_INT(om_info_known(8), 1);                                 /* failure cached too: no retry loop */
    CHECK(om_info_find(8) == NULL);
    /* the menu's run-time cap on the LAUNCHER.BIN body ends where LOADBG.BIN's LW
     * region begins (launch.c rejects body + 4 > cap with OM_E_RANGE), the same bound as sbuild's body_below_loadbg */
    CHECK_INT(OM_LAUNCHER_BODY_CAP, 0x68800);
    CHECK_INT(OM_LAUNCHER_LOAD + OM_LAUNCHER_BODY_CAP, OM_LW_LBG);
    CHECK(OM_LAUNCHER_BODY_CAP <= OM_LAUNCHER_MAX);
    return th_done("test_launch");
}
