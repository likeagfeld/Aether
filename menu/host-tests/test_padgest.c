/* menu/host-tests/test_padgest.c -- the error screens' "C: reset console" gesture (review-final-m3 I4): C alone,
 * fired on release; never while A+B+C+Start is being assembled, never from a button held at entry. BSD-3-Clause. */
#include "th.h"
#include "padgest.h"

#define A  (1u << 10)
#define B  (1u << 8)
#define C  (1u << 9)
#define ST (1u << 11)

static int run(om_cgest *g, const uint16_t *seq, int n)
{
    int i, fired = 0;
    for (i = 0; i < n; i++)
        fired += om_cgest_step(g, seq[i], C);
    return fired;
}

int main(void)
{
    om_cgest g;
    {   /* C pressed and released alone: one reset, on the release frame */
        om_cgest_init(&g, 0);
        CHECK_INT(om_cgest_step(&g, C, C), 0);
        CHECK_INT(om_cgest_step(&g, C, C), 0);
        CHECK_INT(om_cgest_step(&g, 0, C), 1);
        CHECK_INT(om_cgest_step(&g, 0, C), 0);
    }
    {   /* C first, then A, B, Start (the user assembling the soft-reset combo C-first): never a reset */
        const uint16_t s[] = { C, C | A, C | A | B, C | A | B | ST, A | B, 0 };
        om_cgest_init(&g, 0);
        CHECK_INT(run(&g, s, 6), 0);
    }
    {   /* A+B held, then C, then Start: never */
        const uint16_t s[] = { A, A | B, A | B | C, A | B | C | ST, 0 };
        om_cgest_init(&g, 0);
        CHECK_INT(run(&g, s, 5), 0);
    }
    {   /* C held when the screen appears (it was part of the combo that led here): not a press */
        const uint16_t s[] = { C, C, 0 };
        om_cgest_init(&g, C);
        CHECK_INT(run(&g, s, 3), 0);
        CHECK_INT(run(&g, (const uint16_t[]){ C, 0 }, 2), 1);       /* a fresh press after all-up works */
    }
    {   /* C, then A joins and leaves before C is released: disarmed */
        const uint16_t s[] = { C, C | A, C, 0 };
        om_cgest_init(&g, 0);
        CHECK_INT(run(&g, s, 4), 0);
    }
    {   /* another button pressed alone, released, then C alone: fires */
        const uint16_t s[] = { A, 0, C, 0 };
        om_cgest_init(&g, 0);
        CHECK_INT(run(&g, s, 4), 1);
    }
    return th_done("test_padgest");
}
