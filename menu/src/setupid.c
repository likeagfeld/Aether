/* menu/src/setupid.c -- see setupid.h. BSD-3-Clause.
 *
 * /aether/DIAG.TXT, FORMAT=1 (the Card Manager parses it: keys and tokens only ever get added). ASCII, '\n' lines,
 * '#' starts a comment, every other line KEY=VALUE. A value of UNKNOWN means this console cannot tell.
 *   FORMAT=1
 *   AETHER=<version> <build>          version.h
 *   DATE=YYYY-MM-DD HH:MM:SS|UNKNOWN  the console clock when the file was written. NOT compared: the file is rewritten
 *                                     only when another line differs, so this is when the setup last changed
 *   SATIATOR_FW=<n>|UNKNOWN           fwver.h om_fw_parse of SATIATOR_FW_RAW, the firmware's own string
 *   BIOS=<16 chars>|UNKNOWN           the BIOS ROM's header at 0x800, e.g. BTR_1.0191941228 (version 1.01, 94-12-28;
 *                                     measured on 11 BIOS dumps this session, the same shape in every one)
 *   AREA=J|T|U|B|K|A|E|L|UNKNOWN      the SMPC area code (SMPC_Manual.txt OREG9) as its disc-area letter;
 *   AREA_CODE=<0-15>                  and the code itself (the ground truth, as on the diagnostics page)
 *   VIDEO=NTSC60|PAL50                VDP2 TVSTAT bit 0 (HWF-10)
 *   CART=RAM1MB|RAM4MB|NONE|OTHER     the cartridge ID byte at 0x24FFFFFF, read only (ST-TECH-47 §5, §9 step 1: the ID
 *   CART_ID=<2 hex>                   is read before the RAM's init write, which is never done here); FF = NONE
 *   CART_BRAM=<free>/<total> <n>KB|NONE|UNFORMATTED|UNKNOWN   a backup-memory cartridge (BIOS backup library, device 1)
 *   CART_ROM=NONE|PSKAI <product> <version>|ROM <product> <version> <title>   a boot cartridge's header in A-bus CS0
 *                                     ("SEGA SEGASATURN " at 0x22000000; Pseudo Saturn Kai's product starts PSKAI_,
 *                                     Kai main.c:4078/4123). A stock Action Replay's fields are not known yet: ROM ...
 *   NETLINK=UNKNOWN                   the only known probe writes the modem's UART scratch register: not done
 *   PORT1=, PORT2=<dev>|NONE|MULTITAP:<dev>,<dev>,...   SGL's peripheral IDs. SGL skips empty ports and hides the
 *                                     multitap (SGL302 PER.TXT:22-23), so 2+ devices on a port mean a multitap and a
 *                                     multitap holding 0 or 1 devices reads as NONE or that device. <dev>:
 *                                     PAD (0x02: a Control Pad OR a 3D Control Pad in digital mode, HWF-77),
 *                                     3DPAD_ANALOG (0x16, HWF-77); SL_DEF.H's names, not measured: ANALOG (0x15),
 *                                     RACER (0x13), MOUSE (0x23), GUN (0x25), KEYBOARD (0x34), MD3PAD (0xE1),
 *                                     MD6PAD (0xE2), MDMOUSE (0xE3); anything else ID_<2 hex>
 *   RTC=OK|UNSET|BAD                  OREG0 STE = 0: never set since an SMPC cold reset (a dead or removed battery,
 *                                     SMPC_Manual.txt:1539-1555); BAD: set, but a field is not a valid BCD date
 *   BRAM=<free>/<total> <n>KB|UNFORMATTED|UNKNOWN   internal backup RAM in blocks (BUP_Stat device 0)
 *   GAMES=<n>                         games in the library
 *   LOADER=AUTO|CWX|JHL               the default loader (SETTINGS.INI)
 */
#include <string.h>
#include "setupid.h"
#include "fwver.h"
#include "settings.h"

typedef struct { char *p, *e; } wr;

static void put(wr *w, const char *s) { while (*s && w->p < w->e) *w->p++ = *s++; }
static void dec(wr *w, uint32_t v)
{
    char t[11];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n && w->p < w->e) *w->p++ = t[--n];
}
static char hexd(uint32_t v) { return "0123456789ABCDEF"[v & 15u]; }
static void hex2(wr *w, uint8_t b) { char t[3] = { hexd(b >> 4), hexd(b), 0 }; put(w, t); }
static void raw(wr *w, const uint8_t *s, uint32_t n)
{   /* printable ASCII as is (anything else '?'), trailing spaces and NULs dropped */
    while (n && (s[n - 1] == ' ' || !s[n - 1])) n--;
    for (; n && w->p < w->e; n--, s++) *w->p++ = (char)(*s >= 0x20 && *s < 0x7F ? *s : '?');
}

