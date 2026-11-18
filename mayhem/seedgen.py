#!/usr/bin/env python3
"""Regenerates the starter seeds under mayhem/<target>/testsuite/ (also the oracle's fixtures).

The seeds are committed; this script is only kept so they can be reproduced:
    python3 mayhem/seedgen.py

The `fz-<sha1>` files next to them are not generated here: they are a coverage-grown corpus (libFuzzer
fork mode from these seeds, then `-merge=1`), keeping only inputs that run clean one by one and all in one
process. The loaders overflow on most length/count bytes, so with two seeds Mayhem's `-runs=5` startup
probe spent its three mutations on crashes; with a few dozen clean inputs the probe never reaches a mutation.
"""
import os
import struct

ROOT = os.path.dirname(os.path.abspath(__file__))


def put(target, name, data):
    d = os.path.join(ROOT, target, "testsuite")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())


def pstr(s):
    b = s.encode()
    return bytes([len(b)]) + b


def le32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def be32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


# ---------------------------------------------------------------- GIF
def lzw(pixels, min_code):
    clear = 1 << min_code
    eof = clear + 1
    out_bits = []

    def emit(code, width):
        for i in range(width):
            out_bits.append((code >> i) & 1)

    table = {bytes([i]): i for i in range(clear)}
    nxt = eof + 1
    width = min_code + 1
    emit(clear, width)
    cur = b""
    for p in pixels:
        c = cur + bytes([p])
        if c in table:
            cur = c
            continue
        emit(table[cur], width)
        table[c] = nxt
        nxt += 1
        if nxt > (1 << width) and width < 12:
            width += 1
        if nxt >= 4095:
            emit(clear, width)
            table = {bytes([i]): i for i in range(clear)}
            nxt = eof + 1
            width = min_code + 1
        cur = bytes([p])
    emit(table[cur], width)
    emit(eof, width)
    while len(out_bits) % 8:
        out_bits.append(0)
    data = bytearray()
    for i in range(0, len(out_bits), 8):
        data.append(sum(b << j for j, b in enumerate(out_bits[i:i + 8])))
    return bytes(data)


def gif(w, h, pixels, colors=4, interlaced=False):
    bits = {4: 1, 256: 7}[colors]
    hdr = b"GIF89a" + struct.pack("<HH", w, h) + bytes([0x80 | (bits << 4) | bits, 0, 0])
    pal = b"".join(bytes([i * 5 % 256, i * 11 % 256, i * 17 % 256]) for i in range(colors))
    desc = b"," + struct.pack("<HHHH", 0, 0, w, h) + bytes([0x40 if interlaced else 0])
    mc = 2 if colors == 4 else 8
    data = lzw(pixels, mc)
    blocks = b""
    for i in range(0, len(data), 255):
        chunk = data[i:i + 255]
        blocks += bytes([len(chunk)]) + chunk
    return hdr + pal + desc + bytes([mc]) + blocks + b"\x00" + b";"


GIF_PIX = [(x + y) % 4 for y in range(8) for x in range(8)]
put("rsdk_gif", "sheet8x8.gif", gif(8, 8, GIF_PIX))
put("rsdk_gif", "sheet16x16.gif", gif(16, 16, [((x >> 2) + (y >> 2)) % 4 for y in range(16) for x in range(16)]))
put("rsdk_gif", "sheet8x8_interlaced.gif", gif(8, 8, GIF_PIX, interlaced=True))

# ---------------------------------------------------------------- GFX / BMP (rsdk_sheet: byte 0 selects the loader)
gfx = struct.pack(">HH", 8, 4) + bytes(255 * 3)
gfx += bytes([1, 2, 3, 4, 5, 6, 7, 8]) + bytes([0xFF, 9, 8]) + bytes([0xFF, 10, 16]) + bytes([0xFF, 0xFF])
put("rsdk_sheet", "sheet.gfx", b"\x00" + gfx)
bmp = bytearray(b"BM" + bytes(52))
bmp[18:22] = le32(8)
bmp[22:26] = le32(4)
bmp += bytes(1024)
bmp += bytes((r * 8 + c + 1) for r in range(4) for c in range(8))
put("rsdk_sheet", "sheet.bmp", b"\x01" + bytes(bmp))

