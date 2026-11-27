/* menu/app/hang.h -- the hang watchdog (hardware 2026-11-25: the menu froze in the Cheats popup and in Settings, the
 * music stopping with it, never on Mednafen). om_pad_poll bumps om_hang_beat every frame of every interactive loop;
 * om_hang_vbl, the menu's V-blank hook in place of om_audio_vblank (which it calls first), sees the beat stand still
 * for OM_STALL_VB V-blanks and writes what the stopped code was doing on NBG0 row 2: "HANG pc pr sp tag" (the
 * interrupted PC, PR and stack pointer, and the phase, OM_HT_*), hex; the row clears when the beat moves again.
 * Nothing is shown while interrupts are masked: a frozen picture with no HANG line says the CPU stopped with interrupts
 * off. BSD-3-Clause. */
#ifndef OM_HANG_H
#define OM_HANG_H
#include <stdint.h>

/* the fake-card build has no room under the 64 KB floor for it (its own test HUD): there the hook is the music's
 * alone and the beat and tags compile away; -DOM_HANG_FAKE builds it in anyway (a scratch build with the floor
 * lowered, to check hang.c's stack offsets on Mednafen) */
#if !defined(OMSAT_FAKECARD) || defined(OM_HANG_FAKE)
#define OM_HANG_ON 1
extern volatile uint32_t om_hang_beat;
extern volatile uint8_t om_hang_tag;
#define OM_HANG(t) (om_hang_tag = (uint8_t)(t))
extern volatile uint8_t om_hang_clr;
void om_hang_reclear(void);
/* hardware 2026-11-27: a HANG row stayed after the menu went on; the V-blank's clear is written again from here, the
 * main loop's own om_pad_poll, once the hook has seen the beat move (om_hang_clr) */
#define OM_HANG_BEAT() (om_hang_beat++, om_hang_clr ? om_hang_reclear() : (void)0)
#define OM_HANG_VBL om_hang_vbl
void om_hang_vbl(void);              /* slIntFunction target (asm: passes the interrupt's stack to the check) */
/* the CPU exceptions (general and slot illegal instruction, CPU and DMA address error: vectors 4, 6, 9, 10 of the
 * BIOS table at VBR) stop with their registers kept: the watchdog then adds "EXC vector pc sr pr" (hex) on NBG0 row 3,
 * where the BIOS's own handler loops with no trace (the console's reset button restarts). The first om_exc_swap
 * installs them, the second puts the BIOS's back (before the jump to LAUNCHER.BIN: a game finds the table as the BIOS
 * left it). */
void om_exc_swap(void);
#define OM_EXC_INSTALL() om_exc_swap()
#define OM_EXC_RESTORE() om_exc_swap()
#else
#define OM_HANG(t) ((void)0)
#define OM_HANG_BEAT() ((void)0)
#define OM_HANG_VBL om_audio_vblank
#define OM_EXC_INSTALL() ((void)0)
#define OM_EXC_RESTORE() ((void)0)
#endif
/* the phase: 1 the loop's input (and About, Settings, a launch), 2 a popup's key, 3 the music refill, 4 the frame's
 * drawing and slSynch */
enum { OM_HT_LOOP = 1, OM_HT_POPKEY, OM_HT_AUDIO, OM_HT_SYNCH };

#endif
