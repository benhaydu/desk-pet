# Why the server owns the card order: the briefing should look the same every morning,
# and that's a rule, not a language task. Claude only writes words, so the worst it can do
# is an awkward line, never a missing or shuffled briefing.
# If every source fails, the briefing is just the greeting: Steve still says good morning,
# and the board doesn't need a special error case for the morning.

import json
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from typing import Literal
from zoneinfo import ZoneInfo

from pydantic import BaseModel

from news import get_news
from screen import Expression, Scene, clean_for_screen, fit_text, pick_scene
from sports import get_sports
from weather import get_weather

TIMEZONE = ZoneInfo("America/Vancouver")

# the news categories that get their own card, in the order they're shown
NEWS_CARDS = ["canada", "tech", "world"]

# what fits on the board: a face card has one headline, a panel has a title and a few lines
FACE_MAX = 30
PANEL_TITLE_MAX = 20
LINE_MAX = 24
MAX_LINES = 3

# Haiku's thinking varies a lot between runs, so leave plenty of room.
# 3000 fit locally and then ran out on Render with the same model
MAX_TOKENS = 6000

SYSTEM_PROMPT = """You are Steve, a small robot on Ben's desk in Victoria, BC. It's {time},
and this is Ben's weekday morning briefing. He reads it at a glance from across the desk,
a few seconds per card, so every word has to earn its place.

You're given a list of cards. Write the text for every card, matched by its id, and only
those ids. Each card has a kind:
- face: a title of at most 30 characters, and no lines.
- panel: a title of at most 20 characters, and at most 3 lines of at most 24 characters each.

What each card says:
- greeting: a warm, short good morning that uses the day, like "Happy Wednesday Ben!"
- weather: the day at a glance, like "13C, rain from 2pm" or "Sunny, high 18C".
- a team card: the next game (opponent and day/time), the record or standing, and the one
  team headline that matters most, if there is one. Skip old or trivial news.
- a news card: pick the single most important headline and compress it into the lines.
  The title is the category, like "Canada", "Tech" or "World".
- Headline lines are a shorter version of one real headline from that card's facts. Keep
  its meaning and don't add names, numbers or details that aren't in it.

Pick an expression for each card based on how you feel about it: happy is your default,
sad for bad news, suspicious for something odd.

The screen can only show plain text: no emoji, markdown or links, straight quotes and plain
hyphens, and write temperatures like 10C, not with a degree sign. Never make up facts:
only use what's in each card's facts.
"""


class CardText(BaseModel):
    id: str
    title: str
    lines: list[str]  # empty for face cards
    expression: Expression


class BriefingText(BaseModel):
    cards: list[CardText]


class BoardCard(BaseModel):
    id: str # "leafs", "news_tech"... the board picks the panel's colour and icon from it
    kind: Literal["face", "panel"]
    scene: Scene
    expression: Expression
    title: str
    lines: list[str]


class BriefingResponse(BaseModel):
    cards: list[BoardCard]


def _gather() -> dict:
    # all three sources at once, each already turns its own failures into empty values
    with ThreadPoolExecutor() as pool:
        weather_job = pool.submit(get_weather, "Victoria, BC")
        news_job = pool.submit(get_news)
        sports_job = pool.submit(get_sports)

        # each .result() waits for that job to finish and hands back its return value
        weather = weather_job.result()
        if "error" in weather:
            weather = None  # so everything after this has one rule: missing data is None
        return {"weather": weather, "news": news_job.result(), "sports": sports_job.result()}


def _skeleton(data: dict) -> list[dict]:
    # every card the briefing will show, in order, with the facts Claude writes from.
    # the server decides which cards exist and in what order, Claude only writes the words
    weather = data["weather"]
    news = data["news"]
    sports = data["sports"]

    # always first, so even a morning where every source fails still says hello
    cards = [{"id": "greeting", "kind": "face", "scene": "none", "facts": {}}]

    if weather is not None:
        cards.append({
            "id": "weather",
            "kind": "face",
            "scene": pick_scene(weather, "today"),
            "facts": {"today": weather["today"]},
        })

    # one card per team we follow, with that team's own news alongside
    for name, summary in sports.items():
        if summary is None:
            continue
        cards.append({
            "id": name,
            "kind": "panel",
            "scene": "none",
            "facts": {"summary": summary, "news": news.get(name, [])},
        })

    for category in NEWS_CARDS:
        headlines = news.get(category, [])
        if not headlines:
            continue
        cards.append({
            "id": f"news_{category}",
            "kind": "panel",
            "scene": "none",
            "facts": {"category": category, "headlines": headlines},
        })

    return cards


def _write_cards(client, model: str, cards: list[dict]) -> BriefingText:
    # one Claude call that writes the words for every card at once
    now = datetime.now(TIMEZONE).strftime("%A %B %d, %I:%M %p")
    response = client.messages.parse(
        model=model,
        max_tokens=MAX_TOKENS,
        system=SYSTEM_PROMPT.format(time=now),
        messages=[{"role": "user", "content": json.dumps(cards)}],
        output_format=BriefingText,
    )
    # before the None check, so the failed runs log their usage too
    print(f"briefing used {response.usage.output_tokens} of {MAX_TOKENS} output tokens")

    result = response.parsed_output
    if result is None:
        print("briefing: no structured output, stop_reason:", response.stop_reason)
        raise RuntimeError("model didn't return a usable briefing")
    return result


def _merge(cards: list[dict], text: BriefingText) -> list[BoardCard]:
    # structured output guarantees the shape, not the content, so check it like any input
    written = {c.id: c for c in text.cards}  # ids Claude made up just never get looked up

    merged = []
    for card in cards:
        t = written.get(card["id"])
        if t is None:
            # a card Claude skipped: the greeting gets a plain one, anything else is dropped,
            # a panel with no words would just be an empty box
            if card["id"] != "greeting":
                continue
            merged.append(BoardCard(id="greeting", kind="face", scene="none", expression="happy",
                                    title="Morning Ben!", lines=[]))
            continue

        if card["kind"] == "face":
            title = fit_text(clean_for_screen(t.title), FACE_MAX)
            lines = []
        else:
            title = fit_text(clean_for_screen(t.title), PANEL_TITLE_MAX)
            lines = [fit_text(clean_for_screen(line), LINE_MAX) for line in t.lines]
            lines = [line for line in lines if line][:MAX_LINES]  # no blanks, no extras

        merged.append(BoardCard(id=card["id"], kind=card["kind"], scene=card["scene"],
                                expression=t.expression, title=title, lines=lines))
    return merged


def build_briefing(client, model: str) -> BriefingResponse:
    # main.py passes in its Claude client, so this file doesn't need the API key itself
    cards = _skeleton(_gather())
    return BriefingResponse(cards=_merge(cards, _write_cards(client, model, cards)))


if __name__ == "__main__":
    # only runs with `python briefing.py`, not when main.py imports this
    import anthropic
    from dotenv import load_dotenv

    load_dotenv()
    print(build_briefing(anthropic.Anthropic(), "claude-haiku-5-5").model_dump_json(indent=2))