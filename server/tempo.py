import httpx

# Spotify stopped giving apps a song's tempo in 2024, so this asks Deezer's free public API,
# which has a "bpm" for many songs. No key needed. It's often 0 for songs it hasn't analysed,
# and then the board doesn't nod at all rather than nod at a made-up speed
DEEZER = "https://api.deezer.com"

_bpm_cache = {}  # Spotify track id -> bpm (0 = unknown), so each song is only looked up once


def _bpm_of(deezer_track: dict) -> float:
    bpm = deezer_track.get("bpm") or 0
    # a few songs come back with nonsense tempos, anything outside this is no help for bobbing
    return float(bpm) if 40 <= bpm <= 250 else 0.0


def _lookup(isrc: str | None, title: str, artist: str) -> float:
    # the ISRC is the song's barcode, the same on every service, so try that first
    if isrc:
        resp = httpx.get(f"{DEEZER}/track/isrc:{isrc}", timeout=5)
        if resp.status_code == 200:
            data = resp.json()
            if "error" not in data:
                bpm = _bpm_of(data)
                if bpm:
                    return bpm

    # otherwise search by artist and title, and ask for the full details of the best match
    # (search results don't include the bpm)
    resp = httpx.get(f"{DEEZER}/search", params={"q": f'artist:"{artist}" track:"{title}"'}, timeout=5)
    if resp.status_code != 200:
        return 0.0
    results = resp.json().get("data") or []
    if not results:
        return 0.0
    resp = httpx.get(f"{DEEZER}/track/{results[0]['id']}", timeout=5)
    if resp.status_code != 200:
        return 0.0
    return _bpm_of(resp.json())


def song_bpm(track_id: str, isrc: str | None, title: str, artist: str) -> float:
    """Beats per minute for a song, or 0 if nobody knows. Never raises: a tempo is a nice extra,
    so a Deezer problem must never break /now-playing."""
    if track_id in _bpm_cache:
        return _bpm_cache[track_id]
    try:
        bpm = _lookup(isrc, title, artist)
    except (httpx.HTTPError, ValueError, KeyError) as e:
        print("tempo lookup failed:", e)
        bpm = 0.0
    _bpm_cache[track_id] = bpm
    print(f"spotify: {title} is {bpm:g} bpm" if bpm else f"spotify: no tempo for {title}")
    return bpm