#!/bin/sh
# Usage (repo root): sh tools/hosttest.sh [make args]   -- menu/host-tests on host GCC 12.2 inside omsat-build.
# UBSan only (see menu/host-tests/Makefile for why ASan is off on this machine).
MSYS_NO_PATHCONV=1 exec docker run --rm -v "$(pwd -W 2>/dev/null || pwd):/work" -w /work omsat-build make -C menu/host-tests clean run "$@"
