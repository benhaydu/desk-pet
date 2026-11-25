import os
import time

import httpx

from screen import clean_for_screen
from tempo import song_bpm

TOKEN_URL = "https://accounts.spotify.com/api/token"
NOW_PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing"
PLAYER_URL = "https://api.spotify.com/v1/me/player"
REFRESH_EARLY_S = 60  # get a new token a minute before it runs out, not after

CACHE_S = 3        # answer from the cache for this long, so polling can't hammer Spotify
TEXT_MAX = 60      # longest title or artist sent to the board, it fits them to the screen itself
ART_MIN_PX = 100   # smallest album image worth using

# kept between calls, so the token is only refreshed about once an hour
_access_token = None
_expires_at = 0.0       # time.monotonic() when it runs out
_refresh_token = None   # read from .env on first use, Spotify may hand back a new one

_cached = None     # the last summary
_cached_at = 0.0   # time.monotonic() when it was fetched
_art_urls = {}     # album id -> image URL, for /art/<id>

NOT_PLAYING = {"playing": False, "paused": False, "title": "", "artist": "", "progress_ms": 0, "duration_ms": 0, "art_id": "", "bpm": 0.0}

# what the board can ask for: the HTTP method and path under /me/player for each
PLAYER_ACTIONS = {
    "play": ("PUT", "/play"),
    "pause": ("PUT", "/pause"),
    "next": ("POST", "/next"),
    "previous": ("POST", "/previous"),
}


class SpotifyAuthError(Exception):
    """The refresh token stopped working: run tools/spotify_login.py again."""


def get_access_token() -> str:
    global _access_token, _expires_at, _refresh_token

    # still good for more than a minute: no need to ask Spotify
    if _access_token and time.monotonic() < _expires_at - REFRESH_EARLY_S:
        return _access_token

    # read here, not at import: main.py imports this before load_dotenv() runs
    client_id = os.getenv("SPOTIFY_CLIENT_ID")
    client_secret = os.getenv("SPOTIFY_CLIENT_SECRET")
    if _refresh_token is None:
        _refresh_token = os.getenv("SPOTIFY_REFRESH_TOKEN")
    if not (client_id and client_secret and _refresh_token):
        raise SpotifyAuthError("SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET and SPOTIFY_REFRESH_TOKEN need to be set")

    resp = httpx.post(
        TOKEN_URL,
        data={"grant_type": "refresh_token", "refresh_token": _refresh_token},
        auth=(client_id, client_secret),
        timeout=10,
    )

    if resp.status_code == 400:
        try:
            err_data = resp.json()
        except ValueError:
            err_data = {}
        # the refresh token has expired or been revoked, only logging in again fixes it
        if err_data.get("error") == "invalid_grant":
            raise SpotifyAuthError("Refresh token stopped working: run tools/spotify_login.py again")

    if resp.status_code != 200:
        raise RuntimeError(f"Spotify token refresh failed with status {resp.status_code}")

    token_data = resp.json()
    _access_token = token_data["access_token"]
    _expires_at = time.monotonic() + token_data["expires_in"]
    # Spotify sometimes sends a new refresh token, and then that's the one to use
    if "refresh_token" in token_data:
        _refresh_token = token_data["refresh_token"]

    print("spotify: refreshed the access token")  # while testing, to see the caching work
    return _access_token


def currently_playing() -> dict | None:
    resp = httpx.get(NOW_PLAYING_URL, headers={"Authorization": f"Bearer {get_access_token()}"}, timeout=10)

    if resp.status_code == 200:
        return resp.json()
    elif resp.status_code == 204:
        return None  # nothing playing, and Spotify sends no body at all
    else:
        raise RuntimeError(f"failed to get currently playing due to: {resp.status_code}")


def now_playing_summary() -> dict:
    global _cached, _cached_at

    age = time.monotonic() - _cached_at
    if _cached is not None and age < CACHE_S:
        summary = dict(_cached)  # a copy, so the cached one keeps its original progress
        if summary["playing"]:
            # the cached progress is `age` seconds old, so move it along to now
            summary["progress_ms"] = min(summary["progress_ms"] + int(age * 1000), summary["duration_ms"])
        return summary

    data = currently_playing()

    # nothing on, an ad (no item) or a podcast all count as not playing. A paused song keeps
    # its details, so the board can show it paused and a tap can start it again
    if data is None or data.get("item") is None or data.get("currently_playing_type") != "track":
        summary = dict(NOT_PLAYING)
    else:
        item = data["item"]
        album = item["album"]
        art_id = album["id"]
        isrc = (item.get("external_ids") or {}).get("isrc")
        first_artist = item["artists"][0]["name"] if item["artists"] else ""

        summary = {
            "playing": bool(data.get("is_playing")),
            "paused": not data.get("is_playing"),
            "title": clean_for_screen(item["name"])[:TEXT_MAX],
            "artist": clean_for_screen(", ".join(a["name"] for a in item["artists"]))[:TEXT_MAX],
            "progress_ms": data["progress_ms"],
            "duration_ms": item["duration_ms"],
            "art_id": art_id,
            # looked up once per song, so only the first check of a new song is a little slower
            "bpm": song_bpm(item["id"], isrc, item["name"], first_artist),
        }

        # Spotify gives a few sizes, biggest first (usually 640, 300 and 64).
        # The smallest that's still sharp is the least to download and shrink
        image = min(
            (i for i in album["images"] if i["width"] >= ART_MIN_PX),
            key=lambda i: i["width"],
            default=None,
        )
        if image is not None:
            _art_urls[art_id] = image["url"]

    _cached = summary
    _cached_at = time.monotonic()
    print("spotify: fetched now playing")  # while testing, to see the cache work
    return summary


def art_url(art_id: str) -> str | None:
    """The cover image for an album /now-playing has mentioned, or None if it hasn't."""
    return _art_urls.get(art_id)


def player(action: str) -> str:
    """Pause, play, skip or go back. Returns "ok", or "no_device" when Spotify isn't open
    anywhere, or "refused" when Spotify won't do it right now (pausing what's already paused)."""
    global _cached

    method, path = PLAYER_ACTIONS[action]
    resp = httpx.request(
        method,
        PLAYER_URL + path,
        headers={"Authorization": f"Bearer {get_access_token()}"},
        timeout=10,
    )
    # whatever happened, the cached now playing is out of date: the next check asks Spotify
    _cached = None

    if resp.status_code in (200, 202, 204):
        return "ok"
    if resp.status_code == 404:
        return "no_device"
    if resp.status_code == 403:
        return "refused"
    raise RuntimeError(f"player {action} failed with status {resp.status_code}")