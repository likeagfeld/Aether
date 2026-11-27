#include "th.h"
#include "tree.h"

static om_tree t;

static om_lib_folder ef_fold[4];
static char ef_pool[64];
static uint8_t ef_out[256];

int main(void)
{
    uint32_t n, pos, len, elen;
    uint16_t cnt;
    uint8_t *f = th_load(FIX("tiny.library.idx"), &n), *m;
    om_libidx l;
    const om_item *it;
    CHECK_INT(om_libidx_open(&l, f, n), OM_OK);
    CHECK_INT(om_tree_build(&t, &l), OM_OK);
    it = om_tree_items(&t, &cnt);                         /* root: folders by name bytes ("RPG" < "Racing"), then games */
    CHECK_INT(cnt, 3);
    CHECK_INT(it[0].kind, 0); CHECK_INT(it[0].idx, 2);
    CHECK_INT(it[1].kind, 0); CHECK_INT(it[1].idx, 1);
    CHECK_INT(it[2].kind, 1); CHECK_INT(it[2].idx, 3);
    CHECK_INT(om_tree_back(&t), 0);                       /* B at the root does nothing (spec §7.4) */
    CHECK_INT(om_tree_enter(&t), 1);                      /* A on RPG */
    CHECK_INT(t.cur, 2);
    it = om_tree_items(&t, &cnt);
    CHECK_INT(cnt, 2); CHECK_INT(it[0].idx, 1); CHECK_INT(it[1].idx, 2);
    om_tree_move(&t, 1);
    CHECK_INT(om_tree_cursor(&t), 1);
    om_tree_move(&t, 5);
    CHECK_INT(om_tree_cursor(&t), 1);                     /* clamps, no wrap */
    CHECK_INT(om_tree_enter(&t), 2);                      /* A on a game = launch */
    CHECK_INT(om_tree_back(&t), 1);
    CHECK_INT(t.cur, 0);
    CHECK_INT(om_tree_cursor(&t), 0);                     /* root cursor remembered at RPG */
    om_tree_move(&t, 1);
    om_tree_enter(&t);                                    /* Racing */
    CHECK_INT(t.cur, 1);
    om_tree_back(&t);
    om_tree_move(&t, -1);
    om_tree_enter(&t);                                    /* RPG again: cursor 1 remembered per folder */
    CHECK_INT(om_tree_cursor(&t), 1);
    om_tree_page(&t, -1);
    CHECK_INT(om_tree_cursor(&t), 0);
    CHECK_STR(om_tree_folder_name("Racing/Daytona USA"), "Daytona USA");
    CHECK_STR(om_tree_folder_name("RPG"), "RPG");
    om_scrollbar(3, 0, 12, 23, &pos, &len); CHECK_INT(pos, 0); CHECK_INT(len, 23);
    om_scrollbar(120, 0, 12, 23, &pos, &len); CHECK_INT(len, 2); CHECK_INT(pos, 0);
    om_scrollbar(120, 108, 12, 23, &pos, &len); CHECK_INT(pos, 21);
    m = th_dup(f, n);                                     /* folder 2's parent = 2: a cycle */
    m[64 + 32 * 4 + 8 * 2 + 5] = 2;
    CHECK_INT(om_libidx_open(&l, m, n), OM_OK);           /* the format allows it (parent < nfolders) ... */
    CHECK_INT(om_tree_build(&t, &l), OM_E_FIELD);         /* ... the tree does not */
    free(m);

    /* hostile: cursor at the list ends -- re-open the real fixture (m above left `l` on the cyclic one) and
     * drive the root row (3 items) past both boundaries; move/page must clamp, never wrap or go negative. */
    CHECK_INT(om_libidx_open(&l, f, n), OM_OK);
    CHECK_INT(om_tree_build(&t, &l), OM_OK);
    om_tree_move(&t, -50);
    CHECK_INT(om_tree_cursor(&t), 0); CHECK_INT(om_tree_top(&t), 0);
    om_tree_page(&t, -1);
    CHECK_INT(om_tree_cursor(&t), 0);                     /* already at the top: paging up is a no-op */
    om_tree_move(&t, 50);
    CHECK_INT(om_tree_cursor(&t), 2);                     /* clamped to the last of 3 items, not wrapped */
    om_tree_page(&t, 1);
    CHECK_INT(om_tree_cursor(&t), 2);                     /* already at the bottom: paging down is a no-op */
    CHECK(om_tree_selected(&t) != NULL);

    /* hostile: an empty card -- one folder (root), zero entries. Every navigation op must be a safe no-op:
     * no OOB read of items[] (whose only backing is the never-populated root slot) and no crash. */
    {
        om_libw w;
        om_libidx el;
        om_tree et;
        om_libw_init(&w, NULL, 0, NULL, ef_fold, 4, ef_pool, sizeof ef_pool);
        CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
        CHECK_INT(om_libw_finish(&w, 1, 0, 20261117u, ef_out, sizeof ef_out, &elen), OM_OK);
        CHECK_INT(om_libidx_open(&el, ef_out, elen), OM_OK);
        CHECK_INT(el.count, 0); CHECK_INT(el.nfolders, 1);
        CHECK_INT(om_tree_build(&et, &el), OM_OK);
        it = om_tree_items(&et, &cnt);
        CHECK_INT(cnt, 0);
        CHECK(om_tree_selected(&et) == NULL);
        CHECK_INT(om_tree_cursor(&et), 0); CHECK_INT(om_tree_top(&et), 0);
        om_tree_move(&et, 1); CHECK_INT(om_tree_cursor(&et), 0);
        om_tree_move(&et, -1); CHECK_INT(om_tree_cursor(&et), 0);
        om_tree_page(&et, 1); CHECK_INT(om_tree_cursor(&et), 0);
        CHECK_INT(om_tree_enter(&et), 0);                 /* nothing to enter */
        CHECK_INT(om_tree_back(&et), 0);                  /* already at the root */
    }

    free(f);
    return th_done("test_tree");
}
