import io
import struct

import httpx
from PIL import Image, ImageOps

from spotify import art_url

# the last cover sent, so a board that asks again (after a reboot, say) doesn't cost a download
_cached_key = None   # (art_id, size)
_cached_bytes = b""


def rgb565(r: int, g: int, b: int) -> int:
    # the screen's 16-bit colour: 5 bits red, 6 green (eyes are most sensitive to it), 5 blue
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def to_rgb565(image_bytes: bytes, size: int) -> bytes:
    """Any image file's bytes in, size x size raw RGB565 out, row by row, low byte first."""
    image = Image.open(io.BytesIO(image_bytes)).convert("RGB")
    # covers are nearly always square already, fit crops anything that isn't instead of squashing it
    image = ImageOps.fit(image, (size, size), Image.Resampling.LANCZOS)
    values = [rgb565(r, g, b) for r, g, b in image.getdata()]
    # "<" is low byte first, which is how the ESP32 keeps a uint16_t in memory,
    # so the board can read these bytes straight into a uint16_t array
    return struct.pack(f"<{len(values)}H", *values)


def album_art(art_id: str, size: int) -> bytes | None:
    """The cover for an album /now-playing has mentioned, or None for any other id."""
    global _cached_key, _cached_bytes

    if _cached_key == (art_id, size):
        return _cached_bytes

    # only URLs Spotify itself gave us, never one a request made up
    url = art_url(art_id)
    if url is None:
        return None

    resp = httpx.get(url, timeout=10)
    resp.raise_for_status()  # a failed download becomes an httpx.HTTPError, main turns it into a 502

    data = to_rgb565(resp.content, size)
    _cached_key = (art_id, size)
    _cached_bytes = data
    return data