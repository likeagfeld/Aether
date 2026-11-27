import struct

from omsat.desccheck import SEG, check, parse


def _desc(segs, names):
    head = struct.pack("<H", len(segs))
    body = b""
    off = 2 + SEG.size * len(segs)
    tail = b""
    rows = []
    for (start, length, fofs, secsize, track, index, q), name in zip(segs, names):
        rows.append(SEG.pack(start, length, fofs, off + len(tail), 0, secsize, track, index, q))
        tail += bytes([len(name)]) + name.encode()
    return head + b"".join(rows) + tail


def test_valid_desc(tmp_path):
    (tmp_path / "t.bin").write_bytes(b"\0" * 2352 * 10)
    d = _desc([(150, 10, 0, 2352, 1, 1, 0x41)], ["t.bin"])
    assert parse(d)[0]["name"] == "t.bin"
    assert check(d, tmp_path) == []


def test_mutations_are_caught(tmp_path):
    (tmp_path / "t.bin").write_bytes(b"\0" * 2352 * 10)
    assert any("FAD" in e for e in check(_desc([(151, 10, 0, 2352, 1, 1, 0x41)], ["t.bin"]), tmp_path))
    assert any("beyond" in e for e in check(_desc([(150, 11, 0, 2352, 1, 1, 0x41)], ["t.bin"]), tmp_path))
    assert any("missing" in e for e in check(_desc([(150, 10, 0, 2352, 1, 1, 0x41)], ["u.bin"]), tmp_path))
    assert any("secsize" in e for e in check(_desc([(150, 10, 0, 2340, 1, 1, 0x41)], ["t.bin"]), tmp_path))