const char *om_su_cart(uint8_t id)
{
    return id == 0x5A ? "RAM1MB" : id == 0x5C ? "RAM4MB" : id == 0xFF ? "NONE" : "OTHER";
}

static const uint8_t k_dev_id[] = { 0x02, 0x16, 0x15, 0x13, 0x23, 0x25, 0x34, 0xE1, 0xE2, 0xE3, 0xFF };
static const char k_dev_nm[] = "PAD\0" "3DPAD_ANALOG\0" "ANALOG\0" "RACER\0" "MOUSE\0" "GUN\0" "KEYBOARD\0" "MD3PAD\0"
                               "MD6PAD\0" "MDMOUSE\0" "NONE\0";

const char *om_su_dev(uint8_t id, char tmp[6])
{
    const char *s = k_dev_nm;
    uint32_t i;
    for (i = 0; i < sizeof k_dev_id; i++, s += strlen(s) + 1)
        if (k_dev_id[i] == id) return s;
    tmp[0] = 'I'; tmp[1] = 'D'; tmp[2] = '_'; tmp[3] = hexd(id >> 4); tmp[4] = hexd(id); tmp[5] = 0;
    return tmp;
}

char om_su_area(uint8_t code)
{   /* SMPC_Manual.txt OREG9: 1 Japan, 2 Asia NTSC, 4 N America, 5 S America NTSC, 6 Korea, A Asia PAL, C Europe PAL,
     * D S America PAL; the others are reserved */
    char c = "-JT-UBK---A-EL--"[code & 15u];
    return c == '-' ? 0 : c;
}

static int bcd_ok(uint8_t b, uint32_t lo, uint32_t hi)
{
    uint32_t v = (b >> 4) * 10u + (b & 15u);
    return (b >> 4) <= 9u && (b & 15u) <= 9u && v >= lo && v <= hi;
}

const char *om_su_rtc(uint8_t cond, const uint8_t r[7])
{   /* r: year BCD (2 B), month (low nibble, hex; high nibble the weekday), day, hour, minute, second BCD (HWF-27) */
    uint32_t m = r[2] & 15u;
    if (!(cond & 0x80u)) return "UNSET";
    return bcd_ok(r[0], 0, 99) && bcd_ok(r[1], 0, 99) && m >= 1u && m <= 12u && bcd_ok(r[3], 1, 31) &&
           bcd_ok(r[4], 0, 23) && bcd_ok(r[5], 0, 59) && bcd_ok(r[6], 0, 59) ? "OK" : "BAD";
}

static void bram(wr *w, const om_su_bram *b)
{
    if (b->rc == 0) { dec(w, b->free); put(w, "/"); dec(w, b->total); put(w, " "); dec(w, b->kb); put(w, "KB"); }
    else put(w, b->rc == 1 ? "NONE" : b->rc == 2 ? "UNFORMATTED" : "UNKNOWN");
}

static void port(wr *w, uint32_t i, const om_su_raw *r)
{
    char tmp[6];
    uint32_t n = r->nport[i], k;
    if (n > 1u) put(w, "MULTITAP:");
    if (n > OM_SU_TAPS) n = OM_SU_TAPS;
    if (!n) put(w, "NONE");
    for (k = 0; k < n; k++) {
        if (k) put(w, ",");
        put(w, om_su_dev(r->port[i][k], tmp));
    }
}

/* the file, a byte below 0x0A standing for a field (one template keeps the keys out of the code) */
static const char k_tpl[] = "# Aether console setup, detected at start-up. KEY=VALUE; UNKNOWN = not detectable\nFORMAT=1\n"
    "AETHER=\x01\nDATE=\x02\nSATIATOR_FW=\x03\nSATIATOR_FW_RAW=\x04\nBIOS=\x05\nAREA=\x06\nAREA_CODE=\x07\nVIDEO=\x08\n"
    "CART=\x09\nCART_ID=\x0B\nCART_BRAM=\x0C\nCART_ROM=\x0E\nNETLINK=UNKNOWN\nPORT1=\x0F\nPORT2=\x10\nRTC=\x11\n"
    "BRAM=\x12\nGAMES=\x13\nLOADER=\x14\n";

