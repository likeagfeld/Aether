"""Folders-hang gate (.superpowers/sdd/2026-11-18-m3-library-themes-music/debug-folders-vdp1-hang.md):
sbuild.check_sgl_tables, RED on the measured misaligned om_tex addresses and GREEN on an aligned one."""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sbuild  # noqa: E402


@pytest.mark.parametrize("addr", [0x060190DE, 0x0601988E])   # measured: --app-fake and --app at 9638123
def test_misaligned_texture_table_is_red(addr):
    with pytest.raises(SystemExit, match="not 4-byte aligned"):
        sbuild.check_sgl_tables({"_om_tex": addr, "_om_v1_cmd": 0x06019100})


def test_misaligned_v1_command_table_is_red():
    with pytest.raises(SystemExit, match="_om_v1_cmd.*not 4-byte aligned"):
        sbuild.check_sgl_tables({"_om_tex": 0x060190E0, "_om_v1_cmd": 0x06019102})


def test_aligned_texture_table_is_green():
    sbuild.check_sgl_tables({"_om_tex": 0x060190E0, "_om_v1_cmd": 0x06019100})


def test_missing_texture_table_is_red():
    with pytest.raises(SystemExit, match="missing"):
        sbuild.check_sgl_tables({})
