/* menu/src/remap.c -- see remap.h (carried from M3.5 Task 4, #18). The table is a permutation of 8 actions
 * over 8 buttons, so a conflict is always resolved by swapping. BSD-3-Clause. */
#include "remap.h"

static const char k_letter[OM_BTN_COUNT] = { 'A', 'B', 'C', 'X', 'Y', 'Z', 'L', 'R' };
static const uint8_t k_default[OM_ACT_COUNT] = { OM_BTN_A, OM_BTN_B, OM_BTN_C, OM_BTN_X, OM_BTN_Y, OM_BTN_Z,
                                                 OM_BTN_L, OM_BTN_R };
static const char *const k_key[OM_ACT_COUNT] = { "pad_ok", "pad_back", "pad_info", "pad_fav", "pad_sort",
                                                 "pad_filter", "pad_pgup", "pad_pgdn" };
static const char *const k_label[OM_ACT_COUNT] = { "Play", "Back", "Info", "Favorite", "Sort", "Filter", "Page up",
                                                   "Page down" };

void om_remap_default(om_remap *m)
{
    uint32_t i;
    for (i = 0; i < OM_ACT_COUNT; i++) m->btn[i] = k_default[i];
}

int om_remap_valid(const om_remap *m)
{
    uint32_t i, seen = 0;
    for (i = 0; i < OM_ACT_COUNT; i++) {
        if (m->btn[i] >= OM_BTN_COUNT || ((seen >> m->btn[i]) & 1u)) return 0;
        seen |= 1u << m->btn[i];
    }
    return 1;
}

int om_remap_assign(om_remap *m, uint8_t act, uint8_t btn)
{
    uint32_t i;
    uint8_t old;
    if (act >= OM_ACT_COUNT || btn >= OM_BTN_COUNT) return -1;
    old = m->btn[act];
    for (i = 0; i < OM_ACT_COUNT; i++)
        if (i != act && m->btn[i] == btn) { m->btn[i] = old; m->btn[act] = btn; return (int)i; }
    m->btn[act] = btn;
    return -1;
}

uint8_t om_remap_cycle(om_remap *m, uint8_t act, int dir)
{
    uint8_t b;
    if (act >= OM_ACT_COUNT) return 0;
    b = (uint8_t)((m->btn[act] + OM_BTN_COUNT + (dir < 0 ? -1 : 1)) % OM_BTN_COUNT);
    om_remap_assign(m, act, b);
    return b;
}

uint16_t om_remap_acts(const om_remap *m, uint16_t pad, const uint16_t mask[OM_BTN_COUNT])
{
    uint16_t a = 0;
    uint32_t i;
    for (i = 0; i < OM_ACT_COUNT; i++)
        if (m->btn[i] < OM_BTN_COUNT && (pad & mask[m->btn[i]])) a = (uint16_t)(a | (1u << i));
    return a;
}

uint16_t om_remap_mask(const om_remap *m, uint8_t act, const uint16_t mask[OM_BTN_COUNT])
{
    return act < OM_ACT_COUNT && m->btn[act] < OM_BTN_COUNT ? mask[m->btn[act]] : 0;
}

char om_btn_letter(uint8_t btn) { return btn < OM_BTN_COUNT ? k_letter[btn] : '?'; }

int om_btn_parse(const char *v)
{
    uint32_t i;
    char c;
    if (!v[0] || v[1]) return -1;
    c = (char)(v[0] >= 'a' && v[0] <= 'z' ? v[0] - 32 : v[0]);
    for (i = 0; i < OM_BTN_COUNT; i++) if (k_letter[i] == c) return (int)i;
    return -1;
}

const char *om_act_key(uint8_t act) { return act < OM_ACT_COUNT ? k_key[act] : ""; }
const char *om_act_label(uint8_t act) { return act < OM_ACT_COUNT ? k_label[act] : ""; }
