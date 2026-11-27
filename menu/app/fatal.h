/* menu/app/fatal.h -- visible, named failures (Global Constraints) and the two exits of every error screen: return to
 * the official menu (A+B+C+Start) and reset the console (C alone, SMPC SYSRES; Ruling M3-R28a / M3-R15d). */
#ifndef OM_FATAL_H
#define OM_FATAL_H
void om_fatal(const char *what, int code);
/* run once at the start of om_fatal, before its lines are written -- main.c stops the start-up
 * interstitial and prints the boot log there, whichever module the fatal error comes from. */
void om_fatal_set_hook(void (*fn)(void));
/* review-final-m3 I1: run before either exit leaves the menu -- main.c installs om_audio_handoff (every SCSP slot keyed
 * off, the common word written), so neither the official menu nor a reset console inherits a sounding slot. fatal.c
 * keeps no audio include; the hook may run more than once (the hand-off is idempotent). */
void om_exit_set_silence(void (*fn)(void));
void om_return_official(void);   /* never returns on success; a failure is named on row 27 and the screen stays */
int om_reset_console(void);      /* SMPC SYSRES; returns only on failure (the rc, also named on row 27) */
#endif
