/* desc_tool DIR CUE_OR_ISO OUT.desc -- chdir(DIR) then image2desc(name, OUT) exactly as the launcher does. */
#include <stdio.h>
#include <unistd.h>
#include "cdparse.h"
int main(int argc, char **argv)
{
    int rc;
    if (argc != 4 || chdir(argv[1])) { printf("usage/chdir failed\n"); return 2; }
    rc = image2desc(argv[2], argv[3]);
    printf("%s: rc=%d %s\n", argv[2], rc, rc && cdparse_error_string ? cdparse_error_string : "");
    return rc ? 1 : 0;
}
