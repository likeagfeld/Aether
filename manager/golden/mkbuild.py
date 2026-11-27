"""manager/golden/mkbuild.py -- the card-build golden (M4 Task 9): tools/omsat/cardbuild.py on a generated fixture card.
Aether Card Manager, GPL-3.0-or-later. Uses tools/omsat read-only; needs no network and no game data.

    python manager/golden/mkbuild.py   -> manager/golden/build_case/<files>, manager/golden/expect/build.json

The case (manager/golden/build_case/, committed, about 150 KB):
  card.json      a realize.py tree of the card: images that are only a System ID (an .iso holding it, or a .cue naming a
                 ".sid" file holding it -- no game data), BOX.TGA covers in every SPEC §5.6 variant plus the RLE ones
                 Pillow also reads, a FOLDER.PNG;
  GAMES.DB       written here by omsat.gamesdb for the fixture's keys (with real compat rows from tools/data);
  jp_titles.json two JP titles;
  covers.zip     a miniature Cover Project zip (PNG and JPEG members under its real top folder);
  art_cache/     omsat.artsrc RemoteArt's cache for the retrohead and libretro lookups the card makes (hits and misses),
                 so the build runs offline (download=False);
  folder_misc.png  --folder-art for "Misc".
The expect is cardbuild's output: LIBRARY.IDX, GAMES.DB, ART.PAK and manifest.csv as SHA-256 (and ART.PAK's entries
and blob hashes, so a JP-title difference can be told from any other), and the report's deterministic fields."""
from __future__ import annotations

import hashlib
import io
import json
import shutil
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from PIL import Image  # noqa: E402

from omsat import artpak, artsrc, cardbuild, gamesdb, iphdr  # noqa: E402

import realize  # noqa: E402

CASE = REPO / "manager/golden/build_case"
DATE = 20261122
ART_PREFER = {"GS-9001": "boxtga"}


