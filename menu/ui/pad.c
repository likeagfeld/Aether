/* menu/ui/pad.c -- SGL pad data is active-low (SGL302 SAMPLE/S_9_1/MAIN.C:54-62); PER_DGT_* per
 * SL_DEF.H:1250-1262 (HWF-14). Returned masks use pressed = 1. BSD-3-Clause. */
#include "pad.h"
#include "hang.h"
#ifdef OMSAT_FAKECARD
#include "fakepad.h"
#endif

static Uint16 s_prev;
static Uint16 s_pk, s_latch;   /* the keys at the last peek; presses seen there, for the poll */
static Uint16 s_rep, s_repd;   /* s_repd: the keys the last poll reported only because they auto-repeated */
static Uint16 s_repm = PER_DGT_KU | PER_DGT_KD | PER_DGT_TL | PER_DGT_TR;

void om_pad_set_repeat(Uint16 mask) { s_repm = mask; s_rep = 0; }

Uint16 om_pad_held(void)
{
#ifdef OMSAT_FAKECARD
    return om_fakepad_held();       /* plan D6: scripted pad in the fake-card build only */
#endif
    if (!Per_Connect1)
        return 0;
    return (Uint16)~Smpc_Peripheral[0].data;
}

void om_pad_prime(void) { s_prev = s_pk = om_pad_held(); s_rep = 0; s_latch = 0; s_repd = 0; }

Uint16 om_pad_repeated(void) { return s_repd; }

Uint16 om_pad_peek(void)
{   /* a long frame (the static panel's reads) looks between its steps; a press is
     * latched for the next om_pad_poll, so a tap shorter than the frame is not lost. SGL refreshes Smpc_Peripheral
     * outside slSynch (measured on Mednafen 1.32.1: a sentinel written there read back 0xFFFF within 1 V-blank of a
     * busy loop with no slSynch, task-18b-report.md) */
#ifdef OMSAT_FAKECARD
    Uint16 cur = om_fakepad_peek();
#else
    Uint16 cur = Per_Connect1 ? (Uint16)~Smpc_Peripheral[0].data : 0;
#endif
    Uint16 n = (Uint16)(cur & ~s_pk);
    s_pk = cur;
    s_latch |= n;
    return n;
}

Uint16 om_pad_poll(void)
{
#ifdef OMSAT_FAKECARD
    om_fakepad_tick();
#endif
    const Uint16 rep = s_repm;
    OM_HANG_BEAT();                 /* every interactive loop polls once a frame: the hang watchdog's beat */
    Uint16 cur = om_pad_held(), newly = (Uint16)((cur & ~s_prev) | s_latch);   /* + the peeks' presses */
    s_repd = 0;
    if (cur & s_prev & rep) {
        if (++s_rep >= OM_PAD_REPEAT_DELAY && (s_rep - OM_PAD_REPEAT_DELAY) % OM_PAD_REPEAT_RATE == 0) {
            s_repd = (Uint16)(cur & rep & ~newly);
            newly |= (Uint16)(cur & rep);
        }
    } else
        s_rep = 0;
    s_prev = s_pk = cur;
    s_latch = 0;
    return newly;
}

int om_pad_soft_reset(Uint16 held)
{
    const Uint16 m = PER_DGT_TA | PER_DGT_TB | PER_DGT_TC | PER_DGT_ST;   /* A+B+C+Start (spec §7.5, HWF-21) */
    return (held & m) == m;
}

Uint8 om_pad_port1_id(void)
{
#ifdef OMSAT_FAKECARD
    return om_fakepad_port1_id();       /* a scripted 3D-pad id in the fake-card build only */
#endif
    return Per_Connect1 ? Smpc_Peripheral[0].id : (Uint8)0xFF;   /* SGL's PER_ID_NotConnect */
}