# ---------------------------------------------------------------- animation (.ani)
ani = bytes([1]) + pstr("Player.gif")
ani += bytes([2])
ani += pstr("Idle") + bytes([2, 4, 0, 1])
ani += bytes([0, 0, 0, 0, 16, 24, 0xF8, 0xF4])
ani += bytes([0, 1, 16, 0, 16, 24, 0xF8, 0xF4])
ani += pstr("Run") + bytes([1, 6, 0, 0])
ani += bytes([0, 1, 32, 0, 18, 24, 0xF7, 0xF4])
ani += bytes([2])
hb = [0xEC, 0xE8, 0x14, 0x18]
ani += bytes(hb * 8)
ani += bytes([0xF0, 0xF0, 0x10, 0x10] * 8)
put("rsdk_anim", "player.ani", ani)
put("rsdk_anim", "minimal.ani", bytes([0, 0, 0]))

# ---------------------------------------------------------------- scripts
SCRIPT1 = """//-----------------Test Object-----------------//
#alias Object.Value0:Counter
#alias Object.Value1:Dir

sub ObjectMain
	Counter++
	if Counter>5
		Counter=0
		Object.XPos+=65536
	endif
endsub

sub ObjectPlayerInteraction
	Dir=1
endsub

sub ObjectDraw
	DrawSprite(0)
endsub

sub ObjectStartup
	Counter=0
endsub
"""
SCRIPT2 = """//-----------------Switch Object-----------------//
#alias Object.Value0:State

function TestFunc
	State=3
endfunction

sub ObjectMain
	switch State
	case 0
		State=1
		break
	case 1
		State=2
		break
	default
		State=0
		break
	endswitch
	while State<4
		State++
	loop
	CallFunction(TestFunc)
endsub
"""
put("rsdk_script", "object1.txt", SCRIPT1)
put("rsdk_script", "object2.txt", SCRIPT2)
put("rsdk_script", "aliases.txt", "#alias Object.Value0:A\n#alias Object.Value1:B\n#alias Object.Value2:C\nsub ObjectMain\n\tA=B\n\tC=A\nendsub\n")

# ---------------------------------------------------------------- bytecode (GlobalCode.bin, mobile)
def words(ws, packed=True):
    out = b""
    for i in range(0, len(ws), 0x7F):
        chunk = ws[i:i + 0x7F]
        if packed:
            out += bytes([0x80 | len(chunk)]) + b"".join(le32(w) for w in chunk)
        else:
            out += bytes([len(chunk)]) + bytes(w & 0xFF for w in chunk)
    return out


def bytecode(code, jumps, packed=True):
    b = le32(len(code)) + words(code, packed)
    b += le32(len(jumps)) + words(jumps, packed)
    b += struct.pack("<H", 1)
    b += le32(0) + le32(3) + le32(5) + le32(7)
    b += le32(0) + le32(1) + le32(2) + le32(3)
    b += struct.pack("<H", 1)
    b += le32(2) + le32(1)
    return b


put("rsdk_bytecode", "global_packed.bin", bytecode([1, 5, 9, 12, 3, 7, 0, 0x1000, 2], [0, 4, 8], True))
put("rsdk_bytecode", "global_bytes.bin", bytecode([1, 5, 9, 12, 3, 7, 2], [0, 4, 8], False))

# ---------------------------------------------------------------- GameConfig.bin
def gameconfig(name, vars_, stages):
    b = pstr(name) + pstr("Data") + pstr("A test game")
    b += bytes([2]) + pstr("Player") + pstr("Ring")
    b += pstr("Players/Player.txt") + pstr("Global/Ring.txt")
    b += bytes([len(vars_)])
    for k, v in vars_:
        b += pstr(k) + be32(v)
    b += bytes([1]) + pstr("Global/Jump.wav")
    b += bytes([2]) + pstr("SONIC") + pstr("TAILS")
    for cat in stages:
        b += bytes([len(cat)])
        for folder, ident, nm, hl in cat:
            b += pstr(folder) + pstr(ident) + pstr(nm) + bytes([hl])
    return b


