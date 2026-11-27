/* menu/app/fake/fakesat.h -- --app-fake only (plan D6). BSD-3-Clause. */
#ifndef OM_FAKESAT_H
#define OM_FAKESAT_H
#include <stdint.h>
void om_fakesat_mount(const uint8_t *blob, uint32_t len);
int om_fakesat_has(const char *path);
const uint8_t *om_fakesat_data(const char *path);   /* a blob file's bytes, or NULL (fakepad.c's soak script) */
/* the most SD files ever open at once this run (the two-files rule, ledger ruling I2) */
int om_fakesat_fd_max(void);        /* a file in the blob (e.g. the held-scroll script marker) */
/* Task 13c round 2: read iso_name from the menu disc into dst (cap bytes) and serve it as card_path. */
int om_fakesat_cd_file(const char *iso_name, const char *card_path, uint8_t *dst, uint32_t cap);
#endif
