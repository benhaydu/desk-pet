import os
from collections import Counter

import httpx

FORECAST_URL = "https://api.weatherapi.com/v1/forecast.json"

WAKING_START = 5    # 5 am
WAKING_END = 24     # midnight, the end of the day

# WeatherAPI.com condition codes, grouped into the categories scenes are picked from.
# The "possible" codes (1063 patchy rain possible etc.) count as cloudy on purpose:
# WeatherAPI uses them a lot, and the real rain risk comes from chance_of_rain instead.
CLEAR_CODES = {1000}
CLOUDY_CODES = {1003, 1006, 1009, 1063, 1066, 1069, 1072}
FOG_CODES = {1030, 1135, 1147}
RAIN_CODES = {
    1150, 1153, 1168, 1171,                          # drizzle
    1180, 1183, 1186, 1189, 1192, 1195, 1198, 1201,  # rain and freezing rain
    1240, 1243, 1246,                                # rain showers
}
SNOW_CODES = {
    1114, 1117,                                      # blowing snow, blizzard
    1204, 1207, 1249, 1252,                          # sleet
    1210, 1213, 1216, 1219, 1222, 1225,              # snow
    1237, 1261, 1264,                                # ice pellets
    1255, 1258,                                      # snow showers
}
STORM_CODES = {1087, 1273, 1276, 1279, 1282}

# WeatherAPI's "no location matches" error code
LOCATION_NOT_FOUND = 1006


def _category(code):
    if code in CLEAR_CODES:
        return "clear"
    if code in CLOUDY_CODES:
        return "cloudy"
    if code in FOG_CODES:
        return "fog"
    if code in RAIN_CODES:
        return "rain"
    if code in SNOW_CODES:
        return "snow"
    if code in STORM_CODES:
        return "storm"
    return "unknown"


def _words(condition):
    # WeatherAPI's text is like "Patchy rain nearby", lowercase reads better in a reply
    return condition["text"].strip().lower()


def _day(forecast_day):
    # one entry of forecast.forecastday, the hours are already grouped by day
    day = forecast_day["day"]

    hourly_weather = []
    rain_likely_hours = []
    for hour in forecast_day["hour"]:
        # time looks like "2026-11-05 14:00"
        h = int(hour["time"][11:13])
        if not (WAKING_START <= h < WAKING_END):
            continue
        hourly_weather.append(_category(hour["condition"]["code"]))
        rain_pct = hour.get("chance_of_rain")
        if rain_pct is not None and rain_pct >= 50:
            rain_likely_hours.append(hour["time"][11:16])

    if hourly_weather:
        main_conditions = Counter(hourly_weather).most_common(1)[0][0]
    else:
        main_conditions = "unknown"

    return {
        "high_c": day["maxtemp_c"],
        "low_c": day["mintemp_c"],
        "rain_chance_pct": day["daily_chance_of_rain"],
        "main_conditions": main_conditions,
        "rain_likely_hours": rain_likely_hours,
        "summary": _words(day["condition"]),
    }


def get_weather(location: str):
    key = os.environ.get("WEATHER_API_KEY")
    if not key:
        print("WEATHER_API_KEY isn't set")
        return {"error": "the weather service isn't set up"}

    query_params = {
        "key": key,
        "q": location,
        "days": 2,
        "aqi": "no",
        "alerts": "no",
    }

    try:
        response = httpx.get(FORECAST_URL, params=query_params, timeout=10)

        # WeatherAPI answers a bad place name with a 400 and its own error code
        if response.status_code == 400:
            code = response.json().get("error", {}).get("code")
            if code == LOCATION_NOT_FOUND:
                return {"error": f"couldn't find a place called {location}"}

        response.raise_for_status()
        data = response.json()

        place = data["location"]
        current = data["current"]
        days = data["forecast"]["forecastday"]

        name = place["name"]
        if place.get("region"):
            name = f"{name}, {place['region']}"

        return {
            "location": name,
            "now": {
                "temp_c": current["temp_c"],
                "conditions": _words(current["condition"]),
            },
            "today": _day(days[0]),
            "tomorrow": _day(days[1]),
        }
    except httpx.HTTPStatusError as e:
        # don't print e itself, its message has the full URL and the key is in that URL
        print(f"weather request failed: HTTP {e.response.status_code} {e.response.text[:200]}")
        return {"error": "the weather service isn't answering right now"}
    except httpx.RequestError as e:
        print(f"weather request failed: {type(e).__name__}")
        return {"error": "the weather service isn't answering right now"}
    except (KeyError, IndexError, ValueError) as e:
        # the reply wasn't JSON, or didn't have the shape we expect
        print(f"weather reply was unexpected: {type(e).__name__} {e}")
        return {"error": "the weather service isn't answering right now"}


if __name__ == "__main__":
    # only runs with `python weather.py`, not when main.py imports this
    from dotenv import load_dotenv

    load_dotenv()
    print(get_weather("Victoria, BC"))
    print(get_weather("Nanaimo, BC"))
    print(get_weather("Notarealplace"))