put("rsdk_gameconfig", "config1.bin", gameconfig("Retro-Engine Test", [("Options.Soundtrack", 1), ("Game.Lives", 3)],
    [[("Title", "1", "TITLE", 0)], [("R1", "1", "Round 1", 0), ("R1", "2", "Round 1b", 1)], [], [("Special", "1", "Special 1", 0)]]))
put("rsdk_gameconfig", "config2.bin", gameconfig("Minimal", [], [[], [], [], []]))

# ---------------------------------------------------------------- stage files (rsdk_stage: byte 0 selects the loader)
act = pstr("ROUND 1 - ZONE") + bytes([1, 0, 0, 0]) + bytes([0]) + bytes([2, 2])
act += bytes([0, 1, 0, 2, 0, 3, 0, 4])
act += bytes([1]) + pstr("Ring")
act += struct.pack(">H", 2)
act += bytes([1, 0, 0, 0x10, 0, 0x20]) + bytes([2, 1, 0, 0x30, 0, 0x40])
put("rsdk_stage", "act1.bin", b"\x00" + act)

bg = bytes([1])
bg += bytes([1, 0, 1, 2, 3, 4])
bg += bytes([0])
bg += bytes([2, 2, 1, 0, 0x80, 1])
bg += bytes([1, 2, 0xFF, 5, 3, 0xFF, 0xFF])
bg += bytes([0, 1, 0, 2, 0, 3, 0, 4])
put("rsdk_stage", "backgrounds.bin", b"\x01" + bg)

put("rsdk_stage", "tiles.gif", b"\x02" + gif(16, 16, [(x + y) % 256 for y in range(16) for x in range(16)], colors=256))
tg = struct.pack(">HH", 16, 16) + bytes(255 * 3 + 3) + bytes([5, 6, 7, 8]) + bytes([0xFF, 0, 12]) + bytes([0xFF, 0xFF])
put("rsdk_stage", "tiles.gfx", b"\x03" + tg)

# ---------------------------------------------------------------- text / font (rsdk_text: byte 0 selects loader)
def font_entry(cid, sx, sy, w, h):
    return le32(cid) + struct.pack("<HH", sx, sy) + bytes([w - 1, 0, h - 1, 0]) + bytes([0, 0, 0, 0, 8, 0]) + bytes([0, 0])


put("rsdk_text", "font.fnt", b"\x00" + b"".join(font_entry(ord(ch), 8 * i, 0, 8, 8) for i, ch in enumerate("ABC")))
put("rsdk_text", "menu_ascii.txt", b"\x01" + b"START GAME\rOPTIONS\rQUIT\r")
put("rsdk_text", "menu_mapped.txt", b"\x02" + b"ABC\rCBA\r")
put("rsdk_text", "menu_utf16.txt", b"\x01" + b"\xFF\xFE" + "HELLO\rWORLD\r".encode("utf-16-le"))

# ---------------------------------------------------------------- ini
put("rsdk_ini", "settings.ini", "[Game]\nlanguage=0\nwindowed = 1\n# comment\n[Window]\nscalingMode=1\nfullscreen=false\nvsync=true\nscreenWidth=424\n[Audio]\nBGMVolume=100\nSFXVolume=80\n")
put("rsdk_ini", "mod.ini", "Name=My Mod\nDescription=Test\nAuthor=nobody\nVersion=1.0.0\nActive=y\n")

