#!/bin/sh
# Convert Sega SGL 3.02 (COFF, Nov-96 DTS) to ELF, the method of SaturnSDK Saturn-SDK-SGL convert.sh @2438c59.
# Input mounted read-only at /sgl302; output to /work/build/sgl (gitignored; Sega SDK material is never committed).
set -e
SRC=/sgl302
OUT=/work/build/sgl
rm -rf "$OUT"; mkdir -p "$OUT/lib" "$OUT/inc"
for f in "$SRC"/LIB/*.A "$SRC"/LIB/*.O; do
    b=$(basename "$f"); cp "$f" "$OUT/lib/$b"; chmod 644 "$OUT/lib/$b"
    saturn-sh2-elf-objcopy -Icoff-sh -Oelf32-sh "$OUT/lib/$b"
done
for f in "$SRC"/INC/*; do
    b=$(basename "$f")
    lc=$(echo "$b" | tr 'A-Z' 'a-z')
    case "$lc" in
        stdlib.h|string.h) continue ;;   # never copy SGL's STDLIB.H/STRING.H under ANY name: on a
            # case-insensitive filesystem the uppercase copy itself resolves a lowercase
            # #include <stdlib.h>/<string.h>, shadowing newlib's own headers. Skipping only the
            # lowercase alias (the old behaviour) did not stop this - the uppercase copy still did.
    esac
    cp "$f" "$OUT/inc/$b"
    [ "$lc" = "$b" ] || cp "$f" "$OUT/inc/$lc"
done
for o in SYS_SEC.O SYS_AREJ.O SYS_ARET.O SYS_AREU.O SYS_AREE.O SYS_INIT.O; do
    printf '%s: ' "$o"; saturn-sh2-elf-objdump -f "$OUT/lib/$o" | grep -o 'file format .*'
done
# Post-condition: fail the script if either shadowed name resolves on disk (case-insensitive
# filesystems included) - string.h/stdlib.h must never exist under build/sgl/inc in any spelling.
for h in string.h stdlib.h; do test -e "$OUT/inc/$h" && { echo "SHADOW $h"; exit 1; }; done
