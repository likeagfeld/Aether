/* menu/host-tests/test_remap.c -- the action -> button table (#18). BSD-3-Clause. */
#include "th.h"
#include "remap.h"

int main(void)
{
    static const uint16_t mask[OM_BTN_COUNT] = { 1, 2, 4, 8, 16, 32, 64, 128 };   /* host stand-ins for PER_DGT_* */
    om_remap m;
    om_remap_default(&m);
    CHECK(om_remap_valid(&m));
    CHECK_INT(m.btn[OM_ACT_OK], OM_BTN_A); CHECK_INT(m.btn[OM_ACT_BACK], OM_BTN_B);
    CHECK_INT(m.btn[OM_ACT_INFO], OM_BTN_C); CHECK_INT(m.btn[OM_ACT_FAV], OM_BTN_X);
    CHECK_INT(m.btn[OM_ACT_PGDN], OM_BTN_R);
    CHECK_INT(om_remap_acts(&m, 1 | 128, mask), (1u << OM_ACT_OK) | (1u << OM_ACT_PGDN));
    CHECK_INT(om_remap_mask(&m, OM_ACT_SORT, mask), 16);
    CHECK_INT(om_remap_assign(&m, OM_ACT_OK, OM_BTN_B), OM_ACT_BACK);      /* conflict: the two swap */
    CHECK_INT(m.btn[OM_ACT_OK], OM_BTN_B); CHECK_INT(m.btn[OM_ACT_BACK], OM_BTN_A);
    CHECK(om_remap_valid(&m));
    CHECK_INT(om_remap_acts(&m, 1, mask), 1u << OM_ACT_BACK);              /* physical A is Back now */
    CHECK_INT(om_remap_assign(&m, OM_ACT_OK, OM_BTN_B), -1);               /* already there: no change */
    CHECK_INT(om_remap_assign(&m, OM_ACT_COUNT, OM_BTN_A), -1);            /* out of range: refused */
    CHECK_INT(om_remap_assign(&m, OM_ACT_OK, OM_BTN_COUNT), -1);
    CHECK_INT(m.btn[OM_ACT_OK], OM_BTN_B);
    om_remap_default(&m);
    CHECK_INT(om_remap_cycle(&m, OM_ACT_PGDN, 1), OM_BTN_A);               /* R wraps to A, swapping with OK */
    CHECK_INT(m.btn[OM_ACT_OK], OM_BTN_R);
    CHECK_INT(om_remap_cycle(&m, OM_ACT_PGDN, -1), OM_BTN_R);              /* and back */
    CHECK_INT(m.btn[OM_ACT_OK], OM_BTN_A);
    {   /* every cycle step keeps a permutation */
        int k;
        for (k = 0; k < 200; k++) om_remap_cycle(&m, (uint8_t)(k % OM_ACT_COUNT), (k & 1) ? 1 : -1);
        CHECK(om_remap_valid(&m));
    }
    m.btn[OM_ACT_SORT] = m.btn[OM_ACT_FAV];
    CHECK(!om_remap_valid(&m));                                            /* a duplicate */
    m.btn[OM_ACT_SORT] = 9;
    CHECK(!om_remap_valid(&m));                                            /* out of range */
    CHECK_INT(om_btn_parse("A"), OM_BTN_A); CHECK_INT(om_btn_parse("r"), OM_BTN_R);
    CHECK_INT(om_btn_parse("Q"), -1); CHECK_INT(om_btn_parse(""), -1); CHECK_INT(om_btn_parse("AB"), -1);
    CHECK_INT(om_btn_letter(OM_BTN_Z), 'Z'); CHECK_INT(om_btn_letter(99), '?');
    CHECK_STR(om_act_key(OM_ACT_FILTER), "pad_filter"); CHECK_STR(om_act_label(OM_ACT_PGUP), "Page up");
    CHECK_STR(om_act_key(OM_ACT_COUNT), "");
    return th_done("test_remap");
}
