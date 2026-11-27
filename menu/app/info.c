/* menu/app/info.c -- see info.h. Failures are cached too, so an unreadable image is not re-read every frame.
 * BSD-3-Clause. */
#include <string.h>
#include "info.h"
#include "satfs.h"

typedef struct { uint16_t entry; uint8_t used, ok; om_sysid s; } om_info_slot;
static om_info_slot s_slots[OM_INFO_SLOTS];
static uint32_t s_next;

void om_info_reset(void) { memset(s_slots, 0, sizeof s_slots); s_next = 0; }

static om_info_slot *find(uint16_t entry)
{
    uint32_t i;
    for (i = 0; i < OM_INFO_SLOTS; i++)
        if (s_slots[i].used && s_slots[i].entry == entry) return &s_slots[i];
    return NULL;
}

int om_info_known(uint16_t entry) { return find(entry) != NULL; }

const om_sysid *om_info_find(uint16_t entry)
{
    om_info_slot *x = find(entry);
    return x && x->ok ? &x->s : NULL;
}

int om_info_load(const om_lib_entry *e, uint16_t entry)
{
    om_info_slot *x = find(entry);
    int rc;
    if (x)
        return x->ok ? 0 : OM_E_FIELD;
    x = &s_slots[s_next++ % OM_INFO_SLOTS];
    x->used = 1;
    x->entry = entry;
    rc = om_satfs_sysid(e->path, &x->s);
    x->ok = rc == 0;
    return rc;
}
