#!/usr/bin/env python3
# ============================================================
# conv_pbaction-all.py
#
# One-shot ROM converter for the Galagino Plus `pbaction` machine.
# The `pbaction` machine IS MAME `pbaction` = "Pinball Action (set 1)"
# (Tehkan, 1985, unencrypted Z80 + Z80 audio, ROT90).
#
# Self-contained (same style as romconv/bnj/bnj_rom_convert.py). Meant to be
# run from the romconv/ directory:
#
#     cd GalaginoPlus-main\romconv
#     python conv_pbaction-all.py
#
# With no arguments it reads GalaginoPlus-main\romszip\pbaction.zip and writes
# the headers into GalaginoPlus-main\source\src\machines\pbaction\ (both paths
# resolved relative to this script, so the working directory does not matter).
# It verifies every size + CRC32 against the MAME `pbaction` set first, so a
# wrong/renamed dump is caught early.
#
# USAGE
#     python conv_pbaction-all.py [ROMSRC] [-o OUTDIR] [--no-verify] [--no-preview]
#
#     ROMSRC   pbaction.zip, OR a folder holding the loose ROM files
#              (default: ../romszip/pbaction.zip relative to this script)
#     -o       output folder for the .h files
#              (default: ../source/src/machines/pbaction relative to this script)
#
# INPUT FILES  (MAME `pbaction` set 1 - `mame pbaction -verifyroms` must pass)
#     b-p7.bin b-n7.bin b-l7.bin        maincpu   0x0000 / 0x4000 / 0x8000
#     a-e3.bin                          audiocpu  0x0000 (0x2000)
#     a-s6.bin a-s7.bin a-s8.bin        fgchars   3 x 0x2000  (charlayout1, 3bpp)
#     a-j5.bin a-j6.bin a-j7.bin a-j8.bin  bgchars 4 x 0x4000 (charlayout2, 4bpp)
#     b-c7.bin b-d7.bin b-f7.bin        sprites   3 x 0x2000  (spritelayout1/2, 3bpp)
#
# OUTPUT HEADERS  (written to OUTDIR)
#     pbaction_main_rom.h    const unsigned char pbaction_main_rom[0x10000]
#     pbaction_audio_rom.h   const unsigned char pbaction_audio_rom[0x2000]
#     pbaction_fg_tiles.h    const unsigned char pbaction_fg_tiles[1024][8][8]   (pen 0-7)
#     pbaction_bg_tiles.h    const unsigned char pbaction_bg_tiles[2048][8][8]   (pen 0-15)
#     pbaction_sprites16.h   const unsigned char pbaction_sprites16[256][16][16] (pen 0-7)
#     pbaction_sprites32.h   const unsigned char pbaction_sprites32[32][32][32]  (pen 0-7)
#
# All gfx are rotated 90 degrees clockwise here (ROT90 cabinet), matching the
# galagino blitter convention used by bnj / bombjack (rot_galagino()).
#
# NOT generated (hand-authored / not ROM-derived):
#     pbaction_dipswitches.h  - DIP config (verify vs `mame pbaction -listxml`)
#     pbaction_logo.h         - custom menu artwork (already present)
#     palette                 - pbaction has NO colour PROM; the palette is
#                               256 x xBGR_444 written by the CPU into palette
#                               RAM at 0xe400-0xe5ff and decoded live by the
#                               port. Nothing to extract.
#
# MAME reference: src/mame/tehkan/pbaction.cpp  (driver by Nicola Salmoria)
#   main_map            0x0000-0xbfff ROM
#   gfx_pbaction        GFXDECODE_START, charlayout1/2, spritelayout1/2
#   get_bg/fg_tile_info tile code math (bg: +0x10*(attr&0x70) -> 2048 codes;
#                                       fg: +0x10*(attr&0x30) -> 1024 codes)
# ============================================================

from __future__ import annotations

import argparse
import sys
import zipfile
import zlib
from pathlib import Path

