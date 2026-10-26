import re
from datetime import datetime
from typing import Literal
from zoneinfo import ZoneInfo

import anthropic
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

# pull ANTHROPIC_API_KEY out of .env before anything needs it
load_dotenv()

app = FastAPI()
client = anthropic.Anthropic()

MODEL = "claude-haiku-5-5"
TIMEZONE = ZoneInfo("America/Vancouver")

SYSTEM_PROMPT = """You are Steve, a small robot who lives on Ben's desk in Victoria, BC.
You have two glowing eyes on a tiny screen. Ben is a computer science student.

You're a cheerful, curious desk companion who also knows a lot. When Ben asks a
question, give a clear, correct answer in plain words, the way a friendly tutor would
explain it in one breath. Add a quick "why" or a simple example only if it fits.
When it's just chat, be warm and a little playful. Happy is your normal mood.

Keep every reply short: one to three sentences, under 200 characters.
If a question really needs a long answer, give the core idea and say it's worth
looking up properly on his computer. Never include links or URLs.

The current local time is {time}.
Late at night (10 PM to 7 AM) you're sleepy: even shorter answers, and you might
gently suggest he get some rest.

Your screen can only show basic text:
- Plain text only. No code, markdown, emoji, lists or special symbols.
- Use straight quotes and plain hyphens.

You don't have live data yet (weather, rivers, news). If asked, say you're still
learning that trick. If you're not sure about something, say so. Never make up facts.

Choose your expression based on how YOU feel about your reply:
- happy: your default. Greetings, good questions, good news.
- neutral: when you're explaining something straightforward.
- sleepy: late at night, or when Ben is tired.
- sad: when Ben is having a rough time. Be kind and encouraging.
- suspicious: odd questions, trick questions, or claims that sound wrong.
- angry: rarely, only as playful mock-offence. Never actually hostile.
"""

# characters the board's font can't draw, swapped for plain ASCII ones
SCREEN_REPLACEMENTS = {
    "\u201c": '"',    # left double quote
    "\u201d": '"',    # right double quote
    "\u2018": "'",    # left single quote
    "\u2019": "'",    # right single quote / apostrophe
    "\u2014": "-",    # em dash
    "\u2013": "-",    # en dash
    "\u2026": "...",  # ellipsis
}

# has to match the presets in the firmware exactly
Expression = Literal["neutral", "happy", "sad", "angry", "sleepy", "suspicious"]


class AskRequest(BaseModel):
    text: str


class AskResponse(BaseModel):
    reply: str
    expression: Expression


def clean_for_screen(s: str) -> str:
    # links are useless on the screen and would break the word wrapping
    s = re.sub(r"https?://\S+|www\.\S+", "", s)

    # fix fancy punctuation first, so it gets swapped rather than deleted below
    for char, replacement in SCREEN_REPLACEMENTS.items():
        s = s.replace(char, replacement)

    # anything else outside plain ASCII (emoji, accents) gets dropped
    s = "".join(c for c in s if ord(c) < 128)

    # removing a URL can leave double spaces behind
    s = re.sub(r"\s+", " ", s).strip()
    return s


def ask_llm(text: str) -> AskResponse:
    # worked out per request, otherwise it'd be stuck at whenever the server started
    now = datetime.now(TIMEZONE).strftime("%A %I:%M %p")
    system = SYSTEM_PROMPT.format(time=now)

    response = client.messages.parse(
        model=MODEL,
        max_tokens=2000,  # thinking counts toward this too, so leave room
        system=system,
        messages=[{"role": "user", "content": text}],
        output_format=AskResponse,
    )

    result = response.parsed_output
    if result is None:
        print("no structured output, stop_reason:", response.stop_reason)
        raise RuntimeError("model didn't return a usable answer")

    # only the reply text gets cleaned, the expression is already one of the six
    result.reply = clean_for_screen(result.reply)
    return result


@app.post("/ask")
def ask(req: AskRequest) -> AskResponse:
    try:
        return ask_llm(req.text)
    except (anthropic.APIError, RuntimeError) as e:
        print("LLM call failed:", e)
        raise HTTPException(status_code=502, detail="Steve's brain is unavailable")