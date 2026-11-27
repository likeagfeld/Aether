/*  Copyright (c) 2020 James Wah

    This file is in the public domain.
*/

#include "satiator.h"
#include <string.h>

// command enums {{{
typedef enum {
    c_get_status = 0x90,
    c_write_buffer,
    c_read_buffer,

    c_mkfs = 0x94,
    c_info,
    c_settime,

    c_open = 0xA0,
    c_close,
    c_seek,
    c_read,
    c_write,
    c_truncate,
    c_stat,
    c_rename,
    c_unlink,
    c_mkdir,
    c_opendir,
    c_readdir,
    c_chdir,
    c_emulate,
} satiator_cmd_t;

typedef enum {
    i_fw_version = 0,
    i_bootloader_version,
    i_serial_number,
    i_sd_latency,
} satiator_info_cmd_t;
// }}}

// CD block regs/constants {{{

// on a host test build (SAT_HOST_TEST, menu/host-tests/test_sattimeout.c) these resolve through a
// fake, iteration-counting register bank instead of real hardware addresses -- see the SAT_IRQ_* comment
// below for why this is the only other target-specific surface in the file. Real builds (SAT_HOST_TEST
// undefined) are byte-for-byte the original addresses.
#ifdef SAT_HOST_TEST
volatile uint16_t *sat_test_hirq(void);
volatile uint16_t *sat_test_cr(int n);
volatile uint32_t *sat_test_datatrns(void);
#define CDB_REG_HIRQ        (*sat_test_hirq())
#define CDB_REG_HIRQMASK    (*sat_test_hirq())
#define CDB_REG_CR1         (*sat_test_cr(0))
#define CDB_REG_CR2         (*sat_test_cr(1))
#define CDB_REG_CR3         (*sat_test_cr(2))
#define CDB_REG_CR4         (*sat_test_cr(3))
#define CDB_REG_DATATRNS    (*sat_test_datatrns())
#else
#define CDB_REG_HIRQ        *((volatile uint16_t *)0x25890008)
#define CDB_REG_HIRQMASK    *((volatile uint16_t *)0x2589000C)
#define CDB_REG_CR1         *((volatile uint16_t *)0x25890018)
#define CDB_REG_CR2         *((volatile uint16_t *)0x2589001C)
#define CDB_REG_CR3         *((volatile uint16_t *)0x25890020)
#define CDB_REG_CR4         *((volatile uint16_t *)0x25890024)
#define CDB_REG_DATATRNS    *((volatile uint32_t *)0x25818000)
#endif

#define HIRQ_CMOK   0x0001
#define HIRQ_DRDY   0x0002
#define HIRQ_CSCT   0x0004
#define HIRQ_BFUL   0x0008
#define HIRQ_PEND   0x0010
#define HIRQ_DCHG   0x0020
#define HIRQ_ESEL   0x0040
#define HIRQ_EHST   0x0080
#define HIRQ_ECPY   0x0100
#define HIRQ_EFLS   0x0200
#define HIRQ_SCDQ   0x0400
#define HIRQ_MPED   0x0800
#define HIRQ_MPCM   0x1000
#define HIRQ_MPST   0x2000

// }}}

// menu controller ruling -- see UPSTREAM.md "Local changes": exec_cmd busy-waited on CDB_REG_HIRQ
// with no timeout, so a boot with no Satiator, or a hung SD card, froze the menu with no message. These
// three macros isolate the ONLY two things that make this file target-specific (the SH-2 "mask/restore
// interrupts" inline asm, and the hard-coded CD-block register addresses below) behind one build-time
// switch, SAT_HOST_TEST, so the real bounded-wait logic in exec_cmd can be exercised by a host test
// (menu/host-tests/test_sattimeout.c) against a fake, iteration-counting HIRQ register instead of a copy
// of the loop. When SAT_HOST_TEST is undefined (every real Saturn build: menu/app, every menu/spikes/*,
// the launcher -- tools/sbuild.py never defines it) these macros expand to exactly the original asm, so
// the shipped behaviour is unchanged.
#ifdef SAT_HOST_TEST
#define SAT_IRQ_SAVE(sr)      ((void)(sr))
#define SAT_IRQ_MASK(sr)      ((void)(sr))
#define SAT_IRQ_RESTORE(sr)   ((void)(sr))
#else
#define SAT_IRQ_SAVE(sr)      asm("stc sr, %0" : "=r"(sr))
#define SAT_IRQ_MASK(sr)      do { uint32_t _new_sr = (sr) | 0xf0; asm("ldc %0,sr": : "r" (_new_sr)); } while (0)
#define SAT_IRQ_RESTORE(sr)   asm("ldc %0,sr": : "r" (sr))
#endif

