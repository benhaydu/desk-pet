import httpx

# Open-Meteo's WMO weather codes, in words Claude can write from
WEATHER_WORDS = {
    0: "clear",
    1: "mostly clear",
    2: "partly cloudy",
    3: "overcast",
    45: "fog",
    48: "fog",
    51: "light drizzle",
    53: "drizzle",
    55: "heavy drizzle",
    56: "freezing drizzle",
    57: "freezing drizzle",
    61: "light rain",
    63: "rain",
    65: "heavy rain",
    66: "freezing rain",
    67: "freezing rain",
    71: "light snow",
    73: "snow",
    75: "heavy snow",
    77: "snow grains",
    80: "light rain showers",
    81: "rain showers",
    82: "heavy rain showers",
    85: "snow showers",
    86: "heavy snow showers",
    95: "thunderstorm",
    96: "thunderstorm with hail",
    99: "thunderstorm with hail",
}


def _get_coords(city_name):
    # turns a place name into coordinates, None if it can't
    url = "https://geocoding-api.open-meteo.com/v1/search"
    query_params = {
        "name": city_name,
        "count": 1,
    }

    try:
        response = httpx.get(url, params=query_params, timeout=5)
        response.raise_for_status()
        data = response.json()

        if not data.get("results"):
            print(f"no matching location for {city_name}")
            return None

        location = data["results"][0]
        return {
            "name": location["name"],
            "admin1": location.get("admin1"),  # not every place has one
            "lat": location["latitude"],
            "lon": location["longitude"],
        }
    except (httpx.HTTPError, KeyError) as e:
        print(f"geocoding failed: {e}")
        return None


def _day(daily, i):
    # one day out of the daily lists, 0 is today and 1 is tomorrow
    code = daily["weather_code"][i]
    return {
        "high_c": daily["temperature_2m_max"][i],
        "low_c": daily["temperature_2m_min"][i],
        "rain_chance_pct": daily["precipitation_probability_max"][i],
        "code": code,
        "conditions": WEATHER_WORDS.get(code, "unknown"),
    }


def get_weather(location: str):
    coords = _get_coords(location)
    if coords is None:
        return {"error": f"couldn't find a place called {location}"}

    # some places don't have a province/state, so only add it if it's there
    place = coords["name"]
    if coords["admin1"]:
        place = f"{place}, {coords['admin1']}"

    url = "https://api.open-meteo.com/v1/forecast"
    query_params = {
        "latitude": coords["lat"],
        "longitude": coords["lon"],
        "current": "temperature_2m,weather_code",
        "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
        "timezone": "auto",
        "forecast_days": 2,
    }

    try:
        response = httpx.get(url, params=query_params, timeout=5)
        response.raise_for_status()
        data = response.json()

        current = data["current"]
        daily = data["daily"]
        now_code = current["weather_code"]

        return {
            "location": place,
            "now": {
                "temp_c": current["temperature_2m"],
                "code": now_code,
                "conditions": WEATHER_WORDS.get(now_code, "unknown"),
            },
            "today": _day(daily, 0),
            "tomorrow": _day(daily, 1),
        }
    except (httpx.HTTPError, KeyError, IndexError) as e:
        print(f"weather request failed: {e}")
        return {"error": "the weather service isn't answering right now"}


if __name__ == "__main__":
    # only runs with `python weather.py`, not when main.py imports this
    print(get_weather("Victoria, BC"))
    print(get_weather("Nanaimo, BC"))
    print(get_weather("Notarealplace"))