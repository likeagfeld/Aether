/* menu/app/popabi.h -- the in-menu popups' overlay (Cheats, Manage saves, About): what the menu (popup.c, HWRAM) and
 * POPUP.BIN (menu/pop/, linked to run in LWRAM at OM_LW_LBG) agree on. POPUP.BIN is built in the menu's own --app step
 * against menu.elf's symbols and shipped in the menu's disc image; tools/sbuild.py patches its CRC-32 into the menu's
 * om_pop_crc word, so a menu only ever runs the overlay it was built with. BSD-3-Clause. */
#ifndef OM_POPABI_H
#define OM_POPABI_H
#include <stdint.h>
#include "gameid.h"
#include "gamesdb.h"
#include "uiview.h"

#define OM_POP_MAGIC    "OMSATPOP"
#define OM_POP_VERSION  1u
#define OM_POP_ISO_NAME "POPUP.BIN"         /* in the menu disc's root directory */
#define OM_POP_ISO_PATH "/autoboot.iso"     /* the menu's disc image on the card (install.py, the Card Manager) */

enum { OM_POP_CHEATS = 1, OM_POP_SAVES, OM_POP_SAVES_ALL,   /* Launch Options > Cheats / Manage saves; Settings */
       OM_POP_ABOUT };                                          /* Settings > About (pop/pabout.h) */

/* om_pop_hdr.key's result bits */
#define OM_POP_CLOSED   1u                  /* B on the popup's first page: back to the panel, at once */
#define OM_POP_SAVED    2u                  /* CHEATS.PAK was written (the panel's cheat line may change) */
#define OM_POP_SFX_MOVE 4u
#define OM_POP_SFX_OK   8u
#define OM_POP_SFX_BACK 16u
#define OM_POP_SFX_ERR  32u
#define OM_POP_IMPORT   64u                 /* with CLOSED: Manage / Import Saves' import row: run the import */

typedef struct {
    uint8_t kind;                           /* OM_POP_* */
    uint8_t cheat_jhl;                      /* the game's loader resolves to JHL: cheats need CWX (a note) */
    char gid[OM_GID_MAX];                   /* the game's id (saves), "" for Settings > Manage saves */
    char key[17];                           /* its GAMES.DB key (CHEATS.PAK's record) */
    char title[64];                         /* its shown name */
    const om_style *sty;                    /* the theme's colours */
    const om_gamesdb *db;                   /* GAMES.DB (Settings' manager names the folders), or NULL */
    uint8_t *arena;                         /* the card-scan work area, lent while the popup is open (4-aligned) */
    uint32_t arena_cap;
    uint8_t *tables;                        /* the art job's decode canvas (art.h om_art_lend), lent the same way: the */
    uint32_t tables_cap;                    /* lists, the folder names, one game's cheats (4-aligned) */
    uint32_t fps;                           /* x100 (marquees) */
} om_pop_req;

/* the console setup run (menu/pop/psetup.c, /aether/DIAG.TXT): arena = the card-scan work area, >= 28 KB, 4-aligned,
 * nothing in it kept */
typedef struct { uint8_t *arena; uint32_t arena_cap; uint32_t games; uint8_t loader; } om_pop_setup_req;

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t img_len;                       /* bytes of the file: header, code, data */
    uint32_t bss_start, bss_end;            /* zeroed by the menu before open */
    /* < 0: the popup could not open (the caller says why with om_pop_why and stays on the panel) */
    int (*open)(const om_pop_req *r, char *why, uint32_t why_cap);
    /* one frame's fresh presses (SGL PER_DGT_* bits), rep: the D-pad's auto-repeat; OM_POP_* bits */
    uint32_t (*key)(uint16_t press, uint16_t rep);
    /* inside the menu's frame (its V-blank list is open, the safe area applied); vis 0..256 */
    void (*draw)(uint16_t vis, uint32_t frame);
    void (*close)(void);                    /* nothing open, the working folder back to "/" */
    /* the console setup file, run once and returning at once (no open/close around it): 0 unchanged, 1 written,
     * < 0 an rc (app/setup.h om_su_save) */
    int (*setup)(const om_pop_setup_req *q);
} om_pop_hdr;
#endif
