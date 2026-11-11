from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

import httpx

# Should the briefing show a game that's almost a week away? Yes: the Saints only play
# once a week, so "next game Sunday" is still the useful thing to know. The label always
# includes the day and date, so it's never confused with tonight, and Claude decides how
# to phrase it.

RECENT = timedelta(days=3)  # how far back a game still counts as "last night"
LOCAL = ZoneInfo("America/Vancouver") # dates shown to Claude are in My time, not UTC

# the teams the briefing follows, one more line here to follow another
TEAMS = {
    "leafs": {"sport": "hockey", "league": "nhl", "team": "tor"},
    "saints": {"sport": "football", "league": "nfl", "team": "no"},
}


def _fetch_json(url: str) -> dict | None:
    # one ESPN request as a dict, None if anything goes wrong
    try:
        response = httpx.get(url, headers={"User-Agent": "steve-desk-pet/1.0"}, timeout=10)
        response.raise_for_status()
        return response.json()
    except (httpx.HTTPError, ValueError) as e:
        print(f"sports request failed: {url}: {type(e).__name__} {e}")
        return None


def _parse_time(raw: str) -> datetime:
    # ESPN times are UTC like "2026-11-09T00:00Z", older Pythons don't accept the Z
    return datetime.fromisoformat(raw.replace("Z", "+00:00"))


def _score(competitor: dict) -> int | None:
    # the schedule gives {"value": 4.0, "displayValue": "4"}, the scoreboard can give "4",
    # and a game that hasn't started has nothing yet
    raw = competitor.get("score")
    if isinstance(raw, dict):
        raw = raw.get("displayValue")
    if raw is None or raw == "":
        return None
    return int(float(raw)) # float first, so "4.0" works as well as "4"


def _status(event: dict) -> dict:
    # status usually sits on the competition, fall back to the event if it's not there
    competition = event["competitions"][0]
    return (competition.get("status") or event["status"])["type"]


def _split_sides(competition: dict, team: str) -> tuple[dict, dict]:
    # which competitor is our team and which is the opponent
    us = them = None
    for competitor in competition["competitors"]:
        if competitor["team"]["abbreviation"].upper() == team.upper():
            us = competitor
        else:
            them = competitor
    if us is None or them is None:
        raise KeyError(f"{team} isn't in this game") # caught like any other malformed event
    return us, them


def _local_label(when: datetime) -> str:
    # "Tue Nov 10, 4:00 PM" in Ben's time. %-I would drop the leading zero but
    # doesn't exist on Windows, so strip it by hand to work on both
    local = when.astimezone(LOCAL)
    hour = local.strftime("%I").lstrip("0")
    return f"{local.strftime('%a %b')} {local.day}, {hour}:{local.strftime('%M %p')}"


def _team_view(event: dict, team: str) -> dict:
    # one finished game, from our team's side
    competition = event["competitions"][0]
    us, them = _split_sides(competition, team)
    return {
        "team": us["team"]["abbreviation"],
        "opponent": them["team"]["abbreviation"],
        "home": us["homeAway"] == "home",
        "team_score": _score(us),
        "opponent_score": _score(them),
        "won": us["winner"],
        "detail": _status(event)["shortDetail"],
        # a 7 pm Toronto game is "tomorrow" in UTC, so convert before taking the date
        "date": _parse_time(event["date"]).astimezone(LOCAL).strftime("%Y-%m-%d"),
    }


def _last_game(sport: str, league: str, team: str) -> dict | None:
    # the most recent finished game in the last few days, or None
    url = f"https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/teams/{team}/schedule"
    data = _fetch_json(url)
    if data is None:
        return None

    now = datetime.now(timezone.utc)
    best_view = None
    best_time = None

    for event in data.get("events", []):
        try:
            if not _status(event)["completed"]:
                continue

            started = _parse_time(event["date"])
            if now - started > RECENT:
                continue

            if best_time is None or started > best_time:
                # convert now, so a game we can't read can't win and then fail later
                best_view = _team_view(event, team)
                best_time = started
        except (KeyError, IndexError, ValueError, TypeError) as e:
            # one odd event (postponed, missing a field) shouldn't lose the rest
            print(f"skipped an event: {type(e).__name__} {e}")
            continue

    return best_view


def _scoreboard(sport: str, league: str, day: str) -> list:
    # every game on one game day (day as "YYYYMMDD"), in all states, in our own shape.
    # callers pick what they want: the briefing keeps finished ones, a ticker keeps all
    url = f"https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/scoreboard?dates={day}"
    data = _fetch_json(url)
    if data is None:
        return []

    games = []
    for event in data.get("events", []):
        try:
            home = away = None
            for competitor in event["competitions"][0]["competitors"]:
                if competitor["homeAway"] == "home":
                    home = competitor
                else:
                    away = competitor
            if home is None or away is None:
                raise KeyError("game without a home and an away team")

            status = _status(event)
            games.append({
                "league": league.upper(),
                "away": away["team"]["abbreviation"],
                "away_score": _score(away),
                "home": home["team"]["abbreviation"],
                "home_score": _score(home),
                "state": status["state"],         # "pre", "in" or "post"
                "detail": status["shortDetail"],  # "Final", "2nd - 12:34", or a start time
            })
        except (KeyError, IndexError, ValueError, TypeError) as e:
            print(f"skipped a game: {type(e).__name__} {e}")
            continue

    return games


def _team_summary(sport: str, league: str, team: str) -> dict | None:
    # record, standing and next game, all from one schedule request
    url = f"https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/teams/{team}/schedule"
    data = _fetch_json(url)
    if data is None:
        return None

    # nice to have, so a missing one is None rather than losing the whole card
    info = data.get("team", {})

    now = datetime.now(timezone.utc)
    next_game = None
    next_time = None
    for event in data.get("events", []):
        try:
            if _status(event)["completed"]:
                continue
            started = _parse_time(event["date"])
            if started <= now:
                continue # under way right now, not "next"
            if next_time is None or started < next_time:
                us, them = _split_sides(event["competitions"][0], team)
                next_game = {
                    "opponent": them["team"]["abbreviation"],
                    "home": us["homeAway"] == "home",
                    "start": _local_label(started),
                }
                next_time = started
        except (KeyError, IndexError, ValueError, TypeError) as e:
            print(f"skipped an event: {type(e).__name__} {e}")
            continue

    return {
        "team": info.get("abbreviation", team.upper()),
        "record": info.get("recordSummary"),
        "standing": info.get("standingSummary"),
        "next_game": next_game,
    }


def get_sports() -> dict:
    return {name: _team_summary(**t) for name, t in TEAMS.items()}


if __name__ == "__main__":
    # only runs with `python sports.py`, not when main.py imports this
    import json
    print(json.dumps(get_sports(), indent=2))