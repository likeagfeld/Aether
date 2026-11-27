/* menu/app/launch.h -- spec §7.1 launch steps 1-2: write the hand-off block, load /aether/LAUNCHER.BIN
 * (4 KB header + body) to 0x00200000 and jump. BSD-3-Clause.
 *
 * Task 13c: the loading screen shows the steps as they really complete, so the work is split into phases with the same
 * error codes as the one-call om_launch_prepare (which now runs them back to back):
 *   1. om_launch_begin   -- the path split into folder + image and the hand-off block filled ("Reading disc image";
 *                           the CUE itself is parsed by LAUNCHER.BIN after the hand-off, cue2desc.c);
 *   2. om_launch_open    -- LAUNCHER.BIN's size and the open (no data read);
 *      om_launch_read    -- `chunk` bytes a call (so the caller keeps drawing between calls): first the 4 KB header,
 *                           checked with the body's size against the buffer, then the body with its CRC-32
 *                           accumulated; the last call checks it ("Preparing launcher");
 *      om_launch_abort   -- closes the file when the caller stops early (cancel).
 * li->clobbered becomes 1 as soon as the body's first byte may have been written (LWRAM GAMES.DB / ART.PAK table). */
#ifndef OM_LAUNCH_H
#define OM_LAUNCH_H
#include <stdint.h>
#include "handoff.h"
typedef struct { uint8_t *hdr; uint8_t *body; uint32_t body_cap; om_handoff *handoff; } om_launch_bufs;
typedef struct { int clobbered; uint32_t body_size; } om_launch_info;
typedef struct { int fd; uint32_t size, hdr_got, done, crc; om_lnch_info inf; } om_launch_ld;
int om_launch_split(const char *path, char dir[260], char image[260]);
/* o carries the resolved loader and the per-game options (handoff.h om_launch_opts); NULL is refused. */
int om_launch_begin(const char *path, uint8_t area, uint32_t flags, const om_launch_opts *o, const om_launch_bufs *b,
                    om_launch_info *li);
int om_launch_open(om_launch_ld *ld, const om_launch_bufs *b, om_launch_info *li);
int om_launch_read(om_launch_ld *ld, const om_launch_bufs *b, om_launch_info *li, uint32_t chunk);   /* >0 more */
void om_launch_abort(om_launch_ld *ld);
int om_launch_prepare(const char *path, uint8_t area, uint32_t flags, const om_launch_opts *o, const om_launch_bufs *b,
                      om_launch_info *li);
void om_launch_jump(void);
#endif