# --- MAME `pbaction` (set 1) ROM manifest: name -> (size, CRC32) -----------
ROMS = {
    # maincpu
    "b-p7.bin": (0x4000, 0x8D6DCAAE),
    "b-n7.bin": (0x4000, 0xD54D5402),
    "b-l7.bin": (0x2000, 0xE7412D68),
    # audiocpu
    "a-e3.bin": (0x2000, 0x0E53A91F),
    # fgchars (charlayout1, 3bpp)
    "a-s6.bin": (0x2000, 0x9A74A8E1),
    "a-s7.bin": (0x2000, 0x5CA6AD3C),
    "a-s8.bin": (0x2000, 0x9F00B757),
    # bgchars (charlayout2, 4bpp)
    "a-j5.bin": (0x4000, 0x21EFE866),
    "a-j6.bin": (0x4000, 0x7F984C80),
    "a-j7.bin": (0x4000, 0xDF69E51B),
    "a-j8.bin": (0x4000, 0x0094CB8B),
    # sprites (spritelayout1 16x16 + spritelayout2 32x32, 3bpp)
    "b-c7.bin": (0x2000, 0xD1795EF5),
    "b-d7.bin": (0x2000, 0xF28DF203),
    "b-f7.bin": (0x2000, 0xAF6E9817),
}


# --------------------------------------------------------------------------
class RomSource:
    """Reads the pbaction ROM files either from a .zip or a loose directory,
    with size + CRC32 verification against the MAME manifest above."""

    def __init__(self, path: Path, verify: bool):
        self.verify = verify
        if path.is_dir():
            self.zip = None
            self.dir = path
            self.label = str(path)
        elif zipfile.is_zipfile(path):
            self.zip = zipfile.ZipFile(path, "r")
            self.dir = None
            self.label = str(path)
            # map lower-case basename -> real name inside the archive
            self._members = {Path(n).name.lower(): n for n in self.zip.namelist()}
        else:
            sys.exit(f"ERROR: {path} is neither a directory nor a zip archive")

    def _raw(self, name: str) -> bytes:
        if self.zip is not None:
            member = self._members.get(name.lower())
            if member is None:
                sys.exit(f"ERROR: {name} not found in {self.label}")
            return self.zip.read(member)
        p = self.dir / name
        if not p.exists():
            sys.exit(f"ERROR: missing {name} in {self.label}")
        return p.read_bytes()

    def load(self, name: str) -> bytes:
        """Read one ROM, checking size and (optionally) CRC32."""
        size, crc = ROMS[name]
        b = self._raw(name)
        if len(b) != size:
            sys.exit(f"ERROR: {name}: expected {size} bytes, got {len(b)}")
        if self.verify:
            got = zlib.crc32(b) & 0xFFFFFFFF
            if got != crc:
                sys.exit(
                    f"ERROR: {name}: CRC32 0x{got:08X}, expected 0x{crc:08X}. "
                    f"Wrong or bad dump (run `mame pbaction -verifyroms`), or pass "
                    f"--no-verify to override."
                )
        return b


# --- generic MAME planar gfx decoder --------------------------------------
# planes / xoffs / yoffs are ABSOLUTE BIT offsets into `data` (measured from
# `base_bit`), exactly like gfx_element::decode() in src/emu/drawgfx.cpp.
# planes[0] is the most-significant pen bit, planes[-1] the least.
def mame_decode(data: bytes, width: int, height: int, planes: list[int],
                xoffs: list[int], yoffs: list[int], bits_per_tile: int,
                count: int, base_bit: int = 0) -> list[list[list[int]]]:
    tiles = []
    for t in range(count):
        base = base_bit + t * bits_per_tile
        tile = []
        for y in range(height):
            row = []
            for x in range(width):
                v = 0
                for p in planes:
                    off = base + yoffs[y] + xoffs[x] + p
                    bit = (data[off >> 3] >> (7 - (off & 7))) & 1
                    v = (v << 1) | bit
                row.append(v)
            tile.append(row)
        tiles.append(tile)
    return tiles


def rot_galagino(tile: list[list[int]]) -> list[list[int]]:
    """Rotate a square tile 90 degrees clockwise (ROT90 cabinet), matching
    the galagino blitter convention (rot_galagino() in bnj/bombjack)."""
    n = len(tile)
    return [[tile[n - 1 - x][y] for x in range(n)] for y in range(n)]


# --------------------------------------------------------------------------
def build_main_rom(rs: RomSource) -> bytes:
    """MAME ROM_REGION 0x10000 "maincpu":
        b-p7.bin @0x0000 (0x4000), b-n7.bin @0x4000 (0x4000),
        b-l7.bin @0x8000 (0x2000).  0xa000-0xffff = RAM / video / I/O."""
    cpu = bytearray(b"\xFF" * 0x10000)
    cpu[0x0000:0x4000] = rs.load("b-p7.bin")
    cpu[0x4000:0x8000] = rs.load("b-n7.bin")
    cpu[0x8000:0xA000] = rs.load("b-l7.bin")
    return bytes(cpu)


