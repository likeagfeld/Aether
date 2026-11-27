"""Emulator-tier A/B for Task 13: the mean colour of a box in two screenshots must differ by at least --min-delta
(sum of |dR|+|dG|+|dB|), or, with --max-delta, by at most that. A gate whose two legs are equal is not an A/B.
`python -m omsat.pixcheck A.png B.png --box X0 Y0 X1 Y1 (--min-delta N | --max-delta N)`"""
from __future__ import annotations

import argparse
import sys

from PIL import Image


def mean(path: str, box) -> tuple[float, float, float]:
    px = list(Image.open(path).convert("RGB").crop(tuple(box)).getdata())
    return tuple(sum(c[i] for c in px) / len(px) for i in range(3))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--box", nargs=4, type=int, required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--min-delta", type=float)
    g.add_argument("--max-delta", type=float)
    a = ap.parse_args(argv)
    ma, mb = mean(a.a, a.box), mean(a.b, a.box)
    d = sum(abs(x - y) for x, y in zip(ma, mb))
    print(f"pixcheck: A={tuple(round(x) for x in ma)} B={tuple(round(x) for x in mb)} delta={d:.1f}")
    ok = d >= a.min_delta if a.min_delta is not None else d <= a.max_delta
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
