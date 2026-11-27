/* menu/ui/v1.h -- raw VDP1 commands through SGL's slSetSprite (ST-238-R1 slSetSprite: "set altered sprites that cannot be
 * created with the library functions or ... a window that affects specific sprites only"). Coordinates are screen
 * pixels (0,0 = top-left of the 320x224 screen); v1.c converts them to the local coordinates SGL sets (HWF-42).
 * Each call appends one command for this frame; om_v1_begin starts the frame. Z orders the commands: SGL sorts by Z
 * and draws the farthest first, so bigger z = further back (OM_Z_* below). BSD-3-Clause. */
#ifndef OM_V1_H
#define OM_V1_H
#include <stdint.h>
#include "safearea.h"

/* Painter bands, back to front. A user-clip command is issued at the front of its band's range (largest z) so it
 * precedes the parts that use it. */
#define OM_Z_SHADOW    190
#define OM_Z_FRAME     188
#define OM_Z_COVER     186
#define OM_Z_SHIMMER   184
#define OM_Z_HEADER    180
#define OM_Z_LISTCLIP  176
#define OM_Z_CURSOR    174
#define OM_Z_LISTTXT   170
#define OM_Z_LISTICON  168
#define OM_Z_DETAIL    160
#define OM_Z_OVCLIP    150
#define OM_Z_OVCURSOR  148
#define OM_Z_OVTXT     146
#define OM_Z_FOOTCLIP  140
#define OM_Z_FOOT      138

#define OM_V1_CLIP     0x0400u      /* PMOD Clip = 1, Cmod = 0: draw inside the user clip only (VDP1_Manual.txt:3480-3492) */
#define OM_V1_CLIP_OUT 0x0600u      /* PMOD Clip = 1, Cmod = 1: draw OUTSIDE the user clip (VDP1_Manual.txt:3480-3500) */
#define OM_V1_HALF     0x0003u      /* PMOD CC = 3, half-transparency: VDP1-over-VDP1 only (HWF-32, decision D1) */

void om_v1_begin(void);
/* the safe-area transform applied to the commands that follow; NULL (or an identity level) = drawn as given.
 * Views set it around their own draw group and clear it after (menu/views/folders.c, loading.c). */
void om_v1_set_safe(const om_safe *t);
uint32_t om_v1_count(void);
uint32_t om_v1_dropped(void);                /* commands refused since boot because the list was full */
/* A normal sprite, top-left at (x, y), w a multiple of 8. lut != 0: 16-colour lookup-table mode, CMDCOLR = lut / 8
 * (HWF-04); lut == 0: RGB mode (HWF-01). Both with ECdis (the 4 bpp end code 0xF is full coverage) and SPdis clear,
 * so code 0 is transparent (HWF-02). */
void om_v1_sprite(int x, int y, uint32_t tex, int w, int h, uint32_t lut, uint16_t pmod, int z);
/* A normal sprite in 256-color bank mode (8 bpp, CMDPMOD color mode 4): dot d is written to the frame buffer as
 * bank | d (bank's low 8 bits zero, VDP1_Manual.txt:1172-1199), ECdis and SPdis set so every dot, 0 and 0xFF
 * included, is drawn: an opaque rectangle. Task 13b's boot animation. */
void om_v1_sprite_bank(int x, int y, uint32_t tex, int w, int h, uint16_t bank, int z);
/* A scaled sprite drawn into the box (x0,y0)-(x1,y1) inclusive; gour != 0 applies that Gouraud table (VDP1 RGB parts
 * only, VDP1_Manual.txt Gouraud Shading). */
void om_v1_scaled(int x0, int y0, int x1, int y1, uint32_t tex, int w, int h, uint32_t lut, uint32_t gour,
                  uint16_t pmod, int z);
/* An untextured quad (x0,y0)-(x1,y1) inclusive in RGB code `color` (bit 15 set); gour != 0 shades it with that table. */
void om_v1_quad(int x0, int y0, int x1, int y1, uint16_t color, uint32_t gour, uint16_t pmod, int z);
/* a distorted sprite (CMDCTRL 2, HWF-47): the texture's top-left, top-right, bottom-right and
 * bottom-left corners drawn at A, B, C, D (any quad; listing them bottom-up flips the image -- the reflection). */
void om_v1_distorted(const int16_t xy[8], uint32_t tex, int w, int h, uint32_t lut, uint32_t gour, uint16_t pmod,
                     int z);
/* The same in 256-color bank mode (8 bpp, color mode 4): dot d is written as bank | d, dot 0 transparent (SPdis
 * clear); bank carries VDP2 sprite-type bits (priority, color calculation; HWF-44, HWF-48). Task 13c's reflection. */
void om_v1_distorted_bank(const int16_t xy[8], uint32_t tex, int w, int h, uint16_t bank, uint16_t pmod, int z);
/* An untextured quad through four points A, B, C, D (CMDCTRL 4), and a one-pixel line A-B (CMDCTRL 6). */
void om_v1_poly4(const int16_t xy[8], uint16_t color, uint32_t gour, uint16_t pmod, int z);
void om_v1_line(int xa, int ya, int xb, int yb, uint16_t color, uint16_t pmod, int z);
/* The user-clip rectangle for the parts that follow with OM_V1_CLIP. */
void om_v1_clip(int x0, int y0, int x1, int y1, int z);
/* VDP1 VRAM helpers: 16-bit writes only (HWF-23). */
void om_v1_write(uint32_t off, const uint16_t *src, uint32_t words);
void om_v1_write_bytes(uint32_t off, const uint8_t *src, uint32_t bytes);   /* bytes even */
/* A Gouraud table: four RGB555 offsets for A, B, C, D (top-left, top-right, bottom-right, bottom-left); 0x10 per
 * channel = unchanged, 0x00 = -16, 0x1F = +15 (VDP1_Manual.txt Table 5.3, HWF-43). */
void om_v1_gouraud(uint32_t off, uint16_t a, uint16_t b, uint16_t c, uint16_t d);
uint16_t om_v1_gour_delta(int dr, int dg, int db);           /* per-channel -16..+15 -> table word */
#endif
