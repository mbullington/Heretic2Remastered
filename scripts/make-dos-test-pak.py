#!/usr/bin/env python3
"""Build deterministic, explicitly synthetic DOS engine, renderer, and audio fixtures."""

import argparse
import math
from pathlib import Path
import struct

HEADER_SIZE = 12
DIRECTORY_ENTRY_SIZE = 64
ENTRIES = {
    b"SYNTHETIC-NOT-GAME.txt": (
        b"Heretic2Remastered DOS synthetic engine-path fixture.\r\n"
        b"This is not original Heretic II data and cannot prove gameplay.\r\n"
    ),
    b"config.cfg": b"// Synthetic non-game runtime config.\r\n",
    b"default.cfg": (
        b"// Synthetic non-game DOS startup config.\r\n"
        b"set cl_maxfps 30\r\nset vid_maxfps 60\r\n"
    ),
    b"default_h2r.cfg": (
        b"// Synthetic non-game DOS startup config alias.\r\n"
        b"set cl_maxfps 30\r\nset vid_maxfps 60\r\n"
    ),
    b"doscheck.txt": b"H2DOS RESOURCE PATH PASS\n",
    b"gamemsg.txt": b"SYNTHETIC NON-GAME MESSAGE TABLE\r\n",
    b"levelmsg.txt": b"SYNTHETIC NON-GAME LEVEL MESSAGE TABLE\r\n",
    b"menus.cfg": b"// Synthetic non-game empty menu config.\r\n",
    b"user.cfg": b"// Synthetic non-game empty user config.\r\n",
}


def build_m8(
    name: str, width: int, height: int, pixels: bytes, palette: bytes, num_frames: int = 0
) -> bytes:
    """Create an M8 v2 with one mip level; all bytes are synthetic."""
    if len(pixels) != width * height or len(palette) != 256 * 3:
        raise ValueError("invalid synthetic M8 dimensions")
    header = bytearray(1040)
    struct.pack_into("<I", header, 0, 2)
    header[4:36] = name.encode("ascii")[:31].ljust(32, b"\0")
    struct.pack_into("<I", header, 36, width)
    struct.pack_into("<I", header, 100, height)
    struct.pack_into("<I", header, 164, len(header))
    header[260:1028] = palette
    struct.pack_into("<i", header, 1036, num_frames)
    return bytes(header) + pixels


def build_m32(name: str, width: int, height: int, pixels: bytes) -> bytes:
    """Create an M32 v4 with one RGBA mip level; all bytes are synthetic."""
    if len(pixels) != width * height * 4:
        raise ValueError("invalid synthetic M32 dimensions")
    header = bytearray(968)
    struct.pack_into("<I", header, 0, 4)
    header[4:132] = name.encode("ascii")[:127].ljust(128, b"\0")
    struct.pack_into("<I", header, 516, width)
    struct.pack_into("<I", header, 580, height)
    struct.pack_into("<I", header, 644, len(header))
    struct.pack_into("<I", header, 716, 1)  # num_frames
    struct.pack_into("<ff", header, 720, 1.0, 1.0)
    return bytes(header) + pixels


def synthetic_palette() -> bytes:
    colors = bytearray()
    for index in range(256):
        colors.extend(((index * 5) & 255, (index * 3) & 255, (index * 7) & 255))
    colors[255 * 3:255 * 3 + 3] = b"\0\0\0"
    return bytes(colors)


def build_malformed_wavs() -> dict[bytes, bytes]:
    """Create negative RIFF fixtures for the production parser; none contain game data."""
    valid_fmt = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)
    return {
        b"sound/dos/malformed-truncated.wav": b"RIFF\0\0\0\0",
        b"sound/dos/malformed-zero-width.wav": (
            b"RIFF" + struct.pack("<I", 4 + 8 + len(valid_fmt) + 8 + 2)
            + b"WAVEfmt " + struct.pack("<I", len(valid_fmt))
            + valid_fmt[:-2] + struct.pack("<H", 0)
            + b"data" + struct.pack("<I", 2) + b"\0\0"
        ),
        b"sound/dos/malformed-data-length.wav": (
            b"RIFF" + struct.pack("<I", 4 + 8 + len(valid_fmt) + 8 + 2)
            + b"WAVEfmt " + struct.pack("<I", len(valid_fmt)) + valid_fmt
            + b"data" + struct.pack("<I", 0x7FFFFFFF) + b"\0\0"
        ),
    }


def build_synthetic_wav() -> bytes:
    """Create a deterministic 16-bit mono square wave for the mixer proof."""
    rate = 22050
    frame_count = rate // 4
    samples = bytearray()
    for frame in range(frame_count):
        amplitude = 12000 if (frame // 25) & 1 else -12000
        samples.extend(struct.pack("<h", amplitude))
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)
    return (
        b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(samples))
        + b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt
        + b"data" + struct.pack("<I", len(samples)) + samples
    )


def build_wall_texture() -> bytes:
    width = height = 128
    pixels = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            mortar = x % 32 < 2 or y % 16 < 2
            pixels[y * width + x] = 12 if mortar else 92 + ((x // 32 + y // 16) & 1) * 28
    return build_m8("synthetic", width, height, bytes(pixels), synthetic_palette())


def build_special_texture(name: str, first: int, second: int, num_frames: int = 0) -> bytes:
    size = 64
    pixels = bytearray(size * size)
    for y in range(size):
        for x in range(size):
            pixels[y * size + x] = first if ((x // 8) ^ (y // 8)) & 1 else second
    return build_m8(name, size, size, bytes(pixels), synthetic_palette(), num_frames)


def build_sky_face(suffix: str, index: int) -> bytes:
    size = 64
    colors = (
        (210, 45, 45), (55, 95, 220), (45, 190, 85),
        (220, 145, 35), (130, 70, 210), (35, 190, 205),
    )
    palette = bytearray(synthetic_palette())
    palette[32 * 3:32 * 3 + 3] = bytes(colors[index])
    palette[33 * 3:33 * 3 + 3] = b"\xff\xff\xff"
    palette[34 * 3:34 * 3 + 3] = b"\x08\x08\x08"
    pixels = bytearray([32] * (size * size))
    for y in range(size):
        for x in range(size):
            if x < 2 or y < 2 or x >= size - 2 or y >= size - 2:
                pixels[y * size + x] = 34
            elif x == size // 2 or y == size // 2 or (8 + index * 7 <= x < 12 + index * 7 and 8 <= y < 20):
                pixels[y * size + x] = 33
    return build_m8(f"synthetic-sky-{suffix}", size, size, bytes(pixels), bytes(palette))


def build_sprite_texture() -> bytes:
    size = 32
    pixels = bytearray([255] * (size * size))
    for y in range(size):
        for x in range(size):
            dx, dy = x - 16, y - 16
            if dx * dx + dy * dy < 13 * 13:
                pixels[y * size + x] = 205 if (x + y) & 2 else 225
    return build_m8("synthetic-sprite", size, size, bytes(pixels), synthetic_palette())


def build_sprite_model() -> bytes:
    frame = struct.pack(
        "<4i64s", 32, 32, 16, 16, b"synthetic.m8".ljust(64, b"\0")
    )
    return struct.pack("<4sII", b"IDS2", 2, 1) + frame


def fm_block(name: str, version: int, payload: bytes) -> bytes:
    return struct.pack("<32sii", name.encode("ascii").ljust(32, b"\0"), version, len(payload)) + payload


def placement(origin: tuple[float, float, float]) -> bytes:
    direction = (origin[0] + 20.0, origin[1], origin[2])
    up = (origin[0], origin[1], origin[2] + 20.0)
    return struct.pack("<9f", *(origin + direction + up))


def build_flex_skin(alternate: bool = False, shadow: bool = False) -> bytes:
    size = 32
    pixels = bytearray(size * size)
    for y in range(size):
        for x in range(size):
            if shadow and (x < 3 or y < 3 or x >= size - 3 or y >= size - 3):
                pixels[y * size + x] = 255
            elif shadow:
                pixels[y * size + x] = 2
            elif alternate:
                pixels[y * size + x] = 165 if ((x // 4) ^ (y // 4)) & 1 else 205
            else:
                pixels[y * size + x] = 45 if x < 3 or y < 3 else 115 + ((x // 8 + y // 8) & 1) * 25
    name = "synthetic-shadow" if shadow else ("synthetic-actor-alt" if alternate else "synthetic-actor")
    return build_m8(name, size, size, bytes(pixels), synthetic_palette())


def build_synthetic_fm() -> bytes:
    """Create a minimal labeled original synthetic Corvus FM block stream."""
    vertices0 = [
        (0, 28, 8), (0, 68, 8), (0, 48, 48),
        (0, 12, 44), (0, 84, 44), (0, 48, 84),
        (0, 30, 84), (0, 66, 84), (0, 48, 116),
    ]
    vertices1 = vertices0[:3] + [
        (0, 4, 50), (0, 88, 50), (0, 52, 90),
        (0, 40, 92), (0, 76, 92), (0, 58, 126),
    ]
    header = struct.pack("<10i", 32, 32, 76, 1, 9, 9, 3, 33, 2, 3)
    skins = b"models/synthetic_actor/skin.m8\0".ljust(64, b"\0")
    st = b"".join(struct.pack("<hh", s, t) for _ in range(3) for s, t in ((2, 30), (30, 30), (16, 2)))
    triangles = b"".join(
        struct.pack("<6h", base, base + 1, base + 2, base, base + 1, base + 2)
        for base in (0, 3, 6)
    )

    def frame(name: bytes, vertices: list[tuple[int, int, int]]) -> bytes:
        result = bytearray(struct.pack("<6f16s", 0.5, 0.5, 0.5, 0.0, -24.0, -24.0, name.ljust(16, b"\0")))
        for index, vertex in enumerate(vertices):
            result.extend(struct.pack("<4B", *vertex, (5 + index * 23) & 255))
        return bytes(result)

    frames = frame(b"synthetic-old", vertices0) + frame(b"synthetic-new", vertices1)
    commands = bytearray()
    mesh_records = bytearray()
    for base in (0, 3, 6):
        start = len(commands) // 4
        commands.extend(struct.pack("<i", -3))
        for (s, t), index in zip(((0.08, 0.94), (0.92, 0.94), (0.5, 0.06)), range(base, base + 3)):
            commands.extend(struct.pack("<ffi", s, t, index))
        commands.extend(struct.pack("<i", 0))
        mesh_records.extend(bytes(512) + struct.pack("<hh", start, 11))

    skeleton = bytearray(struct.pack("<5i", 5, 3, 3, 3, 3))
    skeleton.extend(struct.pack("<3i", 6, 7, 8))
    skeleton.extend(struct.pack("<3i", 3, 4, 5))
    skeleton.extend(struct.pack("<3i", 0, 1, 2))
    skeleton.extend(struct.pack("<i", 1))
    for joints in (
        ((0.0, 48.0, 40.0), (0.0, 48.0, 64.0), (0.0, 48.0, 96.0)),
        ((0.0, 48.0, 40.0), (0.0, 52.0, 68.0), (0.0, 58.0, 104.0)),
    ):
        for joint in joints:
            skeleton.extend(placement(joint))

    references = bytearray(struct.pack("<ii", 0, 1))
    frame_references = (
        ((0.0, 20.0, 64.0), (0.0, 76.0, 64.0), (0.0, 32.0, 10.0), (0.0, 64.0, 10.0),
         (0.0, 88.0, 60.0), (0.0, 10.0, 58.0), (0.0, 48.0, 112.0)),
        ((0.0, 28.0, 68.0), (0.0, 84.0, 68.0), (0.0, 32.0, 10.0), (0.0, 64.0, 10.0),
         (0.0, 96.0, 64.0), (0.0, 18.0, 62.0), (0.0, 58.0, 122.0)),
    )
    for frame_refs in frame_references:
        for reference in frame_refs:
            references.extend(placement(reference))
    return b"".join((
        fm_block("header", 2, header), fm_block("skin", 1, skins), fm_block("st coord", 1, st),
        fm_block("tris", 1, triangles), fm_block("frames", 1, frames), fm_block("glcmds", 1, bytes(commands)),
        fm_block("mesh nodes", 3, bytes(mesh_records)), fm_block("skeleton", 1, bytes(skeleton)),
        fm_block("references", 1, bytes(references)),
    ))


def fm_blocks(payload: bytes) -> list[tuple[str, int, int, int]]:
    blocks = []
    offset = 0
    while offset + 40 <= len(payload):
        name_raw, version, size = struct.unpack_from("<32sii", payload, offset)
        name = name_raw.split(b"\0", 1)[0].decode("ascii")
        blocks.append((name, offset, version, size))
        offset += 40 + size
    if offset != len(payload):
        raise ValueError("valid synthetic FM did not parse into complete blocks")
    return blocks


def mutate_fm(valid: bytes, block_name: str, payload_offset: int, replacement: bytes) -> bytes:
    result = bytearray(valid)
    block = next(block for block in fm_blocks(valid) if block[0] == block_name)
    start = block[1] + 40 + payload_offset
    result[start:start + len(replacement)] = replacement
    return bytes(result)


def build_malformed_fms(valid: bytes) -> dict[bytes, bytes]:
    blocks = {name: (offset, version, size) for name, offset, version, size in fm_blocks(valid)}
    header_end = blocks["header"][0] + 40 + blocks["header"][2]
    missing_header = valid[header_end:]
    duplicate_header = valid[:header_end] + valid
    negative_size = bytearray(valid)
    struct.pack_into("<i", negative_size, 36, -1)
    wrong_version = bytearray(valid)
    struct.pack_into("<i", wrong_version, blocks["frames"][0] + 32, 9)
    missing_terminator = mutate_fm(valid, "glcmds", 10 * 4, struct.pack("<i", 3))
    int_min_command = mutate_fm(valid, "glcmds", 0, struct.pack("<i", -0x80000000))
    bad_mesh = mutate_fm(valid, "mesh nodes", 512, struct.pack("<h", 32000))
    bad_skeleton_index = mutate_fm(valid, "skeleton", 24, struct.pack("<i", 99))
    bad_reference_type = mutate_fm(valid, "references", 0, struct.pack("<i", 99))
    compressed = valid + fm_block("comp data", 1, b"SYNTHETIC-UNSUPPORTED")
    return {
        b"models/malformed-fm-truncated-header.fm": valid[:20],
        b"models/malformed-fm-unterminated-ident.fm": b"X" * 32 + valid[32:],
        b"models/malformed-fm-negative-size.fm": bytes(negative_size),
        b"models/malformed-fm-duplicate-header.fm": duplicate_header,
        b"models/malformed-fm-missing-header.fm": missing_header,
        b"models/malformed-fm-wrong-version.fm": bytes(wrong_version),
        b"models/malformed-fm-count-overflow.fm": mutate_fm(valid, "header", 24, struct.pack("<i", 0x7FFFFFFF)),
        b"models/malformed-fm-framesize.fm": mutate_fm(valid, "header", 8, struct.pack("<i", 92)),
        b"models/malformed-fm-nonfinite-frame.fm": mutate_fm(valid, "frames", 0, struct.pack("<I", 0x7FC00000)),
        b"models/malformed-fm-missing-terminator.fm": missing_terminator,
        b"models/malformed-fm-int-min.fm": int_min_command,
        b"models/malformed-fm-xyz-index.fm": mutate_fm(valid, "glcmds", 12, struct.pack("<i", 12)),
        b"models/malformed-fm-mesh-range.fm": bad_mesh,
        b"models/malformed-fm-skin-name.fm": mutate_fm(valid, "skin", 0, b"S" * 64),
        b"models/malformed-fm-skeleton-index.fm": bad_skeleton_index,
        b"models/malformed-fm-reference-type.fm": bad_reference_type,
        b"models/malformed-fm-reference-nonfinite.fm": mutate_fm(valid, "references", 8, struct.pack("<I", 0x7F800000)),
        b"models/malformed-fm-compressed.fm": compressed,
    }


GLYPHS = {
    "A": [14, 17, 17, 31, 17, 17, 17], "B": [30, 17, 17, 30, 17, 17, 30],
    "C": [14, 17, 16, 16, 16, 17, 14], "D": [30, 17, 17, 17, 17, 17, 30],
    "E": [31, 16, 16, 30, 16, 16, 31], "F": [31, 16, 16, 30, 16, 16, 16],
    "G": [14, 17, 16, 23, 17, 17, 15], "H": [17, 17, 17, 31, 17, 17, 17],
    "I": [14, 4, 4, 4, 4, 4, 14], "J": [7, 2, 2, 2, 18, 18, 12],
    "K": [17, 18, 20, 24, 20, 18, 17], "L": [16, 16, 16, 16, 16, 16, 31],
    "M": [17, 27, 21, 21, 17, 17, 17], "N": [17, 25, 21, 19, 17, 17, 17],
    "O": [14, 17, 17, 17, 17, 17, 14], "P": [30, 17, 17, 30, 16, 16, 16],
    "Q": [14, 17, 17, 17, 21, 18, 13], "R": [30, 17, 17, 30, 20, 18, 17],
    "S": [15, 16, 16, 14, 1, 1, 30], "T": [31, 4, 4, 4, 4, 4, 4],
    "U": [17, 17, 17, 17, 17, 17, 14], "V": [17, 17, 17, 17, 17, 10, 4],
    "W": [17, 17, 17, 21, 21, 21, 10], "X": [17, 17, 10, 4, 10, 17, 17],
    "Y": [17, 17, 10, 4, 4, 4, 4], "Z": [31, 1, 2, 4, 8, 16, 31],
    "0": [14, 17, 19, 21, 25, 17, 14], "1": [4, 12, 4, 4, 4, 4, 14],
    "2": [14, 17, 1, 2, 4, 8, 31], "3": [30, 1, 1, 14, 1, 1, 30],
    "4": [2, 6, 10, 18, 31, 2, 2], "5": [31, 16, 16, 30, 1, 1, 30],
    "6": [14, 16, 16, 30, 17, 17, 14], "7": [31, 1, 2, 4, 8, 8, 8],
    "8": [14, 17, 17, 14, 17, 17, 14], "9": [14, 17, 17, 15, 1, 1, 14],
    ":": [0, 4, 4, 0, 4, 4, 0], "-": [0, 0, 0, 31, 0, 0, 0],
    ".": [0, 0, 0, 0, 0, 6, 6], "/": [1, 2, 2, 4, 8, 8, 16],
    ">": [16, 8, 4, 2, 4, 8, 16], "+": [0, 4, 4, 31, 4, 4, 0],
    "_": [0, 0, 0, 0, 0, 0, 31], "=": [0, 31, 0, 31, 0, 0, 0],
}


def build_conchars() -> bytes:
    size = 128
    pixels = bytearray(size * size * 4)
    for code in range(256):
        character = chr(code).upper()
        rows = GLYPHS.get(character)
        if rows is None:
            continue
        cell_x, cell_y = (code & 15) * 8, (code >> 4) * 8
        for y, row in enumerate(rows):
            for x in range(5):
                if row & (1 << (4 - x)):
                    offset = ((cell_y + y) * size + cell_x + x + 1) * 4
                    pixels[offset:offset + 4] = b"\xff\xff\xff\xff"
    return build_m32("synthetic-conchars", size, size, bytes(pixels))


# S/T windows of the 62 particle atlas cells (mirrors the production GL table).
PARTICLE_ST: list[tuple[float, float, float, float]] = [
    (0.00390625, 0.00390625, 0.02734375, 0.02734375),
    (0.03515625, 0.00390625, 0.05859375, 0.02734375),
    (0.06640625, 0.00390625, 0.08984375, 0.02734375),
    (0.09765625, 0.00390625, 0.12109375, 0.02734375),
    (0.00390625, 0.03515625, 0.02734375, 0.05859375),
    (0.03515625, 0.03515625, 0.05859375, 0.05859375),
    (0.06640625, 0.03515625, 0.08984375, 0.05859375),
    (0.09765625, 0.03515625, 0.12109375, 0.05859375),
    (0.00390625, 0.06640625, 0.02734375, 0.08984375),
    (0.03515625, 0.06640625, 0.05859375, 0.08984375),
    (0.06640625, 0.06640625, 0.08984375, 0.08984375),
    (0.09765625, 0.06640625, 0.12109375, 0.08984375),
    (0.00390625, 0.09765625, 0.02734375, 0.12109375),
    (0.03515625, 0.09765625, 0.05859375, 0.12109375),
    (0.06640625, 0.09765625, 0.08984375, 0.12109375),
    (0.09765625, 0.09765625, 0.12109375, 0.12109375),
    (0.12890625, 0.00390625, 0.18359375, 0.05859375),
    (0.19140625, 0.00390625, 0.24609375, 0.05859375),
    (0.12890625, 0.06640625, 0.18359375, 0.12109375),
    (0.19140625, 0.06640625, 0.24609375, 0.12109375),
    (0.00390625, 0.12890625, 0.12109375, 0.24609375),
    (0.12890625, 0.12890625, 0.24609375, 0.24609375),
    (0.25390625, 0.00390625, 0.37109375, 0.12109375),
    (0.37890625, 0.00390625, 0.49609375, 0.12109375),
    (0.25390625, 0.12890625, 0.37109375, 0.24609375),
    (0.37890625, 0.12890625, 0.49609375, 0.24609375),
    (0.00390625, 0.25390625, 0.24609375, 0.49609375),
    (0.25390625, 0.25390625, 0.49609375, 0.49609375),
    (0.50390625, 0.00390625, 0.74609375, 0.24609375),
    (0.75390625, 0.00390625, 0.99609375, 0.24609375),
    (0.50390625, 0.25390625, 0.74609375, 0.49609375),
    (0.75390625, 0.25390625, 0.87109375, 0.37109375),
    (0.87890625, 0.25390625, 0.99609375, 0.37109375),
    (0.75390625, 0.37890625, 0.87109375, 0.49609375),
    (0.87890625, 0.37890625, 0.99609375, 0.49609375),
    (0.00390625, 0.50390625, 0.24609375, 0.74609375),
    (0.00390625, 0.50390625, 0.24609375, 0.74609375),
    (0.25390625, 0.50390625, 0.37109375, 0.62109375),
    (0.37890625, 0.50390625, 0.43359375, 0.55859375),
    (0.44140625, 0.50390625, 0.49609375, 0.55859375),
    (0.37890625, 0.56640625, 0.43359375, 0.62109375),
    (0.44140625, 0.56640625, 0.49609375, 0.62109375),
    (0.25390625, 0.62890625, 0.30859375, 0.68359375),
    (0.31640625, 0.62890625, 0.37109375, 0.68359375),
    (0.25390625, 0.69140625, 0.30859375, 0.74609375),
    (0.31640625, 0.69140625, 0.37109375, 0.74609375),
    (0.37890625, 0.62890625, 0.43359375, 0.68359375),
    (0.44140625, 0.62890625, 0.49609375, 0.68359375),
    (0.37890625, 0.69140625, 0.43359375, 0.74609375),
    (0.44140625, 0.69140625, 0.49609375, 0.74609375),
    (0.00390625, 0.75390625, 0.24609375, 0.99609375),
    (0.25390625, 0.75390625, 0.49609375, 0.99609375),
    (0.50390625, 0.50390625, 0.62109375, 0.62109375),
    (0.62890625, 0.50390625, 0.74609375, 0.62109375),
    (0.50390625, 0.62890625, 0.62109375, 0.74609375),
    (0.62890625, 0.62890625, 0.74609375, 0.74609375),
    (0.75390625, 0.50390625, 0.99609375, 0.74609375),
    (0.50390625, 0.75390625, 0.74609375, 0.99609375),
    (0.75390625, 0.75390625, 0.87109375, 0.87109375),
    (0.87890625, 0.75390625, 0.99609375, 0.87109375),
    (0.75390625, 0.87890625, 0.87109375, 0.99609375),
    (0.87890625, 0.87890625, 0.99609375, 0.99609375),
]


def _particle_cell_alpha(lu: float, lv: float, idx: int) -> int:
    """Deterministic per-cell shape so different particle types differ visibly."""
    kind = idx % 4
    dx, dy = lu - 0.5, lv - 0.5
    r = (dx * dx + dy * dy) ** 0.5 * 2.0
    if kind == 0:
        a = max(0.0, 1.0 - r)
    elif kind == 1:
        a = 1.0 if r < 0.7 else 0.0
    elif kind == 2:
        a = 1.0 if abs(r - 0.6) < 0.18 else 0.0
    else:
        a = 1.0 if abs(dx) > abs(dy) else 0.4
    return int(a * 255)


def build_particle_atlas_m32() -> bytes:
    size = 256
    pixels = bytearray(size * size * 4)
    for y in range(size):
        v = (y + 0.5) / size
        for x in range(size):
            u = (x + 0.5) / size
            alpha = 0
            for idx, (s0, t0, s1, t1) in enumerate(PARTICLE_ST):
                if s0 <= u <= s1 and t0 <= v <= t1:
                    alpha = _particle_cell_alpha((u - s0) / (s1 - s0), (v - t0) / (t1 - t0), idx)
                    break
            offset = (y * size + x) * 4
            pixels[offset:offset + 4] = bytes((255, 255, 255, alpha))
    return build_m32("synthetic-particle-m32", size, size, bytes(pixels))


def build_particle_atlas_m8() -> bytes:
    size = 256
    palette = bytearray()
    for index in range(256):
        palette.extend((index, index, index))
    pixels = bytearray(size * size)
    for y in range(size):
        v = (y + 0.5) / size
        for x in range(size):
            u = (x + 0.5) / size
            value = 0
            for idx, (s0, t0, s1, t1) in enumerate(PARTICLE_ST):
                if s0 <= u <= s1 and t0 <= v <= t1:
                    value = _particle_cell_alpha((u - s0) / (s1 - s0), (v - t0) / (t1 - t0), idx)
                    break
            pixels[y * size + x] = value
    return build_m8("synthetic-particle-m8", size, size, bytes(pixels), bytes(palette))


def build_panel() -> bytes:
    width, height = 64, 32
    pixels = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 4
            pixels[offset:offset + 4] = bytes((35 + x, 20 + y, 70 + (x ^ y) % 40, 255))
    return build_m32("synthetic-panel", width, height, bytes(pixels))


def build_menu_book() -> bytes:
    header = struct.pack("<4sIiii", b"BOOK", 2, 1, 640, 480)
    frame = struct.pack(
        "<4i64s", 0, 0, 640, 480, b"synthetic-menu.m32".ljust(64, b"\0")
    )
    return header + frame


def mutate_bsp_lump(payload: bytes, lump: int, relative_offset: int, replacement: bytes) -> bytes:
    result = bytearray(payload)
    lump_offset, lump_length = struct.unpack_from("<II", result, 8 + lump * 8)
    if relative_offset < 0 or relative_offset + len(replacement) > lump_length:
        raise ValueError("malformed BSP mutation is outside its target lump")
    result[lump_offset + relative_offset:lump_offset + relative_offset + len(replacement)] = replacement
    return bytes(result)


def build_malformed_bsps(valid: bytes) -> dict[bytes, bytes]:
    """Mutate cross-lump semantic fields while retaining valid BSP byte ranges."""
    leaf_size = struct.calcsize("<ihh3h3h4H")
    texinfo_size = struct.calcsize("<8fii32si")
    return {
        b"maps/malformed-child.bsp": mutate_bsp_lump(valid, 4, 4, struct.pack("<i", -0x80000000)),
        b"maps/malformed-texinfo-chain.bsp": mutate_bsp_lump(
            valid, 5, texinfo_size * 2 + 72, struct.pack("<i", 1)
        ),
        b"maps/malformed-node-cycle.bsp": mutate_bsp_lump(valid, 4, 4, struct.pack("<i", 0)),
        b"maps/malformed-area.bsp": mutate_bsp_lump(valid, 8, leaf_size + 6, struct.pack("<h", 256)),
        b"maps/malformed-area-reference.bsp": mutate_bsp_lump(valid, 8, leaf_size + 6, struct.pack("<h", 2)),
        b"maps/malformed-leaf-range.bsp": mutate_bsp_lump(valid, 8, leaf_size + 20, struct.pack("<H", 1)),
        b"maps/malformed-face-range.bsp": mutate_bsp_lump(valid, 6, 4, struct.pack("<i", 0x7FFFFFFF)),
        b"maps/malformed-submodel-range.bsp": mutate_bsp_lump(valid, 13, 40, struct.pack("<i", 0x7FFFFFFF)),
        b"maps/malformed-lightmap.bsp": mutate_bsp_lump(valid, 6, 16, struct.pack("<i", 0x7FFFFFFE)),
        b"maps/malformed-visibility.bsp": mutate_bsp_lump(valid, 3, 4, struct.pack("<i", 4)),
    }


def build_malformed_book() -> bytes:
    header = struct.pack("<4sIiii", b"BOOK", 2, 1, 640, 480)
    frame = struct.pack("<4i64s", 630, 0, 32, 32, b"synthetic-menu.m32".ljust(64, b"\0"))
    return header + frame


def build_malformed_font() -> bytes:
    glyph = struct.pack("<4fiii", float("nan"), 0.0, 1.0, 1.0, 8, 8, 0)
    return glyph + bytes((224 - 1) * struct.calcsize("<4fiii"))


def build_synthetic_bsp() -> bytes:
    """Create a labeled special-surface IBSP v38 production-path fixture."""
    surf_sky = 0x4
    surf_warp = 0x8
    surf_trans66 = 0x20
    surf_flowing = 0x40
    surf_animspeed = 0x1000
    surf_undulate = 0x2000
    surf_quake = 0x4000
    entities = (
        b'{\n"classname" "worldspawn"\n"message" "SYNTHETIC NON-GAME RENDER MAP"\n}\n'
        b'{\n"classname" "info_player_start"\n"origin" "32 0 0"\n"angle" "0"\n}\n\0'
    )
    planes = (
        struct.pack("<4fi", 1.0, 0.0, 0.0, 96.0, 0)
        + struct.pack("<4fi", 0.0, 0.0, 1.0, -48.0, 2)
    )
    wall_vecs = (0.0, 1.0, 0.0, 64.0, 0.0, 0.0, 1.0, 48.0)
    floor_vecs = (1.0, 0.0, 0.0, -32.0, 0.0, 1.0, 0.0, 80.0)
    texinfo_specs = (
        (wall_vecs, surf_animspeed, b"synthetic-anim0", 1),
        (wall_vecs, surf_animspeed, b"synthetic-anim1", 0),
        (wall_vecs, surf_flowing, b"synthetic-flow", -1),
        (wall_vecs, surf_warp | surf_flowing | surf_trans66 | surf_undulate, b"synthetic-water", -1),
        (wall_vecs, surf_sky, b"synthetic-sky", -1),
        (wall_vecs, surf_quake, b"synthetic-quake", -1),
        (floor_vecs, 0, b"synthetic", -1),
    )
    texinfo = b"".join(
        struct.pack("<8fii32si", *vecs, flags, 0, name.ljust(32, b"\0"), next_texinfo)
        for vecs, flags, name, next_texinfo in texinfo_specs
    )
    face_specs = (
        (0, 1, ((96.0, -64.0, -42.0), (96.0, -22.0, -42.0), (96.0, -22.0, 18.0), (96.0, -64.0, 18.0)), 0),
        (0, 1, ((96.0, -20.0, -42.0), (96.0, 20.0, -42.0), (96.0, 20.0, 18.0), (96.0, -20.0, 18.0)), 2),
        (0, 1, ((96.0, 22.0, -42.0), (96.0, 64.0, -42.0), (96.0, 64.0, 18.0), (96.0, 22.0, 18.0)), 3),
        (0, 1, ((96.0, -64.0, 20.0), (96.0, -2.0, 20.0), (96.0, -2.0, 48.0), (96.0, -64.0, 48.0)), 4),
        (0, 1, ((96.0, 2.0, 20.0), (96.0, 64.0, 20.0), (96.0, 64.0, 48.0), (96.0, 2.0, 48.0)), 5),
        (1, 0, ((32.0, -80.0, -48.0), (112.0, -80.0, -48.0), (112.0, 80.0, -48.0), (32.0, 80.0, -48.0)), 6),
    )
    points: list[tuple[float, float, float]] = []
    edges = bytearray(struct.pack("<2H", 0, 0))
    surfedges: list[int] = []
    face_records = bytearray()
    lighting = bytearray()
    for face_index, (plane, side, face_points, texinfo_index) in enumerate(face_specs):
        first_edge = len(surfedges)
        first_vertex = len(points)
        points.extend(face_points)
        for edge_index in range(4):
            edges.extend(struct.pack("<2H", first_vertex + edge_index, first_vertex + (edge_index + 1) % 4))
            surfedges.append(1 + face_index * 4 + edge_index)
        vecs, flags, _, _ = texinfo_specs[texinfo_index]
        if flags & (surf_sky | surf_warp | surf_trans66):
            styles = (255, 255, 255, 255)
            light_offset = -1
        else:
            texture_s = [sum(point[axis] * vecs[axis] for axis in range(3)) + vecs[3] for point in face_points]
            texture_t = [sum(point[axis] * vecs[4 + axis] for axis in range(3)) + vecs[7] for point in face_points]
            light_width = int(math.ceil(max(texture_s) / 16.0) - math.floor(min(texture_s) / 16.0) + 1)
            light_height = int(math.ceil(max(texture_t) / 16.0) - math.floor(min(texture_t) / 16.0) + 1)
            styles = (0, 255, 255, 255)
            light_offset = len(lighting)
            for y in range(light_height):
                for x in range(light_width):
                    lighting.extend((70 + (x * 17 + face_index * 11) % 150, 65 + (y * 19 + face_index * 13) % 150, 85 + ((x + y + face_index) * 15) % 140))
        face_records.extend(struct.pack("<Hhihh4Bi", plane, side, first_edge, 4, texinfo_index, *styles, light_offset))
    vertices = b"".join(struct.pack("<3f", *point) for point in points)
    visibility = struct.pack("<iiiB", 1, 12, 12, 1)
    face_count = len(face_specs)
    node = struct.pack("<iii3h3h2H", 0, -1, -2, -128, -128, -128, 128, 128, 128, 0, face_count)
    leaf_solid = struct.pack("<ihh3h3h4H", 1, -1, 0, -128, -128, -128, 128, 128, 128, 0, 0, 0, 0)
    leaf_empty = struct.pack("<ihh3h3h4H", 0, 0, 0, -128, -128, -128, 128, 128, 128, 0, face_count, 0, 1)
    model = struct.pack(
        "<9fiii", -128.0, -128.0, -128.0, 128.0, 128.0, 128.0,
        0.0, 0.0, 0.0, 0, 0, face_count,
    )
    lumps = {
        0: entities, 1: planes, 2: vertices, 3: visibility, 4: node, 5: texinfo,
        6: bytes(face_records), 7: bytes(lighting), 8: leaf_solid + leaf_empty,
        9: b"".join(struct.pack("<H", index) for index in range(face_count)),
        10: struct.pack("<H", 0), 11: bytes(edges),
        12: struct.pack(f"<{len(surfedges)}i", *surfedges), 13: model,
        14: struct.pack("<iii", 0, 1, 1), 15: struct.pack("<Hh", 0, 6),
        17: struct.pack("<iiii", 0, 0, 0, 0),
    }
    header_size = 8 + 19 * 8
    body = bytearray()
    directory: list[tuple[int, int]] = []
    for index in range(19):
        while (header_size + len(body)) % 4:
            body.append(0)
        payload = lumps.get(index, b"")
        directory.append((header_size + len(body), len(payload)))
        body.extend(payload)
    header = bytearray(struct.pack("<4sI", b"IBSP", 38))
    for offset, length in directory:
        header.extend(struct.pack("<II", offset, length))
    return bytes(header + body)


def build_pak() -> bytes:
    entries = dict(ENTRIES)
    synthetic_bsp = build_synthetic_bsp()
    synthetic_fm = build_synthetic_fm()
    entries.update({
        b"maps/synthetic.bsp": synthetic_bsp,
        b"textures/synthetic.m8": build_wall_texture(),
        b"textures/synthetic-anim0.m8": build_special_texture("synthetic-anim0", 42, 72, 4),
        b"textures/synthetic-anim1.m8": build_special_texture("synthetic-anim1", 162, 202, 4),
        b"textures/synthetic-flow.m8": build_special_texture("synthetic-flow", 82, 122),
        b"textures/synthetic-water.m8": build_special_texture("synthetic-water", 132, 182),
        b"textures/synthetic-sky.m8": build_special_texture("synthetic-sky-opening", 32, 62),
        b"textures/synthetic-quake.m8": build_special_texture("synthetic-quake", 212, 232),
        b"pics/skies/syntheticrt.m8": build_sky_face("rt", 0),
        b"pics/skies/syntheticbk.m8": build_sky_face("bk", 1),
        b"pics/skies/syntheticlf.m8": build_sky_face("lf", 2),
        b"pics/skies/syntheticft.m8": build_sky_face("ft", 3),
        b"pics/skies/syntheticup.m8": build_sky_face("up", 4),
        b"pics/skies/syntheticdn.m8": build_sky_face("dn", 5),
        b"pics/misc/conchars.m32": build_conchars(),
        b"pics/misc/conback.m8": build_wall_texture(),
        b"pics/misc/synthetic-panel.m32": build_panel(),
        b"pics/misc/particle.m32": build_particle_atlas_m32(),
        b"pics/misc/aparticle.m8": build_particle_atlas_m8(),
        b"book/back/b_conback8.bk": build_menu_book(),
        b"Book/synthetic-menu.m32": build_panel(),
        b"Sprites/synthetic.m8": build_sprite_texture(),
        b"models/synthetic.sp2": build_sprite_model(),
        b"models/synthetic_actor/tris.fm": synthetic_fm,
        b"models/synthetic_actor/skin.m8": build_flex_skin(),
        b"models/synthetic-shadow.m8": build_flex_skin(shadow=True),
        b"sound/dos/synthetic.wav": build_synthetic_wav(),
        b"book/malformed-frame.bk": build_malformed_book(),
        b"pics/misc/malformed.fnt": build_malformed_font(),
    })
    entries.update(build_malformed_wavs())
    entries.update(build_malformed_bsps(synthetic_bsp))
    entries.update(build_malformed_fms(synthetic_fm))
    data = bytearray()
    directory = bytearray()
    for name, payload in sorted(entries.items()):
        if len(name) > 55:
            raise ValueError(f"PAK path is too long: {name!r}")
        offset = HEADER_SIZE + len(data)
        data.extend(payload)
        directory.extend(struct.pack("<56sII", name.ljust(56, b"\0"), offset, len(payload)))
    directory_offset = HEADER_SIZE + len(data)
    return struct.pack("<4sII", b"PACK", directory_offset, len(directory)) + data + directory


def main() -> int:
    parser = argparse.ArgumentParser(description="Create labeled synthetic DOS engine, renderer, and audio fixtures")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    payload = build_pak()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != payload:
        args.output.write_bytes(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
