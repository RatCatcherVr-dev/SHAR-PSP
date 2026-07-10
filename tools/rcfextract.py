#!/usr/bin/env python3
# Minimal RADCORE CEMENT LIBRARY (.rcf) extractor.
# Directory is a flat hash table (HFE: hash,u32 offset,u32 size) keyed by the
# X31 case-insensitive hash of the filename (radMakeCaseInsensitiveKey32).
import struct, sys, os

def x31_ci(s: str) -> int:
    k = 0
    for ch in s:
        c = ord(ch)
        if c < ord('a'):
            c = c + (ord('a') - ord('A'))   # replicate radcore's "lowercase" (adds 32 to <'a')
        k = ((k << 5) - k + c) & 0xFFFFFFFF
    return k

def load_dir(path):
    with open(path, 'rb') as f:
        data = f.read(64)
        # radCFFileInfo: char ident[32]; u8 maj,min,big,valid; u32 align,padnet,headerStart
        header_start = struct.unpack_from('<I', data, 32 + 4 + 4 + 4)[0]
        with open(path, 'rb') as f2:
            f2.seek(header_start)
            hdr = f2.read(16)
            num_files, detailed, first_file, reserved = struct.unpack('<IIII', hdr)
            hfe = f2.read(12 * num_files)
    entries = {}
    for i in range(num_files):
        h, off, size = struct.unpack_from('<III', hfe, i * 12)
        entries[h] = (off, size)
    return entries

def extract(rcf, name, out):
    entries = load_dir(rcf)
    for cand in (name.replace('/', '\\'), name.replace('\\', '/')):
        h = x31_ci(cand)
        if h in entries:
            off, size = entries[h]
            with open(rcf, 'rb') as f:
                f.seek(off); blob = f.read(size)
            with open(out, 'wb') as f:
                f.write(blob)
            print(f"OK  {name}  ->  {out}  ({size} bytes, magic={blob[:4]!r}, sep={cand[5] if len(cand)>5 else '?'!r})")
            return True
    print(f"MISS {name} (hash not in {os.path.basename(rcf)})")
    return False

if __name__ == '__main__':
    rcf, name, out = sys.argv[1], sys.argv[2], sys.argv[3]
    sys.exit(0 if extract(rcf, name, out) else 1)
