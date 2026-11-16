"""Vietnamese weather tool using Open-Meteo (free, no API key).

Template for adding new assistant features: drop a .py file in server/plugins/,
register it with @register_function, and list its name under
Intent.function_call.functions in config.template.yaml.
"""

import os

import requests

from plugins_func.register import Action, ActionResponse, ToolType, register_function

HOME_NAME = os.environ.get("HOME_CITY", "Hà Nội")
HOME_LAT = float(os.environ.get("HOME_LAT", "21.0285"))
HOME_LON = float(os.environ.get("HOME_LON", "105.8542"))

WMO_VI = {
    0: "trời quang", 1: "ít mây", 2: "có mây", 3: "nhiều mây", 45: "sương mù", 48: "sương mù đóng băng",
    51: "mưa phùn nhẹ", 53: "mưa phùn", 55: "mưa phùn dày", 61: "mưa nhẹ", 63: "mưa vừa", 65: "mưa to",
    80: "mưa rào nhẹ", 81: "mưa rào", 82: "mưa rào rất to", 95: "dông", 96: "dông kèm mưa đá", 99: "dông mạnh kèm mưa đá",
}

THOI_TIET_DESC = {
    "type": "function",
    "function": {
        "name": "thoi_tiet",
        "description": (
            "Xem thời tiết hiện tại và dự báo 3 ngày. Dùng khi người dùng hỏi về thời tiết, nhiệt độ, mưa, "
            f"có cần mang ô không. Nếu không nói địa điểm thì dùng {HOME_NAME}."
        ),
        "parameters": {
            "type": "object",
            "properties": {
                "dia_diem": {"type": "string", "description": "Tên thành phố hoặc địa điểm, ví dụ: Đà Nẵng. Bỏ trống nếu là nơi ở hiện tại."}
            },
            "required": [],
        },
    },
}


def _geocode(name: str):
    r = requests.get(
        "https://geocoding-api.open-meteo.com/v1/search",
        params={"name": name, "count": 1, "language": "vi"},
        timeout=5,
    )
    r.raise_for_status()
    results = r.json().get("results") or []
    if not results:
        return None
    g = results[0]
    return g["name"], g["latitude"], g["longitude"]


@register_function("thoi_tiet", THOI_TIET_DESC, ToolType.WAIT)
def thoi_tiet(dia_diem: str = None):
    try:
        name, lat, lon = HOME_NAME, HOME_LAT, HOME_LON
        if dia_diem and dia_diem.strip() and dia_diem.strip().lower() != HOME_NAME.lower():
            found = _geocode(dia_diem.strip())
            if not found:
                return ActionResponse(Action.REQLLM, f"Không tìm thấy địa điểm '{dia_diem}'.", None)
            name, lat, lon = found
        r = requests.get(
            "https://api.open-meteo.com/v1/forecast",
            params={
                "latitude": lat, "longitude": lon, "timezone": "Asia/Ho_Chi_Minh", "forecast_days": 3,
                "current": "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code",
                "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
            },
            timeout=5,
        )
        r.raise_for_status()
        d = r.json()
        c = d["current"]
        lines = [
            f"{name} hiện tại: {WMO_VI.get(c['weather_code'], 'không rõ')}, {c['temperature_2m']:.0f}°C "
            f"(cảm giác {c['apparent_temperature']:.0f}°C), độ ẩm {c['relative_humidity_2m']}%."
        ]
        daily = d["daily"]
        for i, day in enumerate(daily["time"]):
            label = ["Hôm nay", "Ngày mai", "Ngày kia"][i] if i < 3 else day
            lines.append(
                f"{label}: {WMO_VI.get(daily['weather_code'][i], 'không rõ')}, "
                f"{daily['temperature_2m_min'][i]:.0f} đến {daily['temperature_2m_max'][i]:.0f}°C, "
                f"khả năng mưa {daily['precipitation_probability_max'][i]}%."
            )
        return ActionResponse(Action.REQLLM, "\n".join(lines), None)
    except Exception as e:  # network or API error
        return ActionResponse(Action.REQLLM, f"Không lấy được dữ liệu thời tiết: {e}", None)
