/* menu/src/remap.h -- the menu's action -> button table (SETTINGS.INI pad_* keys, contract/SPEC.md §5.5; M6 Task 10,
 * carried from M3.5 Task 4, #18). Eight actions over the eight face/shoulder buttons A B C X Y Z L R, always a
 * permutation: a conflicting assignment swaps the two actions. Start, the D-pad and A+B+C+Start are fixed (safety) and
 * are not in this table. The view supplies the button bit masks (SGL PER_DGT_*), so this file stays portable.
 * BSD-3-Clause. */
#ifndef OM_REMAP_H
#define OM_REMAP_H
#include <stdint.h>
enum { OM_ACT_OK, OM_ACT_BACK, OM_ACT_INFO, OM_ACT_FAV, OM_ACT_SORT, OM_ACT_FILTER, OM_ACT_PGUP, OM_ACT_PGDN,
       OM_ACT_COUNT };
enum { OM_BTN_A, OM_BTN_B, OM_BTN_C, OM_BTN_X, OM_BTN_Y, OM_BTN_Z, OM_BTN_L, OM_BTN_R, OM_BTN_COUNT };
typedef struct { uint8_t btn[OM_ACT_COUNT]; } om_remap;
void om_remap_default(om_remap *m);
int om_remap_valid(const om_remap *m);
int om_remap_assign(om_remap *m, uint8_t act, uint8_t btn);   /* -> the action swapped onto act's old button, or -1 */
uint8_t om_remap_cycle(om_remap *m, uint8_t act, int dir);    /* next (dir > 0) / previous button, swapping; -> it */
uint16_t om_remap_acts(const om_remap *m, uint16_t pad, const uint16_t mask[OM_BTN_COUNT]);   /* bit a = action a */
uint16_t om_remap_mask(const om_remap *m, uint8_t act, const uint16_t mask[OM_BTN_COUNT]);    /* its pad bit */
char om_btn_letter(uint8_t btn);
int om_btn_parse(const char *v);
const char *om_act_key(uint8_t act);     /* "pad_ok" ... "pad_pgdn" */
const char *om_act_label(uint8_t act);   /* "Play" "Back" "Info" "Favorite" "Sort" "Filter" "Page up" "Page down" */
#endif