# ---------------------------------------------------------------- Game.xml (mod loader)
put("rsdk_gamexml", "game1.xml", """<?xml version="1.0" encoding="UTF-8"?>
<game>
    <title name="Modded Game"/>
    <variables>
        <variable name="Mod.Flag" value="5"/>
        <variable name="Mod.Other" value="-2"/>
    </variables>
    <palette>
        <color bank="1" index="3" r="255" g="128" b="0"/>
        <color bank="0" index="200" r="1" g="2" b="3"/>
    </palette>
    <objects>
        <object name="ModObj" script="Mod/Obj.txt" forceLoad="true"/>
    </objects>
    <sounds>
        <soundfx name="Mod/Sfx" path="Mod/Sfx.wav"/>
    </sounds>
    <players>
        <player name="SONIC"/>
    </players>
    <presentationStages>
        <stage name="Title" folder="Title" id="1"/>
    </presentationStages>
    <regularStages>
        <stage name="Zone 1" folder="Z1" id="1" highlight="true"/>
        <stage name="Zone 2" folder="Z1" id="2"/>
    </regularStages>
</game>
""")
put("rsdk_gamexml", "game2.xml", "<game><title name=\"X\"/><variables><variable name=\"A\" value=\"1\"/></variables></game>")


# ---------------------------------------------------------------- RSDK data pack
A = b"4RaS9D7KaEbxcp2o5r6t\x00"
B = b"3tRaUxLmEaSn\x00"


def encrypt(data, vsize):
    no = (vsize & 0x1FC) >> 2
    pb = (no % 9) + 1
    pa = (no % pb) + 1
    swap = 0
    out = bytearray()
    for d in data:
        x = d ^ A[pa]
        pa += 1
        if swap:
            x = ((x & 0xF) << 4) | (x >> 4)
        out.append(x ^ B[pb] ^ no)
        pb += 1
        if pa <= 19 or pb <= 11:
            if pa > 19:
                pa = 1
                swap ^= 1
            if pb > 11:
                pb = 1
                swap ^= 1
        else:
            no = (no + 1) & 0x7F
            if swap:
                swap = 0
                pa = (no % 12) + 6
                pb = (no % 5) + 4
            else:
                swap = 1
                pa = (no % 15) + 3
                pb = (no % 7) + 1
    return bytes(out)


def pack(dirs):
    """dirs: [(dirname, [(filename, bytes)])]"""
    bodies = []
    for dn, files in dirs:
        body = b""
        for fn, data in files:
            nb = fn.encode()
            body += bytes([len(nb)]) + bytes((~c) & 0xFF for c in nb) + le32(len(data)) + encrypt(data, len(data))
        bodies.append(body)
    hdr_size = 6 + sum(1 + len(dn) + 4 for dn, _ in dirs)
    offs = []
    o = 0
    for b in bodies:
        offs.append(o)
        o += len(b)
    hdr = le32(hdr_size) + struct.pack("<H", len(dirs))
    for (dn, _), off in zip(dirs, offs):
        nb = dn.encode()
        hdr += bytes([len(nb)]) + bytes(c ^ ((-1 - len(nb)) & 0xFF) for c in nb) + le32(off)
    assert len(hdr) == hdr_size
    return hdr + b"".join(bodies)


cfg = gameconfig("Packed Game", [("Options.Soundtrack", 1)], [[("Title", "1", "TITLE", 0)], [], [], []])
# The 3000-byte Pad.bin is the LAST file of the last directory and is never looked up by the harness. The loader
# walks a directory's files for any name that is not first in it, so every looked-up name is first in its directory;
# this keeps the sensitive header (name lengths, sizes, offsets) a small share of each seed, so a few byte mutations
# rarely derail the walk (Mayhem's sanity check needs a few clean libFuzzer iterations before it will fuzz).
pad = bytes((i * 7 + 3) & 0xFF for i in range(3000))
put("rsdk_datapack", "data1.rsdk", pack([
    ("Data/Game/", [("GameConfig.bin", cfg)]),
    ("Data/Scripts/ByteCode/", [("GlobalCode.bin", bytecode([1, 5, 9, 12, 3, 7, 2], [0, 4, 8], True)), ("Pad.bin", pad)]),
]))
put("rsdk_datapack", "data2.rsdk", pack([("Data/Game/", [("GameConfig.bin", cfg), ("Other.bin", b"hello world"), ("Pad.bin", pad)])]))
