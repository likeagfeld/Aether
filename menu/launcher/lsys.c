/* menu/launcher/lsys.c -- newlib system calls over libsatiator, so that cdparse.c / cue2desc.c (stdio, malloc) run
 * unmodified. New code; NOT derived from satiator-menu syscall.c (which includes iapetus). fd = Satiator handle + 3.
 * libsatiator walks every buffer and file name as uint32_t* (satiator.c:114-125, read this session), so all I/O goes
 * through 4-aligned bounce buffers. The heap is the region launcher.ld reserves (>= 64 KB). BSD-3-Clause. */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "satiator.h"
#include "yield.h"

#undef errno
extern int errno;
extern char _heap_start[], _heap_end[];

static uint32_t s_name[76];
static uint32_t s_stat_buf[76];
static uint32_t s_bounce[S_MAXBUF / 4 + 1];

static const char *aligned_name(const char *n)
{
    size_t k = strlen(n);
    if (k > 255) k = 255;
    memcpy(s_name, n, k);
    ((char *)s_name)[k] = 0;
    return (const char *)s_name;
}

static uint32_t s_wfds;                                             /* the handles open for write */
int _open(const char *name, int flags, int mode)
{
    int acc = flags & O_ACCMODE, fa, fd;
    (void)mode;
    fa = acc == O_RDONLY ? FA_READ : acc == O_WRONLY ? FA_WRITE : (FA_READ | FA_WRITE);
    if ((flags & O_CREAT) && (flags & O_TRUNC)) fa |= FA_CREATE_ALWAYS;
    else if (flags & O_CREAT) fa |= FA_OPEN_ALWAYS;
    om_yield();                                                    /* the launch's eye (yield.h): the cue's reads */
    fd = s_open(aligned_name(name), fa);
    if (fd < 0) { errno = ENOENT; return -1; }
    if ((fa & FA_WRITE) && fd < 32) {                              /* open for write: the pacer reads nothing (yield.h) */
        s_wfds |= 1u << fd;
        om_yield_wr++;
    }
    return fd + 3;
}

int _close(int fd)
{
    int r;
    if (fd < 3) return 0;
    r = s_close(fd - 3);
    if (fd - 3 < 32 && (s_wfds & (1u << (fd - 3)))) {
        s_wfds &= ~(1u << (fd - 3));
        om_yield_wr--;
    }
    if (r < 0) { errno = EBADF; return -1; }
    return 0;
}

int _read(int fd, char *ptr, int len)
{
    int got = 0;
    if (fd < 3) return 0;
    while (got < len) {
        int n = len - got > S_MAXBUF ? S_MAXBUF : len - got, r;
        om_yield();
        r = s_read(fd - 3, s_bounce, n);
        if (r < 0) { errno = EIO; return -1; }
        memcpy(ptr + got, s_bounce, (size_t)r);
        got += r;
        if (r < n) break;
    }
    return got;
}

int _write(int fd, const char *ptr, int len)
{
    int done = 0;
    if (fd < 3) return len;                                        /* stdout / stderr are discarded */
    while (done < len) {
        int n = len - done > S_MAXBUF ? S_MAXBUF : len - done, r;
        memcpy(s_bounce, ptr + done, (size_t)n);
        om_yield();
        r = s_write(fd - 3, s_bounce, n);
        if (r != n) { errno = EIO; return -1; }
        done += n;
    }
    return done;
}

int _lseek(int fd, int off, int whence)
{
    int r;
    if (fd < 3) return 0;
    r = s_seek(fd - 3, off, whence);                               /* C_SEEK_* == SEEK_* (0/1/2, satiator.h) */
    if (r < 0) { errno = EINVAL; return -1; }
    return r;
}

int _fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof *st);
    st->st_mode = fd < 3 ? S_IFCHR : S_IFREG;
    return 0;
}

int _stat(const char *name, struct stat *st)
{
    s_stat_t *s = (s_stat_t *)s_stat_buf;
    om_yield();
    if (s_stat(aligned_name(name), s, (int)sizeof s_stat_buf - 4) < 0) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof *st);
    st->st_size = (off_t)s->size;
    st->st_mode = (s->attrib & AM_DIR) ? S_IFDIR : S_IFREG;
    return 0;
}

int _isatty(int fd) { return fd < 3; }

void *_sbrk(ptrdiff_t incr)
{
    static char *brk;
    char *old;
    if (!brk) brk = _heap_start;
    if (brk + incr > _heap_end) { errno = ENOMEM; return (void *)-1; }
    old = brk;
    brk += incr;
    return old;
}

int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
void _exit(int code) { (void)code; for (;;) ; }