// I/O primitives   {{{

typedef uint16_t cmd_t[4];

// SAT_HIRQ_WAIT_CAP is declared in satiator.h: see its comment there for provenance (unverified,
// generous, pending S3's measured SD/HIRQ timings) and why it is visible outside this file.
volatile s_diag_t s_diag;
static int tmo(const uint16_t *cr) {   // a timed-out wait, counted for the hang watchdog (satiator.h s_diag)
    s_diag.tmo++;
    s_diag.tmo_cmd = cr[0];
    return SAT_ETIMEDOUT;
}

static int exec_cmd(cmd_t cr, uint16_t wait) {
    // mask all irqs
    uint32_t sr = 0, n, used;
    SAT_IRQ_SAVE(sr);
    SAT_IRQ_MASK(sr);

    CDB_REG_HIRQ = ~(HIRQ_CMOK | wait);
    CDB_REG_CR1 = cr[0];
    CDB_REG_CR2 = cr[1];
    CDB_REG_CR3 = cr[2];
    CDB_REG_CR4 = cr[3];

    // HWF-57: upstream keeps every interrupt masked for the whole wait, which on hardware held off the menu's V-blank
    // hook during SD reads (diag: LATE IN 2244 of 28476 V-blanks; NBG2 map writes then landed in the active display
    // and the cover shadows flickered while covers loaded). Only the command issue needs to be atomic: no interrupt
    // handler of ours touches the CD block (om_audio_vblank / om_v2_vblank: SCSP and VDP2 only).
    SAT_IRQ_RESTORE(sr);

    n = SAT_HIRQ_WAIT_CAP;
    while (!(CDB_REG_HIRQ & HIRQ_CMOK))
        if (!--n) return tmo(cr);
    used = SAT_HIRQ_WAIT_CAP - n;
    if (wait) {
        n = SAT_HIRQ_WAIT_CAP;
        while (!(CDB_REG_HIRQ & wait))
            if (!--n) return tmo(cr);
        used += SAT_HIRQ_WAIT_CAP - n;
    }
    s_diag.ok++;
    if (used > s_diag.max_polls) { s_diag.max_polls = used; s_diag.max_cmd = cr[0]; }
    return 0;
}

static uint16_t sat_result[4];
static inline int get_stat(void) {
    cmd_t cmd = {c_get_status<<8, 0, 0, 0};
    int rc = exec_cmd(cmd, 0);
    if (rc < 0)
        return rc;
    sat_result[0] = CDB_REG_CR1;
    sat_result[1] = CDB_REG_CR2;
    sat_result[2] = CDB_REG_CR3;
    sat_result[3] = CDB_REG_CR4;
    return 0;
}

static inline int buffer_xfer(void *buf, int len, int dir) {
    uint8_t cmdbyte = dir ? c_write_buffer : c_read_buffer;
    cmd_t cmd = {cmdbyte<<8, 0, 0, len};
    uint32_t words, n;
    int rc = exec_cmd(cmd, HIRQ_EHST);
    if (rc < 0)
        return rc;
    if (CDB_REG_CR1)  // error
        return -1;

    n = SAT_HIRQ_WAIT_CAP;
    while (!(CDB_REG_HIRQ & HIRQ_DRDY))
        if (!--n) return tmo(cmd);

    uint32_t *p = buf;
    words = (len+3)/4;
    if (dir) {
        while (words--)
            CDB_REG_DATATRNS = *p++;
        // mandatory but mysterious
        CDB_REG_DATATRNS = 0;
        CDB_REG_DATATRNS = 0;
    } else {
        while (words--)
            *p++ = CDB_REG_DATATRNS;
    }

    return 0;
}

#define buffer_read(buf, len)  buffer_xfer(buf, len, 0)
#define buffer_write(buf, len) buffer_xfer((void*)(uintptr_t)buf, len, 1)

