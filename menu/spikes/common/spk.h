#ifndef SPK_H
#define SPK_H
#include "sgl.h"

void   spk_init(const char *title);
void   spk_line(int row, const char *text);
char  *spk_u32(char *buf, Uint32 v);
char  *spk_hex(char *buf, Uint32 v);
Uint16 spk_pressed(void);
Uint32 spk_vblanks(void);
void   spk_wait_press(Uint16 mask);
int    spk_write_file(const char *path, const void *buf, Uint32 len);
#endif
