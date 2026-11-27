/* menu/app/audio.h -- the menu's sound (controller overrides task-14-15-overrides.md): direct SCSP slots
 * with the 68000 held by SNDOFF and no SGL/SBL sound linked (decision D2, sbuild check_forbidden_symbols).
 *  - SOUND.BIN (contract/SPEC.md §5.8) comes from the menu disc before the boot animation (om_audio_boot): the "Se-ga"
 *    chant into the music region (0x10000, free at boot), scroll / select / ring / scream into the SFX region (0x30000).
 *  - The background music streams from /aether/MUSIC/BGM.MUS (SPEC §5.12) into a 7-block ring per channel in the
 *    music region (L 0x10000, R 0x20000), two slots hard left / hard right, refilled in 2 KB s_read steps by
 *    om_audio_tick from the menu loop; fades, ducking and the underrun guard run in the V-blank hook (om_audio_vblank).
 *  - The loading screen (menu/views/loading.c) fires the events below and borrows the music region and
 *    0x40000-0x77FFF once the music has faded. BSD-3-Clause. */
#ifndef OM_AUDIO_H
#define OM_AUDIO_H
#include <stdint.h>

enum {
    OM_AEV_SELECT,            /* A on a game: the select sound */
    OM_AEV_MUSIC_FADE_OUT,    /* the launch starts: the menu music fades out (500 ms) */
    OM_AEV_MUSIC_RESUME,      /* the launch was canceled or failed: the music comes back (re-primed, faded in) */
    OM_AEV_SCREAM,            /* "Starting game...": the scream, on silence */
    OM_AEV_COUNT
};
/* SFX map (overrides M3-R17) */
enum {
    OM_SFX_SCROLL,            /* the cursor moved */
    OM_SFX_SCROLL_HELD,       /* a held direction's repeat: every other one sounds, -6 dB */
    OM_SFX_SELECT,            /* A */
    OM_SFX_BACK,              /* B / close: select -5 semitones */
    OM_SFX_FAV_ADD,           /* X: ring */
    OM_SFX_FAV_REMOVE,        /* X: ring -5 semitones */
    OM_SFX_SORT,              /* Y / Z: scroll +4 semitones */
    OM_SFX_ERROR,             /* a two-tone low select (-7, then -12 semitones) */
    OM_SFX_COUNT
};
#define OM_PATH_MUSIC     "/aether/MUSIC/BGM.MUS"
#define OM_PATH_MUSICPOS  "/aether/MUSICPOS.TXT"
#define OM_SOUND_BIN      "SOUND.BIN"            /* on the menu disc (tools/sbuild.py) */

/* one more file from the menu disc's root, read in the same pass, before SOUND.BIN (no second root read): name,
 * the destination and its capacity in, the size read (len) and rc out (0, or the find/read error; whole sectors are
 * written, so (len rounded up to 2,048) <= cap). A failure here never stops SOUND.BIN, nor the reverse. */
typedef struct { const char *name; uint8_t *dst; uint32_t cap, len; int rc; } om_disc_file;
/* Boot, before the animation (main.c): SNDOFF, SCSP init (MEM4MB, DSP neutralized, every slot off), SOUND.BIN from the
 * disc through `work` (>= 96 KB, free until the animation borrows it), the clips into sound RAM; and `also`, if not
 * NULL (BIGLOGO.BIN). Every wait is bounded in V-blanks (3 s in all); a failure is named (om_audio_boot_msg) and
 * the menu boots silent. */
int om_audio_boot(uint8_t *work, uint32_t cap, om_disc_file *also);
void om_audio_chant(void);                       /* the boot animation's OM_BOOTEV_CHANT (bootplay.c's hook) */
/* The menu (folders.c): the first menu fade-in starts the music (after the chant), settings apply at once. */
void om_audio_menu_start(int music, uint32_t music_vol, int sfx);
void om_audio_settings(int music, uint32_t music_vol, int sfx);
void om_audio_tick(void);                        /* once per menu frame: the music's SD refill (2 KB reads) */
void om_audio_tick_low(void);                    /* om_audio_tick only if the ring is under 3 blocks */
void om_audio_vblank(void);                      /* slIntFunction: om_v2_vblank + fades, ducking, the ring monitor */
void om_sfx(unsigned id);
void om_audio_save_state(void);                  /* MUSICPOS.TXT, at safe moments only: launch commit, settings save */
void om_audio_handoff(void);                     /* before the hand-off and before an error screen's exit (fatal.h):
                                                    all 32 slots off, the common word rewritten; idempotent */

