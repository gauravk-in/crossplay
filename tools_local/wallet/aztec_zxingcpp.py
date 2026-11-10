"""Reads each symbol aztec_dump printed back with zxing-cpp and checks it
holds the bytes that were encoded. zxing-cpp rather than the ZXing build the
phone page ships: that one cannot read its own symbols past 22 layers.

    python3 -m pip install zxing-cpp pillow
"""
import sys

import zxingcpp
from PIL import Image

failed = 0
total = 0
for line in open(sys.argv[1]):
    hexed, size, *rest = line.rstrip("\n").split("\t")
    want = bytes.fromhex(hexed)
    if size == "FAIL":
        print("FAIL encode", len(want), "bytes")
        failed += 1
        continue
    size = int(size)
    modules = rest[0]
    total += 1
    px, quiet = 3, 4
    side = (size + 2 * quiet) * px
    img = Image.new("L", (side, side), 255)
    pix = img.load()
    for y in range(size):
        for x in range(size):
            if modules[y * size + x] == "1":
                for dy in range(px):
                    for dx in range(px):
                        pix[(x + quiet) * px + dx, (y + quiet) * px + dy] = 0
    hits = zxingcpp.read_barcodes(img, formats=zxingcpp.BarcodeFormat.Aztec)
    got = hits[0].bytes if hits else None
    ok = got == want
    failed += 0 if ok else 1
    if not ok:
        print(f"BAD  {size}x{size}  {want[:40]!r}")
print("zxing-cpp: " + (f"{failed} failed" if failed else f"all {total} read back"))
sys.exit(1 if failed else 0)
