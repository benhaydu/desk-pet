import re
from typing import Literal

# has to match the presets in the firmware exactly
Expression = Literal["neutral", "happy", "sad", "angry", "sleepy", "suspicious"]

# has to match the scene names in the firmware's scene table
Scene = Literal["none", "sun", "cloud", "rain", "snow", "fog", "storm"]

# the day's main conditions (from weather.py) to the scene the board draws
SCENE_FOR = {
    "clear": "sun",
    "cloudy": "cloud",
    "rain": "rain",
    "snow": "snow",
    "storm": "storm",
    "fog": "fog",
}

# characters the board's font can't draw, swapped for plain ASCII ones
SCREEN_REPLACEMENTS = {
    "“": '"',    # left double quote
    "”": '"',    # right double quote
    "‘": "'",    # left single quote
    "’": "'",    # right single quote / apostrophe
    "—": "-",    # em dash
    "–": "-",    # en dash
    "…": "...",  # ellipsis
}

HEADLINE_MAX = 30

# words that can't end a line on their own, so a cut never leaves "...from a"
DANGLING = {"a", "an", "the", "to", "of", "from", "and", "or", "in", "on", "at", "for", "with"}

def pick_scene(weather, day: str) -> str:
    # no weather lookup, or weather we have no scene for, means no scene
    if not weather:
        return "none"
    return SCENE_FOR.get(weather[day]["main_conditions"], "none")


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

def fit_text(s: str, limit: int = HEADLINE_MAX) -> str:
    # Claude is asked to stay under the limit, this makes sure of it
    if len(s) <= limit:
        return s
    last_space = s.rfind(" ", 0, limit)
    if last_space == -1:
        return s[:limit]  # one long word, nothing better to do than cut it
    # cut at a space so no word gets chopped, and don't leave a dangling comma or dash
    cut = s[:last_space].rstrip(" ,;:-")
    # drop a dangling little word left at the end of the cut
    words = cut.split(" ")
    while len(words) > 1 and words[-1].lower() in DANGLING:
        words.pop()
    return " ".join(words).rstrip(" ,;:-")