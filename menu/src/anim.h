/* menu/src/anim.h -- motion design maths: ease-out tweens that retarget without a jump,
 * the anim= setting's full / reduced / off scaling, the status toast's state machine (it slides up over the footer's
 * button hints and replaces them; an rc error stays until the next action), and RGB555 colour mixing for the pre-blended
 * VDP1 lookup tables. Portable C: no SGL, host-tested in menu/host-tests/test_anim.c. Time is in frames (slSynch,
 * 59.94 or 50 Hz; HWF-11). Input is never blocked: callers change state at once and only the visuals interpolate.
 * BSD-3-Clause. */
#ifndef OM_ANIM_H
#define OM_ANIM_H
#include <stdint.h>

enum { OM_ANIM_FULL, OM_ANIM_REDUCED, OM_ANIM_OFF, OM_ANIM_COUNT };

#define OM_Q 4096                /* Q12: 1.0 */

/* Cubic ease-out, p and result in Q12, p clamped to [0, OM_Q]. */
int32_t om_ease_out(int32_t p);

typedef struct { int32_t from, to; uint16_t t, dur; } om_tween;
void om_tween_jump(om_tween *tw, int32_t v);                 /* no motion: value = v */
/* Glide from the current value to `to` over dur frames (dur 0 = jump). Retargeting mid-glide starts from where it is. */
void om_tween_to(om_tween *tw, int32_t to, uint16_t dur);
/* Glide from `from` to `to` over dur frames, whatever it was doing (a slide that starts offset, e.g. a new list). */
void om_tween_run(om_tween *tw, int32_t from, int32_t to, uint16_t dur);
void om_tween_step(om_tween *tw);                            /* one frame */
int32_t om_tween_value(const om_tween *tw);
int om_tween_active(const om_tween *tw);

/* Frames for a duration in ms under anim mode, at fps_x100 (5994 or 5000). OFF -> 0. `held` (a direction held at
 * auto-repeat) halves it, at least 2 frames, so fast scrolling stays responsive. */
uint16_t om_anim_frames(uint16_t ms, uint8_t mode, int held, uint32_t fps_x100);
/* A slide distance under anim mode: FULL as is, REDUCED halved, OFF 0. */
int32_t om_anim_dist(int32_t px, uint8_t mode);

/* RGB555 (bit 15 kept from a) mixes: t in 0..256 from a to b. */
uint16_t om_c555_mix(uint16_t a, uint16_t b, uint32_t t256);
/* VDP2 colour calculation (HWF-31, D1): top over second at CCRT ratio r (0..31): top*(31-r)/32 + second*(r+1)/32. */
uint16_t om_c555_cc(uint16_t top, uint16_t second, uint8_t r);
/* 0x00RRGGBB -> RGB555 with bit 15 set (a VDP1 RGB code, HWF-01). */
uint16_t om_c555_rgb(uint32_t rgb);

/* The status toast (footer strip): show() a new message (empty = hide), input() on any user action, tick() once a frame.
 * `progress` is the eased slide in Q12: 0 = the hints are shown, OM_Q = the message has replaced them. A message
 * containing " rc " (om_msg_rc's suffix) is an error: it never times out, and the next action dismisses it. Others
 * auto-hide after OM_TOAST_HOLD frames. */
#define OM_TOAST_HOLD 180u       /* ~3 s at 59.94 Hz */
#define OM_TOAST_MSG  64u
typedef struct {
    char msg[OM_TOAST_MSG];
    uint8_t sticky, visible, mode;
    uint16_t age, dur;
    uint32_t serial;           /* the last status serial shown (om_toast_offer) */
    om_tween slide;
} om_toast;
void om_toast_init(om_toast *t, uint8_t mode, uint16_t dur_frames);
void om_toast_show(om_toast *t, const char *msg);
void om_toast_input(om_toast *t);
void om_toast_tick(om_toast *t);
int32_t om_toast_progress(const om_toast *t);
/* Review-13a C1/M2: offer the view's current status, posted with a serial that changes on EVERY post (so the same rc
 * error twice is shown twice). Shown when the serial is new -- except that an automatic post (not caused by input)
 * never replaces a visible rc error: it waits until input dismisses the error. Returns 1 when shown. */
int om_toast_offer(om_toast *t, const char *msg, uint32_t serial, int automatic);
int om_msg_is_error(const char *msg);
#endif
