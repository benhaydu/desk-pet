"""Turns raw RGB565 bytes from /art back into a PNG, to check them by eye.

    python tools/view_art.py art.bin 120
"""
import struct
import sys

from PIL import Image

path, size = sys.argv[1], int(sys.argv[2])
data = open(path, "rb").read()
if len(data) != size * size * 2:
    raise SystemExit(f"expected {size * size * 2} bytes, got {len(data)}")

values = struct.unpack(f"<{size * size}H", data)  # low byte first, the same as the board reads it
# stretch each 5, 6 and 5-bit channel back out to 0-255
pixels = [(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in values]

image = Image.new("RGB", (size, size))
image.putdata(pixels)
# three times bigger with hard edges, so single pixels are easy to see
image.resize((size * 3, size * 3), Image.Resampling.NEAREST).save(path + ".png")
print("saved", path + ".png")