// }}}

// Convenience functions {{{
// Most calls use a similar pattern
static inline void set_cmd(cmd_t cmd, int op, int fd, int flags, int len) {
    cmd[0] = (op<<8) | (fd & 0xff);
    cmd[1] = flags;
    cmd[2] = len>>16;
    cmd[3] = len;
}

// Most API returns a negative number for errors
// get_stat() now returns int (see above); a timeout there must return early too, before reading
// sat_result (otherwise stale/undefined register values would be read as a status byte).
#define get_check_stat() do {               \
    int __gsrc = get_stat();                \
    if (__gsrc < 0)                         \
        return __gsrc;                      \
    uint8_t __retval = sat_result[0] >> 8;  \
    if (__retval)                           \
        return -__retval;                   \
} while(0);

#define get_length()    ((sat_result[2]<<16) | sat_result[3])

// Most calls use the same sequence
// exec_cmd's own return is now checked here too -- it used to be discarded, so a timeout on this
// call fell through into get_check_stat(), which would then read stale/undefined CR1-CR4 as if exec_cmd
// had succeeded.
#define simplecall(...) do {                \
    cmd_t cmd;                              \
    int __ecrc;                             \
    set_cmd(cmd, __VA_ARGS__);              \
    __ecrc = exec_cmd(cmd, HIRQ_MPED);      \
    if (__ecrc < 0)                         \
        return __ecrc;                      \
    get_check_stat();                       \
} while(0);

// }}}

// File API {{{
// Given a filename and some FA_xxx flags, return a file descriptor
// (or a negative error corresponding to FR_xxx)
int s_open(const char *filename, int flags) {
    int rc = buffer_write(filename, strlen(filename));   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_open, 0, flags, strlen(filename));
    return sat_result[3];   // handle
}

// Close a file descriptor
int s_close(int fd) {
    simplecall(c_close, fd, 0, 0);
    return 0;
}

// Seek to a byte on an fd. Returns the offset
int s_seek(int fd, int offset, int whence) {
    simplecall(c_seek, fd, whence, offset);
    return (sat_result[2]<<16) | sat_result[3];
}

// Read some data. Returns bytes read
int s_read(int fd, void *buf, int len) {
    int rc;
    if (len > S_MAXBUF || len < 0)
        return -FR_INVALID_PARAMETER;
    simplecall(c_read, fd, 0, len);
    len = get_length();
    rc = buffer_read(buf, len);   // was ignored -- a data-phase timeout returned
    if (rc < 0)                   // len (stale/zero) as if the read had succeeded
        return rc;
    return len;
}

// Write some data. Returns bytes written
int s_write(int fd, const void *buf, int len) {
    int rc;
    if (len > S_MAXBUF || len < 0)
        return -FR_INVALID_PARAMETER;
    rc = buffer_write(buf, len);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_write, fd, 0, len);
    return get_length();
}

// Flush any buffered data to file
int s_sync(int fd) {
    return s_seek(fd, 0, C_SEEK_CUR);
}

// Truncate file at current pointer. Returns new length
int s_truncate(int fd) {
    simplecall(c_truncate, fd, 0, 0);
    return get_length();
}

// Get info on a named file. Pass in a pointer to a buffer and
// its size - the filename may be truncated if the buffer is
// short.  Returns the length of the (truncated) filename.
// If the filename is NULL, reads the next file from the
// current directory (readdir).
int s_stat(const char *filename, s_stat_t *stat, int statsize) {
    int len, rc;
    if (statsize < 9)
        return -FR_INVALID_PARAMETER;
    if (filename) {
        len = strlen(filename);
        rc = buffer_write(filename, len);   // was ignored
        if (rc < 0)
            return rc;
    } else {
        len = 0;
    }
    simplecall(filename ? c_stat : c_readdir, 0, 0, len);
    len = get_length();
    if (len > statsize)
        len = statsize;
    rc = buffer_read(stat, len);   // was ignored -- see satfs.c's sat_list for the
    if (rc < 0)                     // caller-side half of this (SAT_ETIMEDOUT is never "end of readdir")
        return rc;
    return len - 9;
}