def build_audio_rom(rs: RomSource) -> bytes:
    """MAME ROM_REGION 0x10000 "audiocpu": a-e3.bin @0x0000 (0x2000).
    The audio map only uses 0x0000-0x1fff as ROM, so 0x2000 bytes suffice."""
    return rs.load("a-e3.bin")


def build_fg_tiles(rs: RomSource) -> list[list[list[int]]]:
    """charlayout1 (fgchars), GFXDECODE gfx[0]:
        8,8  RGN_FRAC(1,3)  3 planes
        planeoffset { RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) }
        xoffs STEP8(0,1)   yoffs STEP8(0,8)   charincrement 8*8

    region = a-s6 + a-s7 + a-s8 (0x6000). planeoffset[0]=RGN_FRAC(0,3) is the
    MSB and points at a-s6; planeoffset[2]=RGN_FRAC(2,3) is the LSB -> a-s8.
    count = (0x6000*8) / 3 / (8*8) = 1024 tiles (matches the 0x400 fg codes
    the tile-info handler can address: videoram + 0x10*(attr&0x30))."""
    region = b"".join(rs.load(n) for n in ("a-s6.bin", "a-s7.bin", "a-s8.bin"))
    rf = len(region) * 8 // 3               # RGN_FRAC(1,3) in bits
    planes = [0 * rf, 1 * rf, 2 * rf]       # MSB .. LSB
    xoffs = [i for i in range(8)]
    yoffs = [y * 8 for y in range(8)]
    bpt = 8 * 8
    count = rf // bpt
    assert count == 1024, count
    return mame_decode(region, 8, 8, planes, xoffs, yoffs, bpt, count)


def build_bg_tiles(rs: RomSource) -> list[list[list[int]]]:
    """charlayout2 (bgchars), GFXDECODE gfx[1]:
        8,8  RGN_FRAC(1,4)  4 planes
        planeoffset { RGN_FRAC(0,4), RGN_FRAC(1,4), RGN_FRAC(2,4), RGN_FRAC(3,4) }
        xoffs STEP8(0,1)   yoffs STEP8(0,8)   charincrement 8*8

    region = a-j5 + a-j6 + a-j7 + a-j8 (0x10000). planeoffset[0]=RGN_FRAC(0,4)
    is the MSB -> a-j5; planeoffset[3]=RGN_FRAC(3,4) is the LSB -> a-j8.
    count = (0x10000*8) / 4 / (8*8) = 2048 tiles (matches the 0x800 bg codes:
    videoram + 0x10*(attr&0x70))."""
    region = b"".join(rs.load(n)
                      for n in ("a-j5.bin", "a-j6.bin", "a-j7.bin", "a-j8.bin"))
    rf = len(region) * 8 // 4
    planes = [0 * rf, 1 * rf, 2 * rf, 3 * rf]   # MSB .. LSB
    xoffs = [i for i in range(8)]
    yoffs = [y * 8 for y in range(8)]
    bpt = 8 * 8
    count = rf // bpt
    assert count == 2048, count
    return mame_decode(region, 8, 8, planes, xoffs, yoffs, bpt, count)


def build_sprites16(rs: RomSource) -> list[list[list[int]]]:
    """spritelayout1, GFXDECODE gfx[2] (offset 0x00000 into "sprites"):
        16,16  RGN_FRAC(1,3)  3 planes
        planeoffset { RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) }
        xoffs { STEP8(0,1), STEP8(64,1) }
        yoffs { STEP8(0,8), STEP8(128,8) }
        charincrement 32*8

    region = b-c7 + b-d7 + b-f7 (0x6000). planeoffset[0]=MSB -> b-c7,
    planeoffset[2]=LSB -> b-f7. count = (0x6000*8)/3/(32*8) = 256."""
    region = b"".join(rs.load(n) for n in ("b-c7.bin", "b-d7.bin", "b-f7.bin"))
    rf = len(region) * 8 // 3
    planes = [0 * rf, 1 * rf, 2 * rf]
    xoffs = [i for i in range(8)] + [64 + i for i in range(8)]
    yoffs = [8 * i for i in range(8)] + [128 + 8 * i for i in range(8)]
    bpt = 32 * 8
    count = rf // bpt
    assert count == 256, count
    return mame_decode(region, 16, 16, planes, xoffs, yoffs, bpt, count)


