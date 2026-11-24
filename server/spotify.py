import os
import time

import httpx

TOKEN_URL = "https://accounts.spotify.com/api/token"
NOW_PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing"
REFRESH_EARLY_S = 60  # get a new token a minute before it runs out, not after

# kept between calls, so the token is only refreshed about once an hour
_access_token = None
_expires_at = 0.0       # time.monotonic() when it runs out
_refresh_token = None   # read from .env on first use, Spotify may hand back a new one


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
        return None
   else:
       raise RuntimeError(f"failed to get currently playing due to: {resp.status_code}")
   
