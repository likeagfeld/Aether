/* menu/host-tests/test_idle.c -- the idle timer; the waking input is reported once and is
 * never a press the menu acts on. BSD-3-Clause. */
#include "th.h"
#include "idle.h"

int main(void)
{
    om_idle i;
    CHECK_INT(om_idle_vblanks(0, 5994), 0);
    CHECK_INT(om_idle_vblanks(1, 5994), 3596);              /* 60 s x 59.94 Hz (HWF-11) */
    CHECK_INT(om_idle_vblanks(5, 5994), 17982);             /* the default, 5 minutes */
    CHECK_INT(om_idle_vblanks(5, 5000), 15000);             /* PAL */
    om_idle_init(&i, 100, 1000);
    CHECK_INT(om_idle_step(&i, 1050, 0), OM_IDLE_RUN);
    CHECK_INT(om_idle_step(&i, 1099, 0), OM_IDLE_RUN);
    CHECK_INT(om_idle_step(&i, 1100, 0), OM_IDLE_START); CHECK_INT(i.active, 1);
    CHECK_INT(om_idle_step(&i, 1200, 0), OM_IDLE_RUN);      /* active: no second START */
    CHECK_INT(om_idle_step(&i, 1201, 1), OM_IDLE_WAKE); CHECK_INT(i.active, 0);
    CHECK_INT(om_idle_step(&i, 1202, 1), OM_IDLE_RUN);      /* still held: not a second wake */
    CHECK_INT(om_idle_step(&i, 1301, 0), OM_IDLE_RUN);      /* counts from the last input (1202) */
    CHECK_INT(om_idle_step(&i, 1302, 0), OM_IDLE_START);
    om_idle_init(&i, 100, 0);
    om_idle_step(&i, 99, 1);
    CHECK_INT(om_idle_step(&i, 198, 0), OM_IDLE_RUN);
    CHECK_INT(om_idle_step(&i, 199, 0), OM_IDLE_START);
    om_idle_init(&i, 0, 0);
    CHECK_INT(om_idle_step(&i, 0xFFFFFFF0u, 0), OM_IDLE_RUN);    /* off: never */
    om_idle_init(&i, 100, 0xFFFFFFC0u);
    CHECK_INT(om_idle_step(&i, 0x23u, 0), OM_IDLE_RUN);          /* 99 V-blanks across the wrap */
    CHECK_INT(om_idle_step(&i, 0x24u, 0), OM_IDLE_START);
    om_idle_init(&i, 100, 0);
    om_idle_set(&i, 50, 80);                                    /* a new timeout (Settings) or a blocking screen ended */
    CHECK_INT(om_idle_step(&i, 129, 0), OM_IDLE_RUN);
    CHECK_INT(om_idle_step(&i, 130, 0), OM_IDLE_START);
    om_idle_set(&i, 0, 131);                                    /* Saver -> Off while it runs: it wakes at once */
    CHECK_INT(i.active, 0);
    CHECK_INT(om_idle_step(&i, 5000000u, 0), OM_IDLE_RUN);
    return th_done("test_idle");
}
