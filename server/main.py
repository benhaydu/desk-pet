import os
import secrets
import json
from datetime import datetime
from typing import Literal
from zoneinfo import ZoneInfo

import anthropic
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, Header
from pydantic import BaseModel, Field

from weather import get_weather
from briefing import BriefingResponse, build_briefing
from screen import Expression, Scene, clean_for_screen, fit_text, pick_scene

# pull the API keys out of .env before anything needs them
load_dotenv()
steve_key = os.environ["STEVE_KEY"]
if not steve_key:
    raise RuntimeError("STEVE_KEY is empty, refusing to start")
weather_key = os.environ["WEATHER_API_KEY"]
if not weather_key:
    raise RuntimeError("WEATHER_API_KEY is empty, refusing to start")


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

Every answer has two parts:
- headline: the only text Ben sees on your screen, so it has to make sense on its
  own. A few words, at most 30 characters, no full sentence, no ending period.
  Examples: "Umbrella tomorrow", "10C and cloudy", "Locks for shared data", "Hey Ben!"
- reply: the full answer, one to three sentences, under 200 characters. If a question
  really needs a long answer, give the core idea and say it's worth looking up
  properly on his computer. Never include links or URLs.

The current local time is {time}.
Late at night (11 PM to 7:30 AM) Ben woke you up to ask: you're sleepy, so give even
shorter answers, and you might gently suggest he get some rest.

Your screen can only show basic text:
- Plain text only. No code, markdown, emoji, lists or special symbols.
- Use straight quotes and plain hyphens. Write temperatures like 10C, not with a
  degree sign.

You can check live weather with your tool. You don't have river data, or news outside
the morning briefing, yet. If asked about those, say you're still learning that trick.
If you're not sure about something, say so. Never make up facts.

Choose your expression based on how YOU feel about your reply:
- happy: your default. Greetings, good questions, good news.
- neutral: when you're explaining something straightforward.
- sleepy: late at night, or when Ben is tired.
- sad: when Ben is having a rough time. Be kind and encouraging.
- suspicious: odd questions, trick questions, or claims that sound wrong.
- angry: rarely, only as playful mock-offence. Never actually hostile.
"""

# what Claude can ask the server to run, the descriptions are how it decides when
TOOLS = [
    {
        "name": "get_weather",
        "description": (
            "Gets live weather for a place: current temperature and conditions, plus "
            "today's and tomorrow's high, low, rain chance and conditions. Use it for "
            "any question about the weather, temperature, rain, snow, or what to wear "
            "or bring outside, even if the word 'weather' isn't used. Don't guess the "
            "weather without it."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "location": {
                    "type": "string",
                    "description": (
                        "City plus province or state, like 'Victoria, BC' or "
                        "'Seattle, WA'. If Ben doesn't name a place, use 'Victoria, BC'."
                    ),
                },
            },
            "required": ["location"],
        },
    }
]

MAX_TOOL_ROUNDS = 3


class SteveResponse(BaseModel):
    reply: str
    expression: Expression
    scene: Scene
    headline: str
    data: dict = {}


class AskRequest(BaseModel):
    text: str


class AskResponse(BaseModel):
    reply: str
    headline: str = Field(description="What Ben sees on screen: a few words, at most 30 characters")
    expression: Expression
    weather_day: Literal["today", "tomorrow"] = Field(
        description="Which day Ben's question is about. 'today' if it isn't about the weather."
    )


def ask_llm(text: str) -> SteveResponse:
    # worked out per request, otherwise it'd be stuck at whenever the server started
    now = datetime.now(TIMEZONE).strftime("%A %I:%M %p")
    system = SYSTEM_PROMPT.format(time=now)
    messages = [{"role": "user", "content": text}]
    weather = None  # stays None unless a weather lookup works

    for _ in range(MAX_TOOL_ROUNDS):
        response = client.messages.parse(
            model=MODEL,
            max_tokens=2000,  # thinking counts toward this too, so leave room
            system=system,
            messages=messages,
            output_format=AskResponse,
            tools=TOOLS,
        )

        # anything other than a tool request means Claude has its answer
        if response.stop_reason != "tool_use":
            break

        messages.append({"role": "assistant", "content": response.content})

        tool_results = []
        for block in response.content:
            if block.type == "tool_use":
                print(f"tool call: {block.name} {block.input}")

                if block.name == "get_weather":
                    output = get_weather(block.input["location"])
                    if "error" not in output:
                        weather = output
                else:
                    # every tool_use needs a result, even one we don't know
                    output = {"error": f"unknown tool {block.name}"}

                tool_results.append({
                    "type": "tool_result",
                    "tool_use_id": block.id,
                    "content": json.dumps(output),
                    "is_error": "error" in output,
                })

        messages.append({"role": "user", "content": tool_results})

    result = response.parsed_output
    if result is None:
        print("no structured output, stop_reason:", response.stop_reason)
        raise RuntimeError("model didn't return a usable answer")

    day = result.weather_day
    data = {}
    if weather:
        data = {
            "high_c": weather[day]["high_c"],
            "low_c": weather[day]["low_c"],
        }
        # "right now" only means something for today
        if day == "today":
            data["temp_c"] = weather["now"]["temp_c"]

    # Claude wrote the reply and picked the mood, the scene comes from the data
    return SteveResponse(
        reply=clean_for_screen(result.reply),
        expression=result.expression,
        scene=pick_scene(weather, day),
        headline=fit_text(clean_for_screen(result.headline)),
        data=data,
    )


@app.post("/ask")
def ask(req: AskRequest, x_steve_key: str = Header(default="")) -> SteveResponse:
    if not secrets.compare_digest(x_steve_key, steve_key):
        raise HTTPException(status_code=401, detail="Unauthorized")

    try:
        return ask_llm(req.text)
    except (anthropic.APIError, RuntimeError) as e:
        print("LLM call failed:", e)
        raise HTTPException(status_code=502, detail="Steve's brain is unavailable")


@app.get("/briefing")
def get_briefing(x_steve_key: str = Header(default="")) -> BriefingResponse:
    if not secrets.compare_digest(x_steve_key, steve_key):
        raise HTTPException(status_code=401, detail="Unauthorized")

    try:
        return build_briefing(client, MODEL)
    except (anthropic.APIError, RuntimeError) as e:
        print("briefing failed:", e)
        raise HTTPException(status_code=502, detail="Steve's brain is unavailable")