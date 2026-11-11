# Why a small shape instead of the raw XML: Claude only needs the headline, where it's from
# and when, and the raw feed is mostly tags and extra fields that cost tokens and add noise.
# Why 36 hours and not 24: the briefing runs at 9 am, so a strict 24 hours would drop
# last evening's stories on a slow morning, and Monday's briefing still catches late Sunday news.

from datetime import datetime, timedelta, timezone
from email.utils import parsedate_to_datetime
import xml.etree.ElementTree as ET

import httpx

FEEDS = {
    "canada": "https://www.cbc.ca/webfeed/rss/rss-canada",
    "tech": "https://www.cbc.ca/webfeed/rss/rss-technology",
    "world": "https://www.cbc.ca/webfeed/rss/rss-world",
    "leafs": "https://news.google.com/rss/search?q=%22Toronto+Maple+Leafs%22&hl=en-CA&gl=CA&ceid=CA:en",
    "saints": "https://news.google.com/rss/search?q=%22New+Orleans+Saints%22&hl=en-CA&gl=CA&ceid=CA:en",
}

MAX_ITEMS = 5
MAX_AGE = timedelta(hours=36)
DEFAULT_SOURCE = "CBC"  # CBC items have no <source>, Google News items do


def _parse_date(raw):
    # RSS dates look like "Mon, 09 Nov 2026 14:05:00 EST", None if missing or unreadable
    try:
        published = parsedate_to_datetime(raw)
    except (TypeError, ValueError):
        return None
    if published.tzinfo is None:
        published = published.replace(tzinfo=timezone.utc)  # no zone given, assume UTC
    return published


def _clean_title(title, source):
    # Google News titles end in " - Source Name", which is already in the source field
    suffix = f" - {source}"
    if title.endswith(suffix):
        return title[: -len(suffix)]
    return title


def _get_feed(url: str) -> list:
    # one feed as a short list of recent headlines, empty if anything goes wrong
    try:
        response = httpx.get(url, headers={"User-Agent": "steve-desk-pet/1.0"}, timeout=10)
        response.raise_for_status()
        root = ET.fromstring(response.text)
    except (httpx.HTTPError, ET.ParseError) as e:
        print(f"feed failed: {url}: {type(e).__name__} {e}")
        return []

    now = datetime.now(timezone.utc)
    items = []
    for item in root.findall("./channel/item"):
        title = item.findtext("title")
        published = _parse_date(item.findtext("pubDate"))

        # skip just this item if it's missing something or too old
        if not title or published is None or now - published > MAX_AGE:
            continue

        source = item.findtext("source") or DEFAULT_SOURCE
        items.append({
            "title": _clean_title(title.strip(), source),
            "source": source,
            "published": published.strftime("%Y-%m-%dT%H:%M"),
        })

        # filter first, then stop at the limit, so old items don't use up the slots
        if len(items) == MAX_ITEMS:
            break

    return items


def get_news() -> dict:
    return {name: _get_feed(url) for name, url in FEEDS.items()}


if __name__ == "__main__":
    # only runs with `python news.py`, not when main.py imports this
    import json
    print(json.dumps(get_news(), indent=2))