/* menu/launcher/lgap.c -- see lgap.h. FRC and TCR as menu/ui/uitext.c reads them (SH7604_Hardware.txt:12779-12790:
 * FRC at H'FFFFFE12 high byte first, TCR at H'FFFFFE16 bits 1-0 = clock /8, /32, /128; 26.8741 MHz in 320-dot mode,
 * SMPC_Manual.txt:416-418); TCR is written only by om_lclock_slow. TVSTAT bit 3 VBLANK (HWF-10, vbwait.c).
 * BSD-3-Clause. */
#include <stdint.h>
#include "frcgap.h"
#include "om_fmt.h"
#include "lgap.h"

#define TVSTAT (*(volatile uint16_t *)0x25F80004u)

static uint16_t frc(void)
{
    uint8_t hi = *(volatile uint8_t *)0xFFFFFE12;
    uint8_t lo = *(volatile uint8_t *)0xFFFFFE13;
    return (uint16_t)((hi << 8) | lo);
}

void om_lgap(void)
{
    static const uint32_t div[4] = { 8, 32, 128, 128 };
    uint32_t us10k = 10000u * div[*(volatile uint8_t *)0xFFFFFE16 & 3u] * 1000u / 26874u;   /* 2,976 at clock/8 */
    int low = !(TVSTAT & 0x0008);
    om_frcgap g;
    om_frcgap_start(&g, frc(), us10k);
    for (;;) {
        if (!(TVSTAT & 0x0008)) low = 1;
        else if (low) return;                       /* a V-blank began */
        if (om_frcgap_step(&g, frc())) return;      /* 40 ms or the poll cap (frcgap.h) */
    }
}

static uint16_t s_clk_last;
static uint32_t s_clk_us, s_clk_rem;
void om_lclock_slow(int on)
{
    static uint8_t old;
    volatile uint8_t *tcr = (volatile uint8_t *)0xFFFFFE16;
    if (on) { old = *tcr; *tcr = (uint8_t)((old & ~3u) | 2u); }
    else *tcr = old;
    s_clk_last = frc();
}

uint32_t om_lclock_us(void)
{
    static const uint32_t div[4] = { 8, 32, 128, 128 };
    uint16_t t = frc();
    uint32_t ns = div[*(volatile uint8_t *)0xFFFFFE16 & 3u] * 1000000u / 26874u;   /* a tick: 4,763 ns at clock/128 */
    s_clk_rem += (uint32_t)(uint16_t)(t - s_clk_last) * ns;
    s_clk_last = t;
    s_clk_us += s_clk_rem / 1000u;
    s_clk_rem %= 1000u;
    return s_clk_us;
}

/* a session nonce for the marker and ACTIVE.TXT. Not a secret, only "not an earlier
 * session's": the free-running FRC, the VDP2 H and V counters and a counter, mixed by CRC-32. Reading EXTEN (180002H)
 * latches the HV counter when EXLTEN = 0 (VDP2_Manual.txt:1395-1403); HCNT at 180008H, VCNT at 18000AH
 * (VDP2_Manual.txt:1538-1542, :1573); the VDP2 register base 0x25F80000 is HWF-09. Only read. */
uint32_t om_lnonce(void)
{
    static uint32_t ctr;
    uint16_t v[4];
    (void)*(volatile uint16_t *)0x25F80002u;
    v[0] = frc();
    v[1] = *(volatile uint16_t *)0x25F80008u;
    v[2] = *(volatile uint16_t *)0x25F8000Au;
    v[3] = (uint16_t)++ctr;
    return om_crc32(0, v, sizeof v);
}
