/* menu/src/carddir.h -- the menu's folder on the SD card (Aether rename, 2026-11-25): every menu file lives under
 * /aether/. Cards set up before the rename had /vibemenu/; the Card Manager's Update card moves it (CardFolderMigration),
 * and while both exist the menu shows a banner and does no save swap (card.c om_card_old_folder). Both names stay hidden
 * from the game list (libidx.c), so moving the folder does not change the card's fingerprint. BSD-3-Clause. */
#ifndef OM_CARDDIR_H
#define OM_CARDDIR_H
#define OM_CARD_NAME     "aether"
#define OM_CARD_DIR      "/" OM_CARD_NAME          /* no trailing '/': s_chdir and f_stat refuse one */
#define OM_CARD_ROOT     OM_CARD_DIR "/"
#define OM_CARD_OLD_NAME "vibemenu"                /* the folder before the rename: hidden, detected, never written */
#endif
