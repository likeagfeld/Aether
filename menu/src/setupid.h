/* menu/src/setupid.h -- the console setup the menu detects at start-up, as /aether/DIAG.TXT (the Card Manager's
 * feedback form pre-fills its "Your setup" fields from it). This half is pure C over raw readings (host-tested,
 * menu/host-tests/test_setupid.c); menu/app/setuphw.c takes the readings, menu/app/setupw.c writes the file. The format
 * is documented in setupid.c. BSD-3-Clause. */
#ifndef OM_SETUPID_H
#define OM_SETUPID_H
#include <stdint.h>

#define OM_SU_PATH  "/aether/DIAG.TXT"
#define OM_SU_CAP   1024u           /* the text's buffer; the file is ~400 B */
#define OM_SU_TAPS  6u              /* devices listed per port (a 6-player multitap) */

typedef struct { int rc; uint32_t total, free, kb; } om_su_bram;   /* rc 0 ok, 1 absent, 2 unformatted, < 0 unknown */

typedef struct {
    const char *aether;             /* "0.1 <build id>" */
    const char *fw;                 /* s_get_fw_version's string, NULL when it could not be read */
    const uint8_t *bios;            /* the 16 bytes at BIOS ROM + 0x800, NULL = unknown */
    const uint8_t *cs0;             /* the first 0x80 bytes of A-bus CS0 (a boot cartridge's header) */
    uint8_t area, tvstat_pal, cart_id, smpc_cond;   /* SMPC area code, TVSTAT bit 0, the byte at 0x24FFFFFF, OREG0 */
    uint8_t rtc[7];                 /* SGL SmpcDateTime as stored: year BCD (2 B, big-endian), month, day, h, m, s */
    uint8_t nport[2];               /* Per_Connect1/2 */
    uint8_t port[2][OM_SU_TAPS];    /* each port's peripheral IDs (Smpc_Peripheral[0..] and [15..]) */
    om_su_bram bram, cart_bram;
    uint32_t games;
    uint8_t loader;                 /* settings.h OM_LDR_* */
} om_su_raw;

/* The detectors (each returns a static token): */
const char *om_su_cart(uint8_t id);            /* "RAM1MB" "RAM4MB" "NONE" "OTHER" */
const char *om_su_dev(uint8_t id, char tmp[6]);/* a peripheral ID's token ("PAD", "3DPAD_ANALOG", ..., "ID_xx") */
char om_su_area(uint8_t code);                 /* 'J' 'T' 'U' 'B' 'K' 'A' 'E' 'L', 0 = reserved */
const char *om_su_rtc(uint8_t cond, const uint8_t rtc[7]);   /* "OK" "UNSET" "BAD" */
/* The whole file into out (cap >= OM_SU_CAP); returns its length. */
uint32_t om_su_text(const om_su_raw *r, char *out, uint32_t cap);
/* 1 when a and b hold the same setup: every line equal except DATE= (the file's write time is not part of it). */
int om_su_same(const char *a, uint32_t alen, const char *b, uint32_t blen);
#endif
