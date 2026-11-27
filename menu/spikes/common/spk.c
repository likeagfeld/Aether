#include "spk.h"
#include "satiator.h"

static volatile Uint32 g_vbl;
static Uint16 g_prev = 0xffff;

static void vbl_tick(void) { g_vbl++; }

void spk_init(const char *title)
{
    slInitSystem(TV_320x224, NULL, 1);
    slIntFunction(vbl_tick);
    spk_line(1, title);
}

void spk_line(int row, const char *text)
{
    slPrint("                                      ", slLocate(1, row));
    slPrint((char *)text, slLocate(1, row));
}

char *spk_u32(char *buf, Uint32 v)
{
    char tmp[11];
    int n = 0, i = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
    return buf;
}

char *spk_hex(char *buf, Uint32 v)
{
    static const char hx[] = "0123456789ABCDEF";
    int i;
    for (i = 0; i < 8; i++) buf[i] = hx[(v >> (28 - 4 * i)) & 15];
    buf[8] = 0;
    return buf;
}

Uint16 spk_pressed(void)
{
    Uint16 cur, newly;
    if (!Per_Connect1) return 0;
    cur = Smpc_Peripheral[0].data;
    newly = (Uint16)(g_prev & ~cur);
    g_prev = cur;
    return newly;
}

Uint32 spk_vblanks(void) { return g_vbl; }

void spk_wait_press(Uint16 mask)
{
    for (;;) {
        slSynch();
        if (spk_pressed() & mask) return;
    }
}

int spk_write_file(const char *path, const void *buf, Uint32 len)
{
    const Uint8 *p = (const Uint8 *)buf;
    int fd = s_open(path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fd < 0) return fd;
    while (len) {
        int n = len > S_MAXBUF ? S_MAXBUF : (int)len;
        int w = s_write(fd, p, n);
        if (w != n) { s_close(fd); return -1000 - w; }
        p += n; len -= (Uint32)n;
    }
    s_close(fd);
    return 0;
}
