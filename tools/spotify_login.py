import os
import secrets
import webbrowser
from pathlib import Path
from urllib.parse import urlencode, urlparse, parse_qs

import httpx
from dotenv import load_dotenv

# one-time setup: log in to Spotify in the browser and print a refresh token for server/.env
load_dotenv(Path(__file__).resolve().parent.parent / "server" / ".env")

REDIRECT_URI = "http://127.0.0.1:8888/callback"  # has to match the app's settings exactly
SCOPES = "user-read-currently-playing user-read-playback-state"

client_id = os.getenv("SPOTIFY_CLIENT_ID")
client_secret = os.getenv("SPOTIFY_CLIENT_SECRET")
if not client_id or not client_secret:
    raise SystemExit("SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET need to be in server/.env")

# a random value we expect back unchanged, so we know the redirect came from this login
state = secrets.token_urlsafe(16)

params = {
    "response_type": "code",
    "client_id": client_id,
    "scope": SCOPES,
    "redirect_uri": REDIRECT_URI,
    "state": state,
}
auth_url = f"https://accounts.spotify.com/authorize?{urlencode(params)}"

print("Opening Spotify in your browser. If it doesn't open, visit:")
print(auth_url)
webbrowser.open(auth_url)

# nothing listens on 127.0.0.1:8888, so the page won't load, but the code is in the address bar
redirected = input("After approving, paste the full URL from the address bar: ").strip()

# parse_qs puts every value in a list, since a key can appear more than once
query = parse_qs(urlparse(redirected).query)

if "error" in query:
    raise SystemExit(f"Spotify said no: {query['error'][0]}")
if query.get("state", [None])[0] != state:
    raise SystemExit("State doesn't match. Paste the URL from this run, not an older one")
code = query.get("code", [None])[0]
if not code:
    raise SystemExit("No code in that URL")

# the code alone isn't enough, the client secret proves it's this app asking
response = httpx.post(
    "https://accounts.spotify.com/api/token",
    data={
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
    },
    auth=(client_id, client_secret),
)
if response.status_code != 200:
    raise SystemExit(f"Token exchange failed ({response.status_code}): {response.text}")

tokens = response.json()
print()
print("Granted:", tokens.get("scope"))
print("Add this line to server/.env (and to Render's environment variables later):")
print(f"SPOTIFY_REFRESH_TOKEN={tokens['refresh_token']}")