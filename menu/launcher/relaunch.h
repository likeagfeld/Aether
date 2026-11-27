/* menu/launcher/relaunch.h -- the error screen's exit A: back to Aether (autoboot.iso) after a failed launch.
 * BSD-3-Clause. */
#ifndef OM_RELAUNCH_H
#define OM_RELAUNCH_H
/* Each op returns 0 (or >= 0) on success, < 0 on failure. run and sysres return only on failure. */
typedef struct {
    void (*cd_stop)(void);                    /* stop the CD play, reset the selector (cdblk.c), bounded */
    int (*mode_api)(void);                    /* s_mode(s_api) */
    int (*emulate_off)(void);                 /* s_emulate("") */
    int (*chdir_root)(void);                  /* s_chdir("/") */
    int (*exists)(const char *path);          /* 1 if the file is on the card */
    int (*run)(const char *image);            /* om_run_launch of the menu disc in "/" */
    int (*sysres)(void);                      /* SMPC SYSRES: the Satiator's own menu then autoboots autoboot.iso */
    void (*say)(const char *what, int rc);    /* one progress or error line */
} om_relaunch_ops;
#define OM_RELAUNCH_TRIES 3                   /* s_mode(s_api) attempts, unless one times out */
int om_relaunch(const om_relaunch_ops *o);    /* returns only if SYSRES failed too: its rc */
#endif
