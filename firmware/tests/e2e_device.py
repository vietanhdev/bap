#!/usr/bin/env python3
"""End-to-end test of a running Bắp device over its LAN API. Standard library only.

    python3 firmware/tests/e2e_device.py HOST [--log SERIAL_LOG] [--skip-chat] [--only NAME]

Sections:
  api    web API: pages, config round trip, bad requests, concurrency
  tools  every device tool called directly (no model), valid and invalid arguments
  chat   real Gemini conversations, one per tool; needs --log (a file the serial console is
         being appended to) to see which tool the model called

The test sets the volume to 1 while it runs and restores it, restores the character and
language, and only adds then removes its own notes, timers and memories. It fails if the
device reboots or the serial log shows a crash. Do not run it on a device someone is using.
"""
import argparse
import json
import re
import sys
import threading
import time
import urllib.error
import urllib.request

H = {"X-Bap": "1", "Content-Type": "application/json"}
RESULTS = []


def url(host, path):
    return f"http://{host}{path}"


def get(host, path, timeout=15):
    with urllib.request.urlopen(url(host, path), timeout=timeout) as r:
        return r.status, r.headers.get("Content-Type", ""), r.read()


def post(host, path, body, headers=H, timeout=30):
    data = body if isinstance(body, bytes) else json.dumps(body).encode()
    req = urllib.request.Request(url(host, path), data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def status(host):
    return json.loads(get(host, "/api/status")[2])


def check(name, ok, detail=""):
    RESULTS.append((name, bool(ok), detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""), flush=True)


def tool(host, name, args=None):
    code, body = post(host, "/api/tool", {"name": name, "arguments": args or {}}, timeout=60)
    try:
        j = json.loads(body)
    except ValueError:
        return code, None, body.decode(errors="replace")
    res = j.get("result") or {}
    text = " ".join(c.get("text", "") for c in res.get("content", []) if isinstance(c, dict))
    return code, j, text or json.dumps(j.get("error"), ensure_ascii=False)


def wait_idle(host, limit=90, ok_states=("idle",)):
    end = time.time() + limit
    while time.time() < end:
        try:
            if status(host)["state"] in ok_states:
                return True
        except Exception:
            pass
        time.sleep(1)
    return False


# --------------------------------------------------------------------------------------------
def test_api(host):
    for path in ("/", "/api/status", "/api/config", "/api/perf", "/api/wifi"):
        code, ctype, body = get(host, path)
        check(f"api GET {path}", code == 200 and len(body) > 10, ctype)
    code, ctype, body = get(host, "/screen.jpg")
    check("api GET /screen.jpg", code == 200 and body[:2] == b"\xff\xd8", f"{len(body)} bytes")
    code, ctype, body = get(host, "/face?c=bap")
    check("api GET /face", code == 200 and body[:3] == b"GIF", f"{len(body)} bytes")

    code, _ = post(host, "/api/config", {"volume": 1}, headers={"Content-Type": "application/json"})
    check("api POST without X-Bap is refused", code in (400, 403), str(code))
    code, _ = post(host, "/api/config", b"{not json", headers=H)
    check("api POST bad JSON is refused", code >= 400, str(code))
    code, _ = post(host, "/api/config", b'{"city":"' + b"x" * 200_000 + b'"}', headers=H, timeout=60)
    check("api POST 200 KB body is refused", code >= 400, str(code))
    code, _ = post(host, "/api/action", {"action": "nope"})
    check("api unknown action is refused", code >= 400, str(code))
    code, _ = post(host, "/api/tool", {"name": "no.such_tool", "arguments": {}})
    check("api unknown tool answers an error", code in (200, 400), str(code))

    cfg = json.loads(get(host, "/api/config")[2])
    for lang in ("en", "ko", "zh", "vi"):
        post(host, "/api/config", {"language": lang})
        got = json.loads(get(host, "/api/config")[2])["language"]
        check(f"api language round trip {lang}", got == lang, got)
    post(host, "/api/config", {"language": cfg["language"]})

    for a in ("wheel_hold", "wheel_right", "wheel_right", "wheel_click", "wheel_hold", "wheel_hold"):
        code, _ = post(host, "/api/action", {"action": a})
        time.sleep(0.4)
    check("api remote wheel sequence", code == 200)

    errors = []

    def hammer():
        for _ in range(5):
            try:
                get(host, "/api/status", timeout=20)
            except Exception as e:  # noqa: BLE001
                errors.append(str(e))

    threads = [threading.Thread(target=hammer) for _ in range(6)]
    [t.start() for t in threads]
    [t.join() for t in threads]
    check("api 30 concurrent status requests", len(errors) <= 3, f"{len(errors)} failed")
    shots = sum(1 for _ in range(10) if get(host, "/screen.jpg")[0] == 200)
    check("api 10 screenshots in a row", shots == 10, f"{shots}/10")


# --------------------------------------------------------------------------------------------
def has(*words):
    return lambda t: any(w.lower() in t.lower() for w in words)


def nonempty(t):
    return len(t.strip()) > 10


DIRECT = [
    ("vn.lunar_date", {}, has("âm")),
    ("vn.lunar_date", {"day": 17, "month": 11, "year": 2026}, has("9", "tháng 10")),
    ("vn.lunar_date", {"day": 31, "month": 2, "year": 2026}, has("không có")),
    ("vn.lunar_to_solar", {"lunar_day": 1, "lunar_month": 1, "lunar_year": 2027}, has("2027")),
    ("vn.lunar_to_solar", {"lunar_day": 30, "lunar_month": 2, "lunar_year": 2026}, nonempty),
    ("vn.weather", {"place": "Hà Nội"}, has("độ", "°")),
    ("vn.weather", {"place": "Qwxzy không có thật"}, nonempty),
    ("vn.air_quality", {"place": "Hà Nội"}, nonempty),
    ("vn.news", {"topic": "sports"}, nonempty),
    ("vn.news", {"topic": "no_such_topic"}, nonempty),
    ("vn.exchange_rate", {"code": "USD"}, has("USD", "đô")),
    ("vn.exchange_rate", {"code": "ZZZ"}, nonempty),
    ("vn.gold_price", {}, nonempty),
    ("vn.gold_price_hai_hong", {}, nonempty),
    ("vn.stocks", {}, has("VN-Index", "VNINDEX", "VN Index")),
    ("vn.lottery", {"region": "north"}, nonempty),
    ("vn.lottery", {"region": "south"}, nonempty),
    ("sports.football", {"league": "premier_league"}, nonempty),
    ("sports.football", {"league": "no_such_league"}, has("không có giải")),
    ("timer.set", {"seconds": 3600, "text": "e2e"}, nonempty),
    ("alarm.set", {"hour": 6, "minute": 30, "text": "e2e"}, nonempty),
    ("timer.list", {}, has("e2e", "giờ")),
    ("timer.cancel", {"number": 0}, has("hủy")),
    ("notes.add", {"text": "e2e ghi chú"}, nonempty),
    ("notes.list", {}, has("e2e")),
    ("notes.remove", {"number": 99}, nonempty),
    ("notes.remove", {"number": 0}, has("xóa")),
    ("memory.save", {"fact": "e2e thích trà đá"}, has("nhớ")),
    ("memory.forget", {"fact": "khong co dieu nay"}, has("không tìm thấy")),
    ("memory.forget", {"fact": "tra da"}, has("đã quên 1")),
    ("music.search", {"query": "Hà Nội mùa thu"}, nonempty),
    ("music.sd_card", {"query": "e2e không có bài này"}, nonempty),
    ("extension.call", {"name": "no_such_extension", "text": "x"}, nonempty),
    ("self.get_device_status", {}, has("volume")),
    ("self.camera.take_photo", {"question": "có gì?"}, has("trò chuyện", "camera")),
]


def test_tools(host):
    before = status(host)["uptime_s"]
    # timer.cancel and notes.remove with number=0 delete everything, so never run them over real data.
    cfg = json.loads(get(host, "/api/config")[2])
    has_user_data = bool(cfg.get("notes")) or "Không có" not in (status(host).get("alarms") or "")
    for name, args, ok in DIRECT:
        if has_user_data and name.startswith(("timer.", "alarm.", "notes.")):
            print(f"SKIP  tool {name}: this device holds real timers or notes", flush=True)
            continue
        t0 = time.time()
        try:
            code, j, text = tool(host, name, args)
        except Exception as e:  # noqa: BLE001
            check(f"tool {name} {args}", False, f"exception {e}")
            continue
        check(f"tool {name} {json.dumps(args, ensure_ascii=False)}", code == 200 and ok(text),
              f"{time.time() - t0:.1f}s: {text[:90]}")
    code, j, text = tool(host, "radio.play", {"station": "vov1"})
    time.sleep(6)
    s = status(host)
    check("tool radio.play vov1 starts playback", s["state"] == "notifying" and s["now_playing"],
          s["now_playing"])
    tool(host, "music.stop")
    time.sleep(2)
    check("tool music.stop stops playback", status(host)["state"] == "idle")
    check("tools: no reboot", status(host)["uptime_s"] >= before)


# --------------------------------------------------------------------------------------------
CHAT = [
    ("Hôm nay âm lịch ngày bao nhiêu?", "vn.lunar_date", None),
    ("Rằm tháng giêng năm sau là ngày mấy dương lịch?", "vn.lunar_to_solar", None),
    ("Thời tiết Hà Nội hôm nay thế nào?", "vn.weather", None),
    ("Chất lượng không khí ở đây thế nào?", "vn.air_quality", None),
    ("Đọc giúp mình tin thể thao mới nhất", "vn.news", None),
    ("Tỷ giá đô la hôm nay bao nhiêu?", "vn.exchange_rate", None),
    ("Giá vàng PNJ hôm nay?", "vn.gold_price", None),
    ("Giá vàng tiệm Hải Hồng hôm nay?", "vn.gold_price_hai_hong", None),
    ("VN-Index hôm nay thế nào?", "vn.stocks", None),
    ("Kết quả xổ số miền Bắc hôm qua", "vn.lottery", None),
    ("Lịch thi đấu Ngoại hạng Anh tuần này", "sports.football", None),
    ("Hẹn giờ 10 phút giúp mình", "timer.set", None),
    ("Đặt báo thức 6 giờ 30 sáng mai", "alarm.set", None),
    ("Đang có những hẹn giờ và báo thức nào?", "timer.list", None),
    ("Hủy hết hẹn giờ và báo thức đi", "timer.cancel", None),
    ("Ghi chú giúp mình: mua sữa", "notes.add", None),
    ("Đọc ghi chú của mình", "notes.list", None),
    ("Xóa hết ghi chú", "notes.remove", None),
    ("Nhớ giúp mình là mình thích uống trà đá", "memory.save", None),
    ("Quên chuyện mình thích uống trà đá đi", "memory.forget", None),
    ("Nhìn xem trước mặt bạn có gì?", "self.camera.take_photo",
     lambda a: not re.search(r"không (thấy|nhìn)|chưa (thấy|chụp)|không chụp", a.lower())),
    ("Âm lượng loa đang là bao nhiêu?", "self.get_device_status", None),
    ("Tìm bài Diễm xưa trên Zing", "music.search", None),
    ("Mở radio VOV1", "radio.play", "silent"),  # the session closes and the radio plays
    # Speaking to the device already stops the music before the session opens, so the model may
    # not need music.stop at all: what matters is that the radio is off afterwards.
    ("Dừng nhạc đi", "music.stop", "stopped"),
    ("Mở ứng dụng đố vui", "app.open", None),
]


def tool_calls(log, offset):
    with open(log, "rb") as f:
        f.seek(offset)
        text = f.read().decode("utf-8", "replace")
    calls = re.findall(r"Tool call: (\S+)", text)
    crashes = [l for l in text.splitlines() if re.search(r"Guru Meditation|abort\(\)|Backtrace:", l)]
    return calls, crashes


def test_chat(host, log):
    cfg = json.loads(get(host, "/api/config")[2])
    for text, want, judge in CHAT:
        # Music keeps playing after radio.play: the next question may arrive while it plays.
        if not wait_idle(host, ok_states=("idle", "notifying")):
            check(f"chat {want}", False, "device never idle")
            continue
        up = status(host)["uptime_s"]
        before = json.dumps(status(host).get("log", []))  # an older identical question may be in the log
        offset = len(open(log, "rb").read()) if log else 0
        t0 = time.time()
        post(host, "/api/say", {"text": text})
        answer = ""
        while time.time() - t0 < (12 if judge in ("silent", "stopped") else 60):
            time.sleep(1)
            try:
                s = status(host)
            except Exception:
                continue
            # The log is a ring buffer: find our question, then the reply after it.
            entries = s.get("log", [])
            asked = [i for i, e in enumerate(entries) if e.get("role") == "user" and e.get("text") == text]
            replies = [e for e in entries[asked[-1] + 1:] if e.get("role") != "user"] if asked else []
            if replies and s["state"] != "speaking" and json.dumps(entries) != before:
                answer = replies[-1].get("text", "")
                break
        time.sleep(1.5)
        calls, crashes = tool_calls(log, offset) if log else ([], [])
        called = want.replace(".", "_") in [c.replace(".", "_") for c in calls] if log else True
        if judge == "stopped":
            good = status(host)["state"] != "notifying" and not crashes
        elif judge == "silent":  # these tools end the session on purpose; no spoken answer needed
            good = called and not crashes
        else:
            good = called and bool(answer.strip()) and (judge is None or judge(answer)) and not crashes
        check(f"chat {want}", good and status(host)["uptime_s"] >= up,
              f"{time.time() - t0:.0f}s calls={calls} answer={answer[:90]!r}")
    wait_idle(host)
    post(host, "/api/config", {"character": cfg["character"]})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--log", help="serial log file being appended to (needed for chat checks)")
    ap.add_argument("--skip-chat", action="store_true")
    ap.add_argument("--only", choices=["api", "tools", "chat"])
    a = ap.parse_args()
    cfg = json.loads(get(a.host, "/api/config")[2])
    start_uptime = status(a.host)["uptime_s"]
    post(a.host, "/api/config", {"volume": 1})
    try:
        if a.only in (None, "api"):
            test_api(a.host)
        if a.only in (None, "tools"):
            test_tools(a.host)
        if a.only == "chat" or (a.only is None and not a.skip_chat):
            test_chat(a.host, a.log)
    finally:
        if status(a.host)["state"] == "notifying":
            post(a.host, "/api/action", {"action": "stop"})
        wait_idle(a.host, 30)
        post(a.host, "/api/config", {"volume": cfg["volume"], "language": cfg["language"],
                                     "character": cfg["character"]})
    check("device did not reboot during the run", status(a.host)["uptime_s"] >= start_uptime)
    failed = [r for r in RESULTS if not r[1]]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} passed")
    for name, _, detail in failed:
        print(f"  FAIL {name}: {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
