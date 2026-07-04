"""Minimal Pure3D (.p3d) chunk-tree codec for the SRR2 engine.

On-disk layout (little-endian for PC-authored files):
  file magic u32  (0xFF443350 "P3D\\xff", uncompressed LE)
  then one root chunk = { id=magic, dataLength, chunkLength } whose subchunks
  are all the top-level chunks.

Chunk header = 3x u32:
  id           chunk type
  dataLength   header(12) + own payload, EXCLUDING subchunks
  chunkLength  header(12) + own payload + all subchunks (full span)
A chunk has subchunks iff chunkLength > dataLength; they start at
startPos + dataLength and run to startPos + chunkLength.

Strings are u8 length + that many raw bytes (no NUL on disk).

This codec is intentionally structure-only: every chunk keeps its own payload
as raw bytes, so unknown chunks round-trip byte-exact. Callers decode/mutate
the payloads of the chunk types they care about, then re-serialize; sizes are
recomputed bottom-up so edits can't desync the length fields.
"""
import struct

HEADER_SIZE = 12
MAGIC_LE = 0xFF443350          # "P3D\xff"
MAGIC_Z = 0x5A443350           # "P3DZ" compressed (unsupported here)
MAGIC_SWAP = 0x503344FF
MAGIC_Z_SWAP = 0x5033445A


class Chunk:
    __slots__ = ("id", "payload", "children")

    def __init__(self, cid, payload=b"", children=None):
        self.id = cid
        self.payload = payload            # own data AFTER the 12-byte header
        self.children = children if children is not None else []

    # ---- serialization -------------------------------------------------
    def serialized_len(self):
        body = len(self.payload) + sum(c.serialized_len() for c in self.children)
        return HEADER_SIZE + body

    def data_length(self):
        return HEADER_SIZE + len(self.payload)

    def chunk_length(self):
        return self.data_length() + sum(c.serialized_len() for c in self.children)

    def write(self, out):
        out += struct.pack("<III", self.id, self.data_length(), self.chunk_length())
        out += self.payload
        for c in self.children:
            c.write(out)

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def walk_parent(self, parent=None):
        """Yield (parent, chunk) for every chunk, so callers know a chunk's
        container (e.g. whether an IMAGE sits under a TEXTURE = a mip set)."""
        yield parent, self
        for c in self.children:
            yield from c.walk_parent(self)

    def find(self, cid):
        return [c for c in self.children if c.id == cid]


def _parse_chunk(buf, pos):
    cid, data_len, chunk_len = struct.unpack_from("<III", buf, pos)
    if data_len < HEADER_SIZE or chunk_len < data_len:
        raise ValueError(f"bad chunk @ {pos}: id={cid:#x} data={data_len} chunk={chunk_len}")
    payload = buf[pos + HEADER_SIZE: pos + data_len]
    ch = Chunk(cid, payload)
    child_pos = pos + data_len
    end = pos + chunk_len
    while child_pos < end:
        sub, child_pos = _parse_chunk(buf, child_pos)
        ch.children.append(sub)
    if child_pos != end:
        raise ValueError(f"child overrun @ {pos}: {child_pos} != {end}")
    return ch, pos + chunk_len


def load(path):
    """Parse a .p3d into a root Chunk. Raises on compressed/big-endian files."""
    with open(path, "rb") as f:
        buf = f.read()
    magic = struct.unpack_from("<I", buf, 0)[0]
    if magic in (MAGIC_Z, MAGIC_Z_SWAP):
        raise ValueError(f"{path}: compressed P3DZ not supported (decompress first)")
    if magic in (MAGIC_SWAP,):
        raise ValueError(f"{path}: big-endian SWAP file not supported")
    if magic != MAGIC_LE:
        raise ValueError(f"{path}: not a P3D file (magic {magic:#x})")
    # The 4-byte magic IS the root chunk's id; the two size u32 follow it.
    root, end = _parse_chunk(buf, 0)
    if end != len(buf):
        raise ValueError(f"{path}: trailing bytes {end} != {len(buf)}")
    return root


def dump(root):
    out = bytearray()
    root.write(out)
    return bytes(out)


def save(root, path):
    with open(path, "wb") as f:
        f.write(dump(root))


# ---- payload field readers/writers (u8-len strings, u32 fields) --------
class Reader:
    def __init__(self, buf):
        self.b = buf
        self.p = 0

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v

    def pstr(self):
        n = self.b[self.p]; self.p += 1
        s = self.b[self.p:self.p + n]; self.p += n
        return s

    def rest(self):
        return self.b[self.p:]


def pack_pstr(s):
    if isinstance(s, str):
        s = s.encode("latin-1")
    assert len(s) < 256
    return bytes([len(s)]) + s


if __name__ == "__main__":
    import sys
    # Round-trip identity self-test: parse then re-serialize must be byte-exact.
    ok = True
    for path in sys.argv[1:]:
        try:
            root = load(path)
            with open(path, "rb") as f:
                orig = f.read()
            rt = dump(root)
            same = rt == orig
            nchunks = sum(1 for _ in root.walk())
            print(f"{'OK ' if same else 'MISMATCH'} {path}  chunks={nchunks} "
                  f"orig={len(orig)} roundtrip={len(rt)}")
            ok = ok and same
        except Exception as e:
            print(f"ERR {path}: {e}")
            ok = False
    sys.exit(0 if ok else 1)