def grad(w, h, a=255, seed=0):
    img = Image.new("RGBA", (w, h))
    img.putdata([((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), (seed * 37) & 255,
                  a if (x + y) % 7 else (a // 2)) for y in range(h) for x in range(w)])
    return img


def tga(img: Image.Image, kind: str) -> bytes:
    """A TGA in one variant: t2_24 / t2_32 / t2_16 / t1_8 (24-bit palette) / t10_24 (RLE) / t9_8 (RLE, palette);
    "_tl" in kind = top-left origin (flags 0x20), else bottom-left; "_id" = a 5-byte image ID."""
    w, h = img.size
    tl, idf = "_tl" in kind, b"VIBE!" if "_id" in kind else b""
    rows = list(range(h)) if tl else list(range(h - 1, -1, -1))
    px = img.convert("RGBA").load()
    base = kind.split("_")[0] + "_" + kind.split("_")[1]
    cmap, depth, itype, pal = b"", 0, 0, []
    if base in ("t1_8", "t9_8"):
        q = img.convert("RGB").quantize(64)
        pal = q.getpalette()[:64 * 3]
        qp = q.load()
        cmap = b"".join(bytes([pal[i * 3 + 2], pal[i * 3 + 1], pal[i * 3]]) for i in range(64))
        data = [bytes([qp[x, y]]) for y in rows for x in range(w)]
        depth, itype = 8, 1 if base == "t1_8" else 9
    else:
        bpp = int(base.split("_")[1])
        def pix(x, y):
            r, g, b, a = px[x, y]
            if bpp == 24:
                return bytes([b, g, r])
            if bpp == 32:
                return bytes([b, g, r, a])
            v = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3) | (0x8000 if (x * 3 + y) % 5 == 0 else 0)
            return struct.pack("<H", v)
        data = [pix(x, y) for y in rows for x in range(w)]
        depth, itype = bpp, 2 if base.startswith("t2") else 10
    if itype in (9, 10):                                   # RLE, packets never crossing a row
        out = bytearray()
        for r0 in range(0, len(data), w):
            row = data[r0:r0 + w]
            i = 0
            while i < len(row):
                j = i
                while j + 1 < len(row) and row[j + 1] == row[i] and j - i < 127:
                    j += 1
                if j > i:
                    out += bytes([0x80 | (j - i)]) + row[i]
                    i = j + 1
                else:
                    k = i
                    while k + 1 < len(row) and row[k + 1] != row[k] and k - i < 127:
                        k += 1
                    out += bytes([k - i]) + b"".join(row[i:k + 1])
                    i = k + 1
        body = bytes(out)
    else:
        body = b"".join(data)
    hdr = struct.pack("<BBBHHBHHHHBB", len(idf), 1 if cmap else 0, itype, 0, len(cmap) // 3 if cmap else 0,
                      24 if cmap else 0, 0, 0, w, h, depth, (0x20 if tl else 0) | (8 if depth == 32 else 0))
    return hdr + idf + cmap + body


def sid(product, area="U", device="CD-1/1", title="T", date="19960101"):
    return iphdr.make_system_id(product=product, area=area, device=device, title=title, release_date=date)


def iso(path, **k):
    return {"path": path, "raw": sid(**k).hex()}


def cue(path, **k):
    name = path.rsplit("/", 1)[1]
    return [{"path": path, "raw": f'FILE "{name}.sid" BINARY\n  TRACK 01 MODE1/2048\n'.encode().hex()},
            {"path": path + ".sid", "raw": sid(**k).hex()}]


def png(img) -> bytes:
    b = io.BytesIO()
    img.save(b, "PNG")
    return b.getvalue()


def jpg(img) -> bytes:
    b = io.BytesIO()
    img.convert("RGB").save(b, "JPEG", quality=85)
    return b.getvalue()


RECS = [("GS-9001", "Daytona USA", "JTUE"), ("T-8113H", "Alien Trilogy", "U"), ("T-4507G", "Grandia", "J"),
        ("GS-7103", "Virtua Fighter 2", "JTUE"), ("T-1001A", "Palette Box", "U"), ("T-1002A", "Rle Box", "U"),
        ("T-1003A", "Hicolor Box", "E"), ("T-1004A", "Sample Game", "U"), ("T-1005A", "No Cover Anywhere", "U"),
        ("MK-81070", "Saturn Bomberman", "U"), ("T-1006A", "Kanji Only", "J"), ("MK-81045", "Golden Axe The Duel", "U"),
        ("SGC", "Save Game Copier", "JTUE"), ("T-1007A", "Top Left Box", "U"), ("T-1008A", "Rle Palette", "J")]


def card_files() -> list[dict]:
    f = []
    f += cue("card/Racing/Daytona USA (USA)/Daytona USA (USA).cue", product="GS-9001", area="JTUE", title="DAYTONA USA")
    f.append({"path": "card/Racing/Daytona USA (USA)/BOX.TGA", "raw": tga(grad(32, 50, seed=1), "t2_24").hex()})
    f += cue("card/Shooters/Alien/alien.cue", product="T-8113H", title="ALIEN TRILOGY", date="19960215")
    f.append({"path": "card/Shooters/Alien/BOX.TGA", "raw": tga(grad(32, 50, a=200, seed=2), "t2_32_tl_id").hex()})
    f += cue("card/RPG/Grandia/Grandia (Japan) (Disc 1).cue", product="T-4507G", area="J", device="CD-1/2", title="GRANDIA")
    f += cue("card/RPG/Grandia/Grandia (Japan) (Disc 2).cue", product="T-4507G", area="J", device="CD-2/2", title="GRANDIA")
    f.append(iso("card/Misc/vf2.iso", product="GS-7103", area="JTUE", title="VIRTUA FIGHTER 2"))
    for key, kind, folder in (("T-1001A", "t1_8", "Palette"), ("T-1002A", "t10_24_tl", "Rle"), ("T-1003A", "t2_16", "Hicolor"),
                              ("T-1007A", "t2_24_tl", "TopLeft"), ("T-1008A", "t9_8", "RlePal")):
        f.append(iso(f"card/Boxes/{folder}/game.iso", product=key, title=folder.upper()))
        f.append({"path": f"card/Boxes/{folder}/BOX.TGA", "raw": tga(grad(40 if "8" in kind else 32, 40 if "8" in kind else 50, seed=len(folder)), kind).hex()})
    f.append(iso("card/Misc/sample.iso", product="T-1004A", title="SAMPLE GAME"))
    f.append(iso("card/Misc/nocover.iso", product="T-1005A", title="NO COVER"))
    f.append(iso("card/Misc/bomber.iso", product="MK-81070", title="BOMBERMAN"))
    f.append(iso("card/Misc/kanji.iso", product="T-1006A", area="J", title="KANJI"))
    f.append(iso("card/Misc/goldenaxe.iso", product="MK-81045", title="GOLDEN AXE"))
    f.append(iso("card/Misc/sgc.iso", product="SGC", area="JTUE", title="JO ENGINE"))
    f.append(iso("card/Misc/unknown.iso", product="X-99999", title="UNKNOWN"))
    f.append({"path": "card/RPG/FOLDER.PNG", "raw": png(grad(50, 50, seed=9)).hex()})
    return f


def build_case() -> None:
    if CASE.exists():
        shutil.rmtree(CASE)
    CASE.mkdir(parents=True)
    (CASE / "card.json").write_text(json.dumps({"files": card_files()}, indent=0, sort_keys=True) + "\n", newline="\n")
    recs = [gamesdb.GameRecord(k, t, region_flags=gamesdb.region_flags_from(a), players=1) for k, t, a in RECS]
    (CASE / "GAMES.DB").write_bytes(gamesdb.write_gamesdb(recs, ["Action", "RPG"], {"610-6342": "GS-7103"}, 20261101))
    (CASE / "jp_titles.json").write_text(json.dumps({"T-4507G": "グランディア", "T-1006A": "漢字だけのゲーム"}, ensure_ascii=False,
                                                    indent=1) + "\n", encoding="utf-8", newline="\n")
    with zipfile.ZipFile(CASE / "covers.zip", "w", zipfile.ZIP_STORED) as z:
        top = "SEGA SATURN RINGS Cover Project/"
        z.writestr(top + "US/Daytona USA (USA).png", png(grad(130, 200, seed=11)))
        z.writestr(top + "JAPAN/Grandia (Japan) (Disc 1).png", png(grad(170, 170, seed=12)))
        z.writestr(top + "JAPAN/Grandia (Japan) (Disc 2).jpg", jpg(grad(170, 170, seed=13)))
        z.writestr(top + "US/Sample Game (USA) [patched].jpg", jpg(grad(64, 64, seed=14)))   # never by image name
        z.writestr(top + "README.txt", b"not a cover")
    cache = CASE / "art_cache"
    cache.mkdir()
    def put(url, data):
        h = hashlib.sha256(url.encode("utf-8")).hexdigest()[:24]
        (cache / (h + (".bin" if data is not None else ".miss"))).write_bytes(data or b"")
        (cache / (h + ".url")).write_text(url + "\n", encoding="utf-8")
    import urllib.parse
    put(artsrc.RETROHEAD + urllib.parse.quote("GS-7103.TGA"), tga(grad(80, 80, seed=21), "t2_24"))
    put(artsrc.RETROHEAD + urllib.parse.quote("MK-81070.TGA"), tga(grad(64, 100, seed=22), "t2_24"))   # REJECTED by artsrc
    put(artsrc.LIBRETRO + urllib.parse.quote("Saturn Bomberman (USA).png"), png(grad(120, 200, seed=23)))
    for key in ("T-1004A", "T-1005A", "T-1006A", "MK-81045", "SGC"):
        for name in artsrc.retrohead_names(key):
            put(artsrc.RETROHEAD + urllib.parse.quote(name + ".TGA"), None)
    put(artsrc.LIBRETRO + urllib.parse.quote("Sample Game (USA).png"), None)
    put(artsrc.LIBRETRO + urllib.parse.quote("No Cover Anywhere (USA).png"), None)
    put(artsrc.LIBRETRO + urllib.parse.quote("Kanji Only (Japan).png"), None)
    put(artsrc.LIBRETRO + urllib.parse.quote("Golden Axe The Duel (USA).png"), png(grad(130, 190, seed=24)))
    for region in ("USA", "Japan", "Europe", "Taiwan"):
        put(artsrc.LIBRETRO + urllib.parse.quote(f"Save Game Copier ({region}).png"), None)
    (CASE / "folder_misc.png").write_bytes(png(grad(160, 120, seed=31)))


def run() -> dict:
    import os
    os.chdir(CASE)                                  # --folder-art is given relative, so the manifest row is portable
    with tempfile.TemporaryDirectory() as t:
        t = Path(t)
        realize.realize(json.loads((CASE / "card.json").read_text()), t / "x")
        card, out = t / "x/card", t / "out"
        remote = artsrc.RemoteArt(CASE / "art_cache", download=False)
        rep = cardbuild.build(card, (CASE / "GAMES.DB").read_bytes(),
                              json.loads((CASE / "jp_titles.json").read_text(encoding="utf-8")), CASE / "covers.zip",
                              None, False, out, DATE, remote=remote, folder_art={"Misc": Path("folder_misc.png")},
                              art_prefer=ART_PREFER)
        files = {n: hashlib.sha256((out / n).read_bytes()).hexdigest() for n in ("LIBRARY.IDX", "GAMES.DB", "ART.PAK", "manifest.csv")}
        pak = (out / "ART.PAK").read_bytes()
        ents = []
        for e in artpak.read_artpak(pak):
            row = {k: v for k, v in e.items() if not k.startswith("_")}
            for kind, flag in (("thumb", 1), ("hero", 2), ("jp", 4)):
                row[kind] = hashlib.sha256(artpak.get_blob(pak, e, kind)).hexdigest() if e["flags"] & flag else ""
                if kind == "jp" and e["flags"] & flag:
                    row["jp_data"] = artpak.get_blob(pak, e, kind).hex()
            ents.append(row)
        keep = {k: rep[k] for k in ("years_from_ipbin", "compat_applied", "covers", "folder_art", "art_entries", "check_errors",
                                     "per_title", "compat_notes") if k in rep}
        keep["jp"] = {k: v for k, v in rep["jp"].items() if k != "font"}
        keep["scan"] = rep["scan"]
        keep["remote"] = {"rejected": rep["remote"]["rejected"], "downloads": rep["remote"]["downloads"]}
        return {"source": "omsat.cardbuild.build (mkbuild.py)", "date": DATE, "art_prefer": ART_PREFER, "files": files,
                "manifest_csv": (out / "manifest.csv").read_bytes().decode("utf-8"), "art_entries": ents, "report": keep}  # CRLF kept


def main(argv) -> int:
    if "--keep-case" not in argv:
        build_case()
    exp = run()
    (REPO / "manager/golden/expect/build.json").write_text(json.dumps(exp, indent=1, sort_keys=True, ensure_ascii=False) + "\n",
                                                           encoding="utf-8", newline="\n")
    print(json.dumps(exp["report"]["covers"]), exp["report"]["jp"], exp["report"]["art_entries"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
