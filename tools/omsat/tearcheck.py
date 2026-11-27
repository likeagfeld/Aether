"""Torn-frame check for the loading screen's background (Task 13c round 3, emulator captures).

For each emucap PNG, the 13 px strip at native x 306-318 (right of the cover and its shadow: no UI over it) is split
into a top half (rows 26-109) and a bottom half (rows 110-213), and each half is matched against every decoded loop
frame (`python -m omsat.loadbg pack --preview DIR`). A whole frame on screen is explained by one loop frame in both
halves (several loop frames may share a strip, so any frame that is a best match for both counts); a torn one (rows
of two frames) is not. Captures that are no loop frame (the menu, a fade) are skipped.

`python -m omsat.tearcheck --preview DIR CAPTURE.png...`"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main(argv=None) -> int:
    import numpy as np
    from PIL import Image
    ap = argparse.ArgumentParser(prog="python -m omsat.tearcheck")
    ap.add_argument("--preview", required=True, type=Path)
    ap.add_argument("captures", nargs="+", type=Path)
    a = ap.parse_args(argv)
    TOL = 0.15                              # Mednafen's RGB555 -> RGB888 rounding against the packer's preview
    prev = [np.asarray(Image.open(f).convert("RGB"), float) for f in sorted(a.preview.glob("lbp_*.png"))]

    def errs(img, y0, y1):
        reg = img[y0:y1, 306:319]
        return [np.abs(p[y0:y1, 306:319] - reg).mean() for p in prev]

    torn = checked = 0
    for f in a.captures:
        im = Image.open(f).convert("RGB").crop((51, 50, 1011, 722)).resize((320, 224), Image.NEAREST)
        img = np.asarray(im, float)
        if img.mean() < 20:                 # a black frame (a fade)
            continue
        top, bot = errs(img, 26, 110), errs(img, 110, 214)
        ti, bi = int(np.argmin(top)), int(np.argmin(bot))
        if top[ti] > 3 or bot[bi] > 3:      # not a loop frame on screen (the menu, a fade): skipped
            print(f"{f.name}: not a loop frame (err {top[ti]:.1f} / {bot[bi]:.1f}), skipped")
            continue
        checked += 1
        # whole: one loop frame explains both halves (frames that share a half are equally good there)
        whole = any(top[k] <= top[ti] + TOL and bot[k] <= bot[bi] + TOL for k in range(len(prev)))
        torn += not whole
        print(f"{f.name}: top best {ti} ({top[ti]:.2f}), bottom best {bi} ({bot[bi]:.2f}) {'whole' if whole else 'TORN'}")
    print(f"tearcheck: {checked} captures, {torn} torn")
    return 1 if torn else 0


if __name__ == "__main__":
    sys.exit(main())
