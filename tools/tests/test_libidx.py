import pytest
from omsat import binfmt as bf
from omsat.libidx import Folder, LibEntry, fingerprint, read_libidx, write_libidx


def test_roundtrip():
    folders = [Folder("", 0xFFFF), Folder("RPG", 0)]
    ents = [LibEntry("RPG/Grandia (Disc 1)/game.cue", key="T-4507G", disc=1, disc_total=2, folder_id=1, has_box=True),
            LibEntry("Daytona/daytona.iso", key="GS-9001", image_type=2, title_override="Daytona!")]
    fp = fingerprint(["RPG", "Daytona"])
    data = write_libidx(ents, folders, fp, 20261117)
    h = bf.validate_common(data, bf.MAGIC_LIB)
    assert h.table_size == 32 * 2
    e2, f2, fp2 = read_libidx(data)
    assert e2 == ents and f2 == folders and fp2 == fp


def test_fingerprint_order_independent():
    assert fingerprint(["a", "b"]) == fingerprint(["b", "a"])
    assert fingerprint(["a"]) != fingerprint(["b"])


def test_rejects_long_path():
    with pytest.raises(ValueError, match="255"):
        write_libidx([LibEntry("x" * 256)], [Folder("", 0xFFFF)], 0, 20261117)
