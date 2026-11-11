import re


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

SCENE_FOR = {"clear": "sun", "cloudy": "cloud", 
             "rain": "rain", "snow": "snow", 
             "storm": "storm", "fog": "fog",}


HEADLINE_MAX = 30

def pick_scene(weather, day: str) -> str:
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

def fit_headline(s: str, limit: int = HEADLINE_MAX) -> str:
    if len(s) <= HEADLINE_MAX:
        return s
    last_space = s.rfind(" ", 0, HEADLINE_MAX)
    if last_space == -1:
        return s[0:HEADLINE_MAX]
    
    return s[0: last_space]