uint32_t om_su_text(const om_su_raw *r, char *out, uint32_t cap)
{
    wr w = { out, out + cap - 1 };
    const char *rtc = om_su_rtc(r->smpc_cond, r->rtc), *t;
    const uint8_t *h = r->cs0;
    char c;
    int fw = r->fw ? om_fw_parse(r->fw) : -1;
    for (t = k_tpl; (c = *t) != 0; t++) {
        char one[2] = { c, 0 };
        switch (c) {
        case 1: put(&w, r->aether); break;
        case 2:
            if (rtc[0] != 'O') { put(&w, "UNKNOWN"); break; }
            /* YYYY-MM-DD HH:MM:SS; the BCD bytes print as hex, the month (low nibble) is binary */
            hex2(&w, r->rtc[0]); hex2(&w, r->rtc[1]); put(&w, "-");
            hex2(&w, (uint8_t)((r->rtc[2] & 15u) + ((r->rtc[2] & 15u) > 9u ? 6u : 0u))); put(&w, "-");
            hex2(&w, r->rtc[3]); put(&w, " "); hex2(&w, r->rtc[4]); put(&w, ":"); hex2(&w, r->rtc[5]); put(&w, ":");
            hex2(&w, r->rtc[6]);
            break;
        case 3: if (fw < 0) put(&w, "UNKNOWN"); else dec(&w, (uint32_t)fw); break;
        case 4: if (r->fw) raw(&w, (const uint8_t *)r->fw, (uint32_t)strlen(r->fw)); else put(&w, "UNKNOWN"); break;
        case 5: if (r->bios) raw(&w, r->bios, 16); else put(&w, "UNKNOWN"); break;
        case 6: one[0] = om_su_area(r->area); put(&w, one[0] ? one : "UNKNOWN"); break;
        case 7: dec(&w, r->area & 15u); break;
        case 8: put(&w, r->tvstat_pal & 1u ? "PAL50" : "NTSC60"); break;
        case 9: put(&w, om_su_cart(r->cart_id)); break;
        case 0x0B: hex2(&w, r->cart_id); break;
        case 0x0C: bram(&w, &r->cart_bram); break;
        case 0x0E:
            if (!h) put(&w, "UNKNOWN");
            else if (memcmp(h, "SEGA SEGASATURN ", 16)) put(&w, "NONE");
            else {   /* ST-040-R4 §4.2 System ID: product number 0x20 (10 B), version 0x2A (6 B), title 0x60 */
                int kai = !memcmp(h + 0x20, "PSKAI_", 6);
                put(&w, kai ? "PSKAI " : "ROM ");
                raw(&w, h + 0x20, 10); put(&w, " "); raw(&w, h + 0x2A, 6);
                if (!kai) { put(&w, " "); raw(&w, h + 0x60, 32); }
            }
            break;
        case 0x0F: case 0x10: port(&w, (uint32_t)(c - 0x0F), r); break;
        case 0x11: put(&w, rtc); break;
        case 0x12: bram(&w, &r->bram); break;
        case 0x13: dec(&w, r->games); break;
        case 0x14: put(&w, r->loader == OM_LDR_CWX ? "CWX" : r->loader == OM_LDR_JHL ? "JHL" : "AUTO"); break;
        default: put(&w, one);
        }
    }
    *w.p = 0;
    return (uint32_t)(w.p - out);
}

static const char *line(const char *p, const char *e, uint32_t *n)
{   /* the next line from p that is not DATE=..., its length in *n (without '\n'); NULL at the end */
    for (; p < e; ) {
        const char *q = memchr(p, '\n', (size_t)(e - p));
        uint32_t len = (uint32_t)((q ? q : e) - p);
        if (len < 5u || memcmp(p, "DATE=", 5)) { *n = len; return p; }
        p += len + (q ? 1u : 0u);
    }
    return 0;
}

int om_su_same(const char *a, uint32_t alen, const char *b, uint32_t blen)
{
    const char *ae = a + alen, *be = b + blen;
    for (;;) {
        uint32_t na, nb;
        a = line(a, ae, &na);
        b = line(b, be, &nb);
        if (!a || !b) return !a && !b;
        if (na != nb || memcmp(a, b, na)) return 0;
        a = a + na < ae ? a + na + 1u : ae;
        b = b + nb < be ? b + nb + 1u : be;
    }
}