void om_audio_event(unsigned ev);
uint32_t om_audio_events(unsigned ev);       /* times ev was fired (diagnostics, the fake build's captures) */
uint32_t om_audio_scream_len(void);          /* the scream's length in V-blanks; 0 when it would not play */
uint32_t om_audio_scream_remaining(void);    /* V-blanks until the scream has finished; 0 when none is playing */
uint32_t om_audio_underruns(void);           /* the music ran dry and faded out (diagnostics) */
#ifdef OMSAT_FAKECARD
uint32_t om_audio_stream_bytes(void);        /* bytes streamed so far (fake diagnostics) */
uint32_t om_audio_min_ahead5(void);          /* the ring's lowest lead (blocks) over the last 5 s */
#endif
uint32_t om_audio_boot_vblanks(void);        /* V-blanks om_audio_boot took (SNDOFF + SOUND.BIN; diagnostics screen) */
/* review-sound I2: before a synchronous SETTINGS / FAVS / RECENT write the caller closes the music file (and cancels an
 * art job), so at most 2 SD files are open; om_audio_tick reopens it and seeks back to where it was (once, bounded by
 * the libsatiator command timeouts; the ring's >= 1.1 s lead covers it, else the underrun guard fades). */
void om_audio_sd_pause(void);
const char *om_audio_boot_msg(void);         /* "" or the named SOUND.BIN / SCSP failure with its rc */

/* Task 13c round 2: the loading screen borrows the sound RAM that is idle while it runs -- the music region
 * 0x10000-0x2FFFF (the music has faded out) and the unclaimed 0x40000-0x77FFF (ledger D3). Never the SFX region
 * 0x30000-0x3FFFF (the scream plays from it), 0x00000-0x0FFFF or PCM_Work 0x78000-0x7FFFF. Before the first write:
 * the 68000 is held (SNDOFF), every SCSP slot whose sample range [SA, SA + LEA) meets a borrowed region is keyed off
 * (HWF-34), MEM4MB is set so the SCSP sees the whole 512 KB (HWF-35; the word is written whole: write-only), and the DSP ring buffer is checked: if it overlaps
 * a borrowed region while a microprogram is loaded (HWF-39), nothing is borrowed (OM_AUDIO_E_DSP). After a borrow the
 * music ring is gone: OM_AEV_MUSIC_RESUME re-primes it, and om_audio_music_lost() says so until
 * om_audio_music_reloaded(). */
#define OM_AUDIO_E_DSP -88
#define OM_AUDIO_E_68K -97            /* SNDOFF was never accepted at boot: the 68000 may be running */
/* review-c2-12-14.md Minor: om_audio_boot's `also` sentinel. An early exit (SNDOFF/CD-busy/PVD/ROOT DIR failure)
 * never reaches the `also` find-and-read at all, so its rc must say "never attempted", not borrow a file-format
 * error that would misreport a disc fault as "file too short" on a pass that was never run. */
#define OM_AUDIO_E_NOTTRIED -98
int om_audio_sndram_borrow(void);
/* sound RAM 0x40000-0x77FFF (ledger D3: free) may be written now, with the music playing --
 * the 68000 is held (SNDOFF, HWF-38) and the SCSP is set up (MEM4MB = 1, HWF-35; the DSP ring parked at 0x78000) */
int om_audio_sndram_free_ok(void);
int om_audio_music_faded(void);              /* the music is silent (faded out, or not playing at all) */
int om_audio_music_lost(void);
void om_audio_music_reloaded(void);
#endif
