import httpx
from collections import Counter

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
WAKING_START = 5    # 5 am
WAKING_END = 24     # midnight, the end of the day

def _category(code):
    if code in (0, 1):
        return "clear"
    elif code in (2, 3):
        return "cloudy"
    elif code in (45, 48):
        return "fog"
    elif(51 <= code <= 67 or 80 <= code <= 82):
        return "rain"
    elif(71 <= code <= 77 or code in (85, 86)):
        return "snow"
    elif(95 <= code <= 99):
        return "storm"

    return "unknown"

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


def _day(daily, hourly, i):
    # one day out of the daily lists, 0 is today and 1 is tomorrow
    worst_code = daily["weather_code"][i]
    date = daily["time"][i]
    hourly_weather = []
    rain_likely_hours = []
    for t, hour_code, rain_pct in zip(hourly["time"], hourly["weather_code"], hourly["precipitation_probability"]):
        if t[:10] != date:
            continue
        hour = int(t[11:13])
        if not (WAKING_START <= hour < WAKING_END):
            continue
        hourly_weather.append(_category(hour_code)) 
        if rain_pct is not None and rain_pct >= 50:
            rain_likely_hours.append(t[11:16]) 

    if hourly_weather:
        main_conditions = Counter(hourly_weather).most_common(1)[0][0]
    else:
        main_conditions = "unknown"

    return {
        "high_c": daily["temperature_2m_max"][i],
        "low_c": daily["temperature_2m_min"][i],
        "rain_chance_pct": daily["precipitation_probability_max"][i],
        "main_conditions": main_conditions,
        "rain_likely_hours": rain_likely_hours,
        "worst_code": worst_code,
        "worst_conditions": WEATHER_WORDS.get(worst_code, "unknown"),
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
        "hourly": "weather_code,precipitation_probability",
        "timezone": "auto",
        "forecast_days": 2,
    }

    try:
        response = httpx.get(url, params=query_params, timeout=5)
        response.raise_for_status()
        data = response.json()

        current = data["current"]
        daily = data["daily"]
        hourly = data["hourly"]
        now_code = current["weather_code"]

        return {
            "location": place,
            "now": {
                "temp_c": current["temperature_2m"],
                "code": now_code,
                "conditions": WEATHER_WORDS.get(now_code, "unknown"),
            },
            "today": _day(daily, hourly, 0),
            "tomorrow": _day(daily, hourly,  1),
        }
    except (httpx.HTTPError, KeyError, IndexError) as e:
        print(f"weather request failed: {e}")
        return {"error": "the weather service isn't answering right now"}


if __name__ == "__main__":
    # only runs with `python weather.py`, not when main.py imports this
    print(get_weather("Victoria, BC"))
    print(get_weather("Nanaimo, BC"))
    print(get_weather("Notarealplace"))