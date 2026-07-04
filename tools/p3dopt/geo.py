"""Geometry chunk reader/writer for PC-format ("non-optimized", version==1)
Pure3D meshes, for the offline decimation pass.

Layout (payload = bytes after the 12-byte chunk header), all little-endian:
  MESH 0x10000:      pstr name, u32 version, u32 numPrimGroups, then children:
                       PRIMGROUP 0x10002, BOX 0x10003 (6 f32), SPHERE 0x10004
                       (4 f32), RENDERSTATUS 0x10017 (u32).
  PRIMGROUP 0x10002: u32 version, pstr shader, u32 primType, u32 vertexFormat,
                       u32 numVert, u32 numIndex, u32 numMatrix; then list children:
  POSITIONLIST 0x10005: u32 count + count*vec3(f32)
  NORMALLIST   0x10006: u32 count + count*vec3(f32)
  UVLIST       0x10007: u32 count + u32 channel + count*vec2(f32)   (one per channel)
  COLOURLIST   0x10008: u32 count + count*u32 (BGRA bytes on disk)
  INDEXLIST    0x1000A: u32 count + count*u32   (32-bit on disk!)
  skinned:     WEIGHTLIST 0x1000C / MATRIXLIST 0x1000B / MATRIXPALETTE 0x1000D
primType: 0=TRIANGLES(list) 1=TRISTRIP 2=LINES 3=LINESTRIP 4=POINTS.
"""
import struct
import numpy as np
from p3d import Reader, pack_pstr

MESH, SKIN = 0x10000, 0x10001
PRIMGROUP = 0x10002
BOX, SPHERE = 0x10003, 0x10004
POSITIONLIST, NORMALLIST, UVLIST, COLOURLIST = 0x10005, 0x10006, 0x10007, 0x10008
INDEXLIST = 0x1000A
WEIGHTLIST, MATRIXLIST, MATRIXPALETTE = 0x1000C, 0x1000B, 0x1000D
RENDERSTATUS = 0x10017

PRIM_TRIANGLES, PRIM_TRISTRIP = 0, 1
V_NORMAL, V_COLOUR = 1 << 4, 1 << 5


def parse_primgroup_header(payload):
    r = Reader(payload)
    version = r.u32(); shader = r.pstr(); primType = r.u32()
    vfmt = r.u32(); nvert = r.u32(); nindex = r.u32(); nmatrix = r.u32()
    return dict(version=version, shader=shader, primType=primType, vfmt=vfmt,
                nvert=nvert, nindex=nindex, nmatrix=nmatrix)


def build_primgroup_header(h):
    return (struct.pack("<I", h["version"]) + pack_pstr(h["shader"]) +
            struct.pack("<IIIII", h["primType"], h["vfmt"], h["nvert"],
                        h["nindex"], h["nmatrix"]))


def is_skinned(pg):
    return any(c.id in (WEIGHTLIST, MATRIXLIST, MATRIXPALETTE) for c in pg.children)


# ---- list codecs -------------------------------------------------------
def read_vec3_list(chunk):
    r = Reader(chunk.payload); n = r.u32()
    return np.frombuffer(chunk.payload, "<f4", 3 * n, 4).reshape(n, 3).copy()


def write_vec3_list(arr):
    return struct.pack("<I", len(arr)) + arr.astype("<f4").tobytes()


def read_uv_list(chunk):
    r = Reader(chunk.payload); n = r.u32(); ch = r.u32()
    uv = np.frombuffer(chunk.payload, "<f4", 2 * n, 8).reshape(n, 2).copy()
    return ch, uv


def write_uv_list(channel, uv):
    return struct.pack("<II", len(uv), channel) + uv.astype("<f4").tobytes()


def read_u32_list(chunk):
    r = Reader(chunk.payload); n = r.u32()
    return np.frombuffer(chunk.payload, "<u4", n, 4).copy()


def write_u32_list(arr):
    return struct.pack("<I", len(arr)) + arr.astype("<u4").tobytes()


def read_colour_list(chunk):
    # on-disk little-endian u32 => byte order B,G,R,A; return (n,4) uint8 BGRA.
    r = Reader(chunk.payload); n = r.u32()
    return np.frombuffer(chunk.payload, "u1", 4 * n, 4).reshape(n, 4).copy()


def write_colour_list(bgra):
    return struct.pack("<I", len(bgra)) + bgra.astype("u1").tobytes()


def strip_to_list(indices):
    """Convert a triangle-strip index array to a triangle-list (Nx3), honoring
    winding alternation and dropping degenerate triangles."""
    tris = []
    for i in range(len(indices) - 2):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        if a == b or b == c or a == c:
            continue  # degenerate (common strip stitch)
        tris.append((a, c, b) if (i & 1) else (a, b, c))
    return np.array(tris, np.int64) if tris else np.zeros((0, 3), np.int64)
