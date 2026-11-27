"""Aether Card Manager packaging, GPL-3.0-or-later: the Linux .tar.gz and the macOS .app .zip with real Unix modes.

Windows' tar and Compress-Archive drop the modes, so the binaries would unpack non-executable. Here every directory
is 0755, every ELF or Mach-O file (the apphosts, vmcm, createdump, the .so/.dylib libraries) 0755 and the rest 0644,
owned by root:root (uid/gid 0), entries in sorted order.

  python unixpack.py tar <folder> <out.tar.gz>    the folder itself is the archive's top level (<rid>/...)
  python unixpack.py zip <Name.app> <out.zip>     the .app bundle itself is the archive's top level
  python unixpack.py icns <icon.png> <out.icns>   the .app's icon: PNG-coded 128-1024 px entries (Pillow resizes)
  python unixpack.py list <archive>               mode and path of every entry (to check an archive)
"""
from __future__ import annotations

import os
import stat
import sys
import tarfile
import zipfile
from pathlib import Path

_MAGICS = (b"\x7fELF", b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
           b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def is_native(p: Path) -> bool:
    """An ELF or Mach-O (thin or fat) file: an executable or a native library."""
    with open(p, "rb") as f:
        return f.read(4) in _MAGICS


def mode_for(p: Path) -> int:
    return 0o755 if p.is_dir() or is_native(p) else 0o644


def _walk(top: Path):
    yield top
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames.sort()
        for n in dirnames:
            yield Path(dirpath) / n
        for n in sorted(filenames):
            yield Path(dirpath) / n


def make_tar(top: Path, out: Path) -> None:
    def fix(ti: tarfile.TarInfo) -> tarfile.TarInfo:
        ti.uid = ti.gid = 0
        ti.uname = ti.gname = "root"
        return ti

    with tarfile.open(out, "w:gz", format=tarfile.PAX_FORMAT) as tf:
        for p in _walk(top):
            ti = fix(tf.gettarinfo(str(p), (top.name / p.relative_to(top)).as_posix()))
            ti.mode = mode_for(p)
            if p.is_dir():
                tf.addfile(ti)
            else:
                with open(p, "rb") as f:
                    tf.addfile(ti, f)


def make_zip(top: Path, out: Path) -> None:
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zf:
        for p in _walk(top):
            name = (top.name / p.relative_to(top)).as_posix()
            if p.is_dir():
                zi = zipfile.ZipInfo(name + "/")
                zi.external_attr = ((stat.S_IFDIR | 0o755) << 16) | 0x10    # 0x10: MS-DOS directory bit
                zi.create_system = 3                                          # Unix: unzip/Archive Utility read the mode
                zf.writestr(zi, b"")
            else:
                zi = zipfile.ZipInfo.from_file(p, name)
                zi.external_attr = (stat.S_IFREG | mode_for(p)) << 16
                zi.create_system = 3
                zi.compress_type = zipfile.ZIP_DEFLATED
                with open(p, "rb") as src, zf.open(zi, "w", force_zip64=True) as dst:
                    while chunk := src.read(1 << 20):
                        dst.write(chunk)


# ICNS types that hold PNG data (macOS 10.7+): 128, 256, 512 and 1024 px
_ICNS = (("ic07", 128), ("ic08", 256), ("ic09", 512), ("ic10", 1024))


def make_icns(png: Path, out: Path) -> None:
    import io
    import struct
    from PIL import Image
    src = Image.open(png).convert("RGBA")
    body = b""
    for kind, px in _ICNS:
        buf = io.BytesIO()
        src.resize((px, px), Image.LANCZOS).save(buf, "PNG")
        body += kind.encode() + struct.pack(">I", 8 + len(buf.getvalue())) + buf.getvalue()
    out.write_bytes(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def listing(archive: Path) -> list[tuple[int, str]]:
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as tf:
            return [(m.mode & 0o7777, m.name + ("/" if m.isdir() else "")) for m in tf.getmembers()]
    with zipfile.ZipFile(archive) as zf:
        return [((zi.external_attr >> 16) & 0o7777, zi.filename) for zi in zf.infolist()]


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[0] in ("tar", "zip"):
        top, out = Path(argv[1]).resolve(), Path(argv[2])
        if not top.is_dir():
            print(f"{top} is not a folder", file=sys.stderr)
            return 1
        (make_tar if argv[0] == "tar" else make_zip)(top, out)
        return 0
    if len(argv) == 3 and argv[0] == "icns":
        make_icns(Path(argv[1]), Path(argv[2]))
        return 0
    if len(argv) == 2 and argv[0] == "list":
        for m, n in listing(Path(argv[1])):
            print(f"{m:04o} {n}")
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