// Rename a file.
int s_rename(const char *old, const char *new) {
    // Need both names, zero separated, for the write
    // UPSTREAM.md: word-typed, because buffer_xfer walks it as uint32_t* (was char[512], unaligned)
    uint32_t namebuf32[128];
    char *namebuf = (char *)namebuf32;
    int len1 = strlen(old);
    int len2 = strlen(new);
    int rc;
    if (len1 + 1 + len2 > (int)sizeof namebuf32)   // never past namebuf32
        return -FR_INVALID_NAME;
    memcpy(namebuf, old, len1+1);
    memcpy(namebuf+len1+1, new, len2);
    rc = buffer_write(namebuf, len1+1+len2);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_rename, 0, 0, len1+1+len2);
    return 0;
}

// Create a directory.
int s_mkdir(const char *filename) {
    int len = strlen(filename);
    int rc = buffer_write(filename, len);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_mkdir, 0, 0, len);
    return 0;
}

// Delete a file.
int s_unlink(const char *filename) {
    int len = strlen(filename);
    int rc = buffer_write(filename, len);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_unlink, 0, 0, len);
    return 0;
}

// Open a directory to read file entries.
int s_opendir(const char *filename) {
    int len = strlen(filename);
    int rc = buffer_write(filename, len);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_opendir, 0, 0, len);
    return 0;
}

// Change working directory.
int s_chdir(const char *filename) {
    int len = strlen(filename);
    int rc = buffer_write(filename, len);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_chdir, 0, 0, len);
    return 0;
}

// Get working directory. Adds terminating null.
int s_getcwd(char *filename, int buflen) {
    int rc = buffer_write(".", 1);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_chdir, 0, 0, 1);
    buflen--;
    if (buflen > get_length())
        buflen = get_length();
    rc = buffer_read(filename, buflen);   // was ignored
    if (rc < 0)
        return rc;
    filename[buflen] = 0;
    return buflen;
}

// Set the Satiator RTC. Takes a FAT timestamp.
int s_settime(uint32_t time) {
    simplecall(c_settime, 0, 0, time);
    return 0;
}
// }}}

// System API {{{
static int is_satiator_present(void) {
    // This checks the MPEG version field.
    // Real MPEG cards have version 1.
    // The Satiator has version 2.
    cmd_t cmd = {0x0100, 0, 0, 0};
    // a timeout here is folded into "not present" (0/false), matching this function's existing
    // bool contract -- the exec_cmd call just above it in s_mode (HIRQ_EFLS) is the one that actually
    // hangs on "no Satiator attached" in practice, and that call now propagates SAT_ETIMEDOUT distinctly.
    if (exec_cmd(cmd, 0) < 0)
        return 0;
    return (CDB_REG_CR3 & 0xff) == 2;
}

// HWF-54 (hardware, FW161, 2026-11-19 diag A-E): upstream's s_unknown bootstrap sends 0x9300 and waits for MPED,
// which the Satiator never raises while in CD-ROM mode -- every boot timed out (-20), even with a 120 s cap.
// As Rings' fork (retrohead/libsatiator@9ae02fd satiator.c:312): the menu is a disc image booted by the official
// menu, so it starts in CD-ROM mode (diag E reached API mode with E000/0100/0400). LAUNCHER.BIN is only ever
// entered from the menu, already in API mode, so its build passes -DSAT_INITIAL_MODE=s_api.
#ifndef SAT_INITIAL_MODE
#define SAT_INITIAL_MODE s_cdrom
#endif
static enum satiator_mode cur_mode = SAT_INITIAL_MODE;
#ifdef SAT_HOST_TEST
/* Test-only (menu/host-tests/test_sattimeout.c): cur_mode is static file state, so without this a second
 * s_mode() scenario in the same test binary would see a mode already latched by an earlier one. Never
 * compiled into a real build (SAT_HOST_TEST is never defined there). */
