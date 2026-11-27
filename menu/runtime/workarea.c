/* SGL work area, menu-sized. Layout and symbol set from SGL302 SAMPLE/WORKAREA/WORKAREA.C;
 * only MAX_* and WORK_AREA changed. Occupies 0x060F0000-0x060F99F8 (ledger: docs/ledger.md). */
#include "sl_def.h"

#define SystemWork    0x060ffc00
#define MAX_VERTICES  256
#define MAX_POLYGONS  256
#define MAX_EVENTS    64
#define MAX_WORKS     256
#define WORK_AREA     0x060f0000
#define trans_list    0x060fb800
#define pcmbuf        (SoundRAM + 0x78000)
#define PCM_SIZE      0x8000
#define master_stack  SystemWork
#define slave_stack   0x06001e00

#define _Byte_        sizeof(Uint8)
#define _LongWord_    sizeof(Uint32)
#define _Sprite_      (sizeof(Uint16) * 18)
#define AdjWork(pt, sz, ct) (pt + (sz) * (ct))

enum workarea {
    sort_list  = WORK_AREA,
    zbuffer    = AdjWork(sort_list,  _LongWord_ * 3, MAX_POLYGONS + 6),
    spritebuf  = AdjWork(zbuffer,    _LongWord_, 512),
    pbuffer    = AdjWork(spritebuf,  _Sprite_, (MAX_POLYGONS + 6) * 2),
    clofstbuf  = AdjWork(pbuffer,    _LongWord_ * 4, MAX_VERTICES),
    commandbuf = AdjWork(clofstbuf,  _Byte_ * 32 * 3, 32),
    NextEntry  = AdjWork(commandbuf, _LongWord_ * 8, MAX_POLYGONS)
};

const void  *MasterStack   = (void *)(master_stack);
const void  *SlaveStack    = (void *)(slave_stack);
const Uint16 MaxVertices   = MAX_VERTICES;
const Uint16 MaxPolygons   = MAX_POLYGONS;
const Uint16 EventSize     = sizeof(EVENT);
const Uint16 WorkSize      = sizeof(WORK);
const Uint16 MaxEvents     = MAX_EVENTS;
const Uint16 MaxWorks      = MAX_WORKS;
const void  *SortList      = (void *)(sort_list);
const Uint32 SortListSize  = (MAX_POLYGONS + 6) * _LongWord_ * 3;
const void  *Zbuffer       = (void *)(zbuffer);
const void  *SpriteBuf     = (void *)(spritebuf);
const Uint32 SpriteBufSize = _Sprite_ * (MAX_POLYGONS + 6) * 2;
const void  *Pbuffer       = (void *)(pbuffer);
const void  *CLOfstBuf     = (void *)(clofstbuf);
const void  *CommandBuf    = (void *)(commandbuf);
const void  *PCM_Work      = (void *)(pcmbuf);
const Uint32 PCM_WkSize    = PCM_SIZE;
const void  *TransList     = (void *)(trans_list);

EVENT  EventBuf[MAX_EVENTS];
WORK   WorkBuf[MAX_WORKS];
EVENT *RemainEvent[MAX_EVENTS];
WORK  *RemainWork[MAX_WORKS];