def build_sprites32(rs: RomSource) -> list[list[list[int]]]:
    """spritelayout2, GFXDECODE gfx[3] (offset 0x01000 into "sprites"):
        32,32  RGN_FRAC(1,6)  3 planes
        planeoffset { RGN_FRAC(0,3), RGN_FRAC(1,3), RGN_FRAC(2,3) }
        xoffs { STEP8(0,1), STEP8(64,1), STEP8(256,1), STEP8(320,1) }
        yoffs { STEP8(0,8), STEP8(128,8), STEP8(512,8), STEP8(640,8) }
        charincrement 128*8

    NOTE the plane FRACTIONS here are thirds of the WHOLE 0x6000 region
    (RGN_FRAC(n,3)), while the tile count is RGN_FRAC(1,6) and the GFXDECODE
    entry starts 0x1000 BYTES into the region. So:
        region  = b-c7 + b-d7 + b-f7  (0x6000)
        rf3     = 0x6000*8/3          (plane stride, bits)
        base    = 0x1000*8            (start offset, bits)
        planes  = [0*rf3, 1*rf3, 2*rf3]   MSB..LSB  (b-c7 / b-d7 / b-f7)
        count   = RGN_FRAC(1,6) / (128*8) = (0x6000*8/6) / 1024 = 32
    """
    region = b"".join(rs.load(n) for n in ("b-c7.bin", "b-d7.bin", "b-f7.bin"))
    rf3 = len(region) * 8 // 3
    base = 0x1000 * 8
    planes = [0 * rf3, 1 * rf3, 2 * rf3]
    xoffs = ([i for i in range(8)] + [64 + i for i in range(8)]
             + [256 + i for i in range(8)] + [320 + i for i in range(8)])
    yoffs = ([8 * i for i in range(8)] + [128 + 8 * i for i in range(8)]
             + [512 + 8 * i for i in range(8)] + [640 + 8 * i for i in range(8)])
    bpt = 128 * 8
    count = (len(region) * 8 // 6) // bpt
    assert count == 32, count
    return mame_decode(region, 32, 32, planes, xoffs, yoffs, bpt, count, base_bit=base)


# --- header writers ------------------------------------------------------
BANNER = "// Generated by conv_pbaction-all.py from the MAME `pbaction` (set 1) ROM set. Do not edit.\n"


def write_bytes_header(path: Path, guard: str, sym: str, data: bytes, note: str) -> None:
    with path.open("w") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(BANNER)
        f.write(f"// {note}\n\n")
        f.write(f"const unsigned char {sym}[{len(data)}] = {{\n")
        for i in range(0, len(data), 16):
            f.write("  " + ",".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",\n")
        f.write("};\n\n#endif\n")
    print(f"  wrote {path.name:24s} {len(data):#8x} bytes")


def write_tiles_header(path: Path, guard: str, sym: str, tiles: list, dim: int, note: str) -> None:
    with path.open("w") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(BANNER)
        f.write(f"// {note}\n")
        f.write(f"// {len(tiles)} tiles, {dim}x{dim} pixels, rotated 90 deg CW (ROT90).\n\n")
        f.write(f"const unsigned char {sym}[{len(tiles)}][{dim}][{dim}] = {{\n")
        for i, t in enumerate(tiles):
            rows = ["{" + ",".join(str(v) for v in t[y]) + "}" for y in range(dim)]
            f.write("  {" + ",".join(rows) + "}")
            f.write(",\n" if i < len(tiles) - 1 else "\n")
        f.write("};\n\n#endif\n")
    print(f"  wrote {path.name:24s} {len(tiles)} tiles ({dim}x{dim})")


# --- optional PNG preview ----------------------------------------------
def preview(fg, bg, s16, s32, outpng: Path) -> None:
    try:
        from PIL import Image
    except ImportError:
        print("  (PIL not installed - skipping preview)")
        return

    PAL = [(20, 20, 20), (200, 60, 60), (60, 200, 60), (60, 60, 200),
           (200, 200, 60), (200, 60, 200), (60, 200, 200), (230, 230, 230),
           (120, 40, 40), (40, 120, 40), (40, 40, 120), (120, 120, 40),
           (120, 40, 120), (40, 120, 120), (150, 150, 150), (255, 255, 255)]

    def block(tiles, dim, cols):
        rows = (len(tiles) + cols - 1) // cols
        img = Image.new("RGB", (cols * (dim + 1), rows * (dim + 1)), (32, 32, 96))
        px = img.load()
        for i, t in enumerate(tiles):
            ox, oy = (i % cols) * (dim + 1), (i // cols) * (dim + 1)
            for y in range(dim):
                for x in range(dim):
                    px[ox + x, oy + y] = PAL[t[y][x] & 15]
        return img

    parts = [block(fg, 8, 32), block(bg, 8, 32),
             block(s16, 16, 32), block(s32, 32, 16)]
    W = max(p.width for p in parts)
    H = sum(p.height + 6 for p in parts)
    img = Image.new("RGB", (W, H), (0, 0, 0))
    y = 0
    for p in parts:
        img.paste(p, (0, y))
        y += p.height + 6
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(outpng)
    print(f"  wrote {outpng.name} (preview)")


# --------------------------------------------------------------------------
def main() -> None:
    ap = argparse.ArgumentParser(description="Convert the MAME `pbaction` (set 1) ROM set to pbaction headers.")
    ap.add_argument("romsrc", nargs="?", default=None,
                    help="pbaction.zip or a folder of loose ROM files "
                         "(default: ../romszip/pbaction.zip relative to this script)")
    ap.add_argument("-o", "--outdir", default=None,
                    help="output folder for the .h files "
                         "(default: ../source/src/machines/pbaction relative to this script)")
    ap.add_argument("--no-verify", action="store_true", help="skip CRC32 checks")
    ap.add_argument("--no-preview", action="store_true", help="do not write the PNG preview")
    args = ap.parse_args()

    # This copy lives in GalaginoPlus-main/romconv/pbaction/.
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    romsrc = (Path(args.romsrc).resolve() if args.romsrc
              else (root / "romszip" / "pbaction.zip").resolve())
    outdir = (Path(args.outdir).resolve() if args.outdir
              else (root / "source" / "src" / "machines" / "pbaction").resolve())

    if not romsrc.exists():
        sys.exit(f"ERROR: ROM source {romsrc} does not exist")
    outdir.mkdir(parents=True, exist_ok=True)

    verify = not args.no_verify
    print(f"ROMs : {romsrc}")
    print(f"out  : {outdir}")
    print(f"CRC verify: {'on' if verify else 'OFF'}\n")

    rs = RomSource(romsrc, verify)

    write_bytes_header(outdir / "pbaction_main_rom.h", "PBACTION_MAIN_ROM_H",
                       "pbaction_main_rom", build_main_rom(rs),
                       "Z80 main CPU space: b-p7 @0x0000, b-n7 @0x4000, b-l7 @0x8000; "
                       "0xa000+ is RAM/video/I/O (filled 0xFF).")
    write_bytes_header(outdir / "pbaction_audio_rom.h", "PBACTION_AUDIO_ROM_H",
                       "pbaction_audio_rom", build_audio_rom(rs),
                       "Z80 audio CPU ROM a-e3.bin (0x0000-0x1fff).")

    fg = [rot_galagino(t) for t in build_fg_tiles(rs)]
    bg = [rot_galagino(t) for t in build_bg_tiles(rs)]
    s16 = [rot_galagino(t) for t in build_sprites16(rs)]
    s32 = [rot_galagino(t) for t in build_sprites32(rs)]

    write_tiles_header(outdir / "pbaction_fg_tiles.h", "PBACTION_FG_TILES_H",
                       "pbaction_fg_tiles", fg, 8,
                       "Foreground chars (a-s6/s7/s8), charlayout1, 3bpp (pen 0-7). "
                       "gfx[0]: color = attr&0x0f, transparent pen 0.")
    write_tiles_header(outdir / "pbaction_bg_tiles.h", "PBACTION_BG_TILES_H",
                       "pbaction_bg_tiles", bg, 8,
                       "Background chars (a-j5/j6/j7/j8), charlayout2, 4bpp (pen 0-15). "
                       "gfx[1]: color = attr&0x07, opaque, palette base 128.")
    write_tiles_header(outdir / "pbaction_sprites16.h", "PBACTION_SPRITES16_H",
                       "pbaction_sprites16", s16, 16,
                       "Normal sprites (b-c7/d7/f7), spritelayout1, 3bpp (pen 0-7). "
                       "gfx[2]: color = spriteram[offs+1]&0x0f, transparent pen 0.")
    write_tiles_header(outdir / "pbaction_sprites32.h", "PBACTION_SPRITES32_H",
                       "pbaction_sprites32", s32, 32,
                       "Large sprites (same ROMs, +0x1000), spritelayout2, 3bpp (pen 0-7). "
                       "gfx[3]: color = spriteram[offs+1]&0x0f, transparent pen 0.")

    if not args.no_preview:
        preview(fg, bg, s16, s32, here / "pbaction_gfx_preview.png")

    print("\ndone.")


if __name__ == "__main__":
    main()