void sat_test_reset_mode(void) { cur_mode = SAT_INITIAL_MODE; }
#endif
int s_mode(enum satiator_mode mode) {
    /* Switch between emulating a CD drive and exposing the SD card API.
     * This function returns:
     *      0: success
     *      -1: Satiator not detected
     *      SAT_ETIMEDOUT: no response within SAT_HIRQ_WAIT_CAP polls
     */
    int rc;

    if (cur_mode == s_unknown) {
        cur_mode = s_api;
        // this recursive bootstrap call's result used to be ignored -- on a cold boot (the exact
        // scenario the controller ruling names) this is the FIRST exec_cmd a real run ever makes, so a
        // silently-ignored timeout here would have let s_mode(s_api) return 0 ("success") even though
        // the Satiator never responded.
        rc = s_mode(s_cdrom);
        if (rc < 0) {
            // without this, cur_mode is left latched at s_api (set just above) even though the
            // bootstrap never actually completed, so a SECOND s_mode(s_api) call (e.g. s_reset_to_
            // satiator on an A+B+C+Start return-to-menu, after the first call already fataled) would
            // short-circuit on "mode == cur_mode" and report success without touching hardware again.
            cur_mode = s_unknown;
            return rc;
        }
    }

    if (mode == cur_mode)
        return 0;

    if (mode == s_cdrom) {
        cmd_t cmd = {0x9300, 1, 0, 0};
        rc = exec_cmd(cmd, HIRQ_MPED);
        if (rc < 0)
            return rc;
    } else {
        cmd_t cmd = {0xe000, 0x0000, 0x00c1, 0x05e7};
        rc = exec_cmd(cmd, HIRQ_EFLS);
        if (rc < 0)
            return rc;

        // is there actually a Satiator attached?
        if (!is_satiator_present())
            return -1;

        // stop CD "drive"
        cmd_t cmd_stop = {0x0400, 0x0001, 0x0000, 0x040f};
        rc = exec_cmd(cmd_stop, 0);
        if (rc < 0)
            return rc;
    }
    cur_mode = mode;
    return 0;
}

// Given the filename of a disc descriptor, load it into the emulated drive.
int s_emulate(const char *filename) {
    int len = strlen(filename);
    int rc = buffer_write(filename, len);   // was ignored
    if (rc < 0)
        return rc;
    simplecall(c_emulate, 0, 0, len);
    return 0;
}

int s_get_fw_version(char *buf, int buflen) {
    int rc;
    simplecall(c_info, i_fw_version, 0, 0);
    buflen--;
    if (buflen > get_length())
        buflen = get_length();
    rc = buffer_read(buf, buflen);   // was ignored
    if (rc < 0)
        return rc;
    buf[buflen] = 0;
    return buflen;
}

int s_get_bootloader_version(uint32_t *version) {
    int rc;
    simplecall(c_info, i_bootloader_version, 0, 0);
    rc = buffer_read(version, 4);   // was ignored
    if (rc < 0)
        return rc;
    return 0;
}

int s_get_serial_number(uint32_t *serial) {
    int rc;
    simplecall(c_info, i_serial_number, 0, 0);
    rc = buffer_read(serial, 4);   // was ignored
    if (rc < 0)
        return rc;
    return 0;
}

int s_get_sd_latency(uint16_t latency_us[], int *errors, int count) {
    int rc;
    simplecall(c_info, i_sd_latency, count, 0);
    rc = buffer_read(latency_us, 2*count);   // was ignored
    if (rc < 0)
        return rc;
    *errors = sat_result[1];
    return 0;
}

int s_format_sd_card(int flags) {
    simplecall(c_mkfs, flags, 0xfeed, 0xdeadbeef);
    return 0;
}

// relaunch the satiator menu
int s_reset_to_satiator(void) {
    s_mode(s_api);
    int (**bios_get_mpeg_rom)(uint32_t index, uint32_t size, uint32_t addr) = (void*)0x06000298;
    int ret = (*bios_get_mpeg_rom)(2, 2, 0x200000);

    if (ret < 0)
        return ret;

    // mask all irqs
    uint32_t sr = 0;
    SAT_IRQ_SAVE(sr);
    SAT_IRQ_MASK(sr);

    void (*entry)(uint32_t) = (void*)0x200000;
    entry(S_BOOT_NO_AUTOLOAD);

    SAT_IRQ_RESTORE(sr);
    return -0x1000;
}

// }}}

// Cartridge API {{{
satiator_cart_header_t *s_find_cartridge(void) {
    uint8_t *ptr = (void*)0x02000000;
    uint8_t *end = ptr + 1048576;

    while (ptr < end) {
        if (!memcmp(ptr, "SatiatorCart", 12))
            return (void*)ptr;

        ptr += 0x100;
    }

    return NULL;
}
// }}}
