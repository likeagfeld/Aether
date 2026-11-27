/* manager/golden/sortdir.c -- VibeMenu Card Manager golden harness (M4 Task 1b). GPL-3.0-or-later.
 * Linked with -Wl,--wrap=opendir,--wrap=readdir,--wrap=closedir so that menu/host-tests/fake_satiator.c's listings
 * (s_opendir/s_stat over readdir, unsorted on a host filesystem) come back in unsigned byte order of name -- the order
 * the C# CardFs lists in. On a real card the console lists in FAT directory order; the import's outcome depends on
 * that order only for which file of a colliding name gets a ~n suffix (manager/golden/README.md). */
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

DIR *__real_opendir(const char *name);
struct dirent *__real_readdir(DIR *d);
int __real_closedir(DIR *d);

#define MAXH 16
static struct { DIR *d; struct dirent *e; int n, at; } s_h[MAXH];

static int cmp(const void *a, const void *b)
{
    return strcmp(((const struct dirent *)a)->d_name, ((const struct dirent *)b)->d_name);   /* unsigned bytes */
}

DIR *__wrap_opendir(const char *name)
{
    DIR *d = __real_opendir(name);
    struct dirent *e;
    int i, cap = 64;
    if (!d) return d;
    for (i = 0; i < MAXH && s_h[i].d; i++) ;
    if (i == MAXH) abort();
    s_h[i].d = d;
    s_h[i].n = s_h[i].at = 0;
    s_h[i].e = (struct dirent *)malloc(sizeof(struct dirent) * (size_t)cap);
    while ((e = __real_readdir(d)) != NULL) {
        if (s_h[i].n == cap) { cap *= 2; s_h[i].e = (struct dirent *)realloc(s_h[i].e, sizeof(struct dirent) * (size_t)cap); }
        s_h[i].e[s_h[i].n++] = *e;
    }
    qsort(s_h[i].e, (size_t)s_h[i].n, sizeof(struct dirent), cmp);
    return d;
}

struct dirent *__wrap_readdir(DIR *d)
{
    int i;
    for (i = 0; i < MAXH; i++)
        if (s_h[i].d == d) return s_h[i].at < s_h[i].n ? &s_h[i].e[s_h[i].at++] : NULL;
    return __real_readdir(d);
}

int __wrap_closedir(DIR *d)
{
    int i;
    for (i = 0; i < MAXH; i++)
        if (s_h[i].d == d) { free(s_h[i].e); s_h[i].d = NULL; s_h[i].e = NULL; }
    return __real_closedir(d);
}
