# Regenerate: python3 docs/images/diagrams/src/gen.py docs/images/diagrams
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import esc, header, pill, rect, svg, text  # noqa: E402

OUT = sys.argv[1]


def block(x, y, w, h, title, sub, color="accentf", sub2=None):
    s = rect(x, y, w, h, "card", rx=14)
    s += f'<rect x="{x}" y="{y + 14}" width="4" height="{h - 28}" rx="2" class="{color}"/>\n'
    s += text(x + 18, y + 29, title, "t-h")
    s += text(x + 18, y + 50, sub, "t-s")
    if sub2:
        s += text(x + 18, y + 67, sub2, "t-s")
    return s


def arrow(d, cls="line", marker="ar", both=False):
    st = f' marker-start="url(#{marker})"' if both else ""
    return f'<path d="{d}" class="{cls}" marker-end="url(#{marker})"{st}/>\n'


# ---------------------------------------------------------------- architecture
def architecture():
    W, H = 980, 690
    b = header("Bắp · system architecture",
               "Everything runs on the Watcher. Audio goes straight to Google, nothing sits in between.")
    px, py, pw, ph = 24, 92, 604, 578
    b += rect(px, py, pw, ph, "panel", rx=20, shadow=False)
    b += text(px + 20, py + 28, "SenseCAP Watcher", "t-h")
    b += text(px + 194, py + 28, "ESP32-S3 · 8 MB PSRAM · 32 MB flash · 412×412 round LCD", "t-s")
    c = [44, 240, 436]
    cw, gap = 176, 20
    span2 = cw * 2 + gap
    span3 = cw * 3 + gap * 2
    rows = [136, 224, 312, 400, 488, 576]
    h = 72
    # row 0: protocol
    b += block(c[0], rows[0], span3, h, "GeminiLiveProtocol", "setup · audio up/down · tool bridge", "skyf")
    p, _ = pill(c[0] + span3 - 16, rows[0] + 23, "TicketSsl · TLS resume", "soft", "t-b t-accent", h=28, cw=8, anchor="end")
    b += p
    # row 1
    b += block(c[0], rows[1], span2, h, "Application", "upstream xiaozhi-esp32 · states, wake word, sessions", "accentf")
    b += block(c[2], rows[1], cw, h, "Audio service", "AFE · codecs · resample", "accentf")
    # row 2: inputs
    b += block(c[0], rows[2], cw, h, "Mic + AFE", "wake word “Hi Lili”", "cornf")
    b += block(c[1], rows[2], cw, h, "Wheel + touch", "gestures · round menu", "cornf")
    b += block(c[2], rows[2], cw, h, "Himax HX6538", "on-chip person detect", "cornf")
    # row 3
    b += block(c[0], rows[3], cw, h, "LVGL faces", "23 faces · subtitles", "pinkf")
    b += block(c[1], rows[3], cw, h, "Music player", "Zing · VOV/FM · SD card", "pinkf")
    b += block(c[2], rows[3], cw, h, "Config", "NVS + assistant.json", "avof")
    # row 4
    b += block(c[0], rows[4], cw, h, "Scheduler", "timers · alarms · night", "avof")
    b += block(c[1], rows[4], span2, h, "MCP tools", "weather · lunar · news · gold · stocks · football", "avof")
    # row 5
    b += block(c[0], rows[5], cw, h, "SD card", "offline MP3 library", "cornf")
    b += block(c[1], rows[5], span2, h, "Web UI + OTA", "bap-xxxx.local · password · health · update", "skyf")

    # right column
    rx_, rw = 664, 292
    gy, gh = 112, 140
    b += f'<rect x="{rx_}" y="{gy}" width="{rw}" height="{gh}" rx="18" fill="url(#grad)" filter="url(#sh)"/>\n'
    b += f'<text x="{rx_ + 20}" y="{gy + 34}" style="font-size:18px;font-weight:800;fill:#fff">Google Gemini Live API</text>\n'
    for i, line in enumerate(["WebSocket over TLS", "PCM16 16 kHz up · 24 kHz down",
                              "tool calls · live transcripts"]):
        b += f'<text x="{rx_ + 20}" y="{gy + 64 + i * 22}" style="font-size:14px;fill:#fff;opacity:.92">{esc(line)}</text>\n'
    # no relay badge
    by = gy + gh + 14
    b += rect(rx_, by, rw, 34, "chip", rx=17, shadow=False)
    b += f'<circle cx="{rx_ + 20}" cy="{by + 17}" r="7" class="okf"/>\n'
    b += f'<path d="M{rx_ + 16.5},{by + 17} l2.5,2.6 l4.5,-5" stroke="#fff" stroke-width="2" fill="none" stroke-linecap="round"/>\n'
    b += text(rx_ + 36, by + 22, "No relay server, no cloud of ours", "t-b t-ok")

    dy, dh = 312, 196
    b += rect(rx_, dy, rw, dh, "card", rx=18)
    b += text(rx_ + 20, dy + 30, "Live data", "t-h")
    b += text(rx_ + 102, dy + 30, "HTTP(S), no extra keys", "t-s")
    srcs = [("Open-Meteo", "avof"), ("VnExpress", "pinkf"), ("Vietcombank", "avof"), ("PNJ · Hải Hồng", "cornf"),
            ("CafeF", "skyf"), ("ESPN", "skyf"), ("Zing MP3", "pinkf"), ("VOV · FM", "cornf")]
    for i, (s, col) in enumerate(srcs):
        cx = rx_ + 20 + (i % 2) * 136
        cy = dy + 52 + (i // 2) * 34
        b += rect(cx, cy, 126, 26, "chip", rx=13, shadow=False)
        b += f'<circle cx="{cx + 13}" cy="{cy + 13}" r="4.5" class="{col}"/>\n'
        b += text(cx + 24, cy + 18, s, "t-s")
    wy = 524
    b += rect(rx_, wy, rw, 56, "card", rx=16)
    b += text(rx_ + 20, wy + 24, "Webhooks", "t-h")
    b += text(rx_ + 20, wy + 44, "Home Assistant · n8n · your server", "t-s")
    bry = 594
    b += rect(rx_, bry, rw, 56, "card", rx=16)
    b += text(rx_ + 20, bry + 24, "Browser on your LAN", "t-h")
    b += text(rx_ + 20, bry + 44, "settings · logs · firmware update", "t-s")

    # arrows (device edge x = c[2]+cw = 612 → right column 664)
    ex = c[2] + cw
    b += arrow(f"M{ex + 4},{rows[0] + 36} L{rx_ - 6},{rows[0] + 36}", "aline", "ara", both=True)
    b += arrow(f"M{ex + 4},{rows[4] + 26} C{ex + 30},{rows[4] + 26} {rx_ - 30},{dy + 150} {rx_ - 6},{dy + 150}")
    b += arrow(f"M{ex + 4},{rows[4] + 50} C{ex + 30},{rows[4] + 50} {rx_ - 30},{wy + 28} {rx_ - 6},{wy + 28}")
    b += arrow(f"M{rx_ - 6},{bry + 28} C{rx_ - 30},{bry + 28} {ex + 30},{rows[5] + 36} {ex + 4},{rows[5] + 36}", "dline", both=True)
    return svg(W, H, b, "Bắp system architecture",
               "Firmware modules on the SenseCAP Watcher and the external services they talk to directly.")


# ---------------------------------------------------------------- session flow
def session():
    W, H = 980, 810
    b = header("A conversation, step by step",
               "From the wake word to the session closing on its own. Times measured on the device.")
    lx = {"user": 150, "dev": 470, "gem": 800}
    top = 100
    heads = [("user", "You", "cornf"), ("dev", "Bắp (ESP32-S3)", "accentf"), ("gem", "Gemini Live", "skyf")]
    for k, name, col in heads:
        x = lx[k]
        b += rect(x - 92, top, 184, 46, "card", rx=23)
        b += f'<circle cx="{x - 70}" cy="{top + 23}" r="7" class="{col}"/>\n'
        b += text(x + 8, top + 29, name, "t-h", "middle")
        b += f'<line x1="{x}" y1="{top + 52}" x2="{x}" y2="{H - 40}" class="life"/>\n'
    # activation bar on device
    b += f'<rect x="{lx["dev"] - 6}" y="{top + 72}" width="12" height="{H - top - 136}" rx="6" class="soft"/>\n'

    y = [190, 246, 302, 352, 412, 462, 512, 562, 620, 680]

    def msg(a, c, yy, label, sub=None, cls="line", mk="ar"):
        x1, x2 = lx[a], lx[c]
        d = 1 if x2 > x1 else -1
        s = f'<path d="M{x1 + d * 10},{yy} L{x2 - d * 10},{yy}" class="{cls}" marker-end="url(#{mk})"/>\n'
        mx = (x1 + x2) / 2
        s += text(mx, yy - 9, label, "t-b", "middle")
        if sub:
            s += text(mx, yy + 19, sub, "t-s", "middle")
        return s

    def selfnote(yy, label, sub=None):
        x = lx["dev"] + 16
        s = rect(x, yy - 18, 270, 46 if sub else 30, "chip", rx=12, shadow=False)
        s += text(x + 14, yy + 2, label, "t-b")
        if sub:
            s += text(x + 14, yy + 20, sub, "t-s")
        return s

    def tpill(yy, label, accent=False):
        p, _ = pill(28, yy - 13, label, "soft" if accent else "chip", "t-b t-accent" if accent else "t-s", h=26, cw=7.6)
        return p

    Y = dict(wake=196, tls=252, setup=316, done=380, note=416, speak=486, call=546, resp=600, out=670, idle=742)
    b += msg("user", "dev", Y["wake"], "“Hi Lili” · tap · press", None, "aline", "ara")
    b += tpill(Y["wake"], "0 s", True)
    b += msg("dev", "gem", Y["tls"], "TLS with saved session ticket", "~0.1 s resumed (1.36 s full handshake)")
    b += tpill(Y["tls"], "+0.1 s")
    b += msg("dev", "gem", Y["setup"], "setup: persona + tools + voice", "~15 KB, resume handle if unchanged")
    b += msg("gem", "dev", Y["done"], "setupComplete")
    b += selfnote(Y["note"], "listening face, mic opens", "1.4–2.3 s after the wake word")
    b += tpill(Y["note"], "≈1.4–2.3 s", True)
    b += msg("user", "dev", Y["speak"], "speaks")
    b += msg("dev", "gem", Y["speak"], "realtimeInput", "PCM16 16 kHz, 120 ms chunks")
    b += msg("gem", "dev", Y["call"], "toolCall: weather, gold, Zing…")
    b += msg("dev", "gem", Y["resp"], "toolResponse", "tool ran on the device (HTTP / NVS / camera)")
    b += msg("gem", "dev", Y["out"], "serverContent", "PCM16 24 kHz audio + transcripts")
    b += msg("dev", "user", Y["out"], "voice · lip sync · subtitles", None, "aline", "ara")
    b += selfnote(Y["idle"], "30 s of silence → close session", "tap or wake word starts a new one")
    b += tpill(Y["idle"], "+30 s idle")
    return svg(W, H, b, "Gemini Live session flow",
               "Sequence of messages between the user, the Bắp device and Gemini Live, with measured timings.")


# ---------------------------------------------------------------- performance
def performance():
    rows = [
        ("Latency", None),
        ("Wake → listening", (2.9, 1.8, "s", "{:.1f}")),
        ("TLS handshake", (1.36, 0.1, "s", "{:.2f}")),
        ("Zing song search", (2.5, 1.0, "s", "{:.1f}")),
        ("Weather lookup", (6.0, 2.7, "s", "{:.1f}")),
        ("CPU (display task)", None),
        ("Listening face", (19.5, 6.0, "%", "{:.1f}")),
        ("Music playing", (14.7, 1.3, "%", "{:.1f}")),
        ("Size & update", None),
        ("Firmware image", (3.83, 3.31, "MB", "{:.2f}")),
        ("OTA upload", (112, 33, "s", "{:.0f}")),
    ]
    W = 980
    y = 116
    body = ""
    lx, bx, bw = 40, 250, 520
    for name, v in rows:
        if v is None:
            y += 10
            body += text(lx, y + 14, name.upper(), "t-xs", extra=' style="letter-spacing:1.4px;font-weight:800"')
            body += f'<line x1="{lx + len(name) * 8.8 + 14:.0f}" y1="{y + 10}" x2="{W - 40}" y2="{y + 10}" class="dline" style="stroke:var(--line)"/>\n'
            y += 30
            continue
        before, after, unit, fmt = v
        body += text(lx, y + 25, name, "t-b")
        wb = bw
        wa = max(4, bw * after / before)
        body += f'<rect x="{bx}" y="{y + 4}" width="{wb}" height="14" rx="7" class="beforef"/>\n'
        body += f'<rect x="{bx}" y="{y + 24}" width="{wa:.1f}" height="14" rx="7" fill="url(#grad)"/>\n'
        body += text(bx + wb + 10, y + 16, f"{fmt.format(before)} {unit}", "t-s")
        body += text(bx + wa + 10, y + 36, f"{fmt.format(after)} {unit}", "t-b t-accent")
        imp = round((1 - after / before) * 100)
        p, _ = pill(W - 40, y + 8, f"−{imp}%", "soft", "t-b t-accent", h=28, cw=9, anchor="end")
        body += p
        y += 52
    H = y + 56
    leg = 32
    body += f'<rect x="{bx}" y="{H - leg - 12}" width="28" height="12" rx="6" class="beforef"/>\n'
    body += text(bx + 36, H - leg - 2, "before", "t-s")
    body += f'<rect x="{bx + 110}" y="{H - leg - 12}" width="28" height="12" rx="6" fill="url(#grad)"/>\n'
    body += text(bx + 146, H - leg - 2, "after (lower is better, each row on its own scale)", "t-s")
    b = header("Optimization results", "Measured on a SenseCAP Watcher over the course of the tuning work.") + body
    return svg(W, H, b, "Before and after performance",
               "Horizontal bars comparing latency, CPU, firmware size and OTA time before and after optimization.")


# ---------------------------------------------------------------- flash layout
def flash():
    W, H = 980, 612
    b = header("Flash and memory layout", "32 MB flash (partitions/v2/32m.csv) and where RAM goes at runtime.")
    x0, x1, y = 40, 940, 112
    total = 32.0
    scale = (x1 - x0) / total
    parts = [  # name, start MB, size MB, class, label, sub
        ("sys", 0, 2, "pinkf", "sys", "0–2 MB"),
        ("ota_0", 2, 4, "accentf", "ota_0", "app 4 MB"),
        ("ota_1", 6, 4, "accent2f", "ota_1", "app 4 MB"),
        ("assets", 10, 16, "avof", "assets", "16 MB · fonts, faces, wake word"),
        ("free", 26, 6, "beforef", "unused", "6 MB"),
    ]
    b += text(x0, y, "32 MB FLASH", "t-xs", extra=' style="letter-spacing:1.4px;font-weight:800"')
    by = y + 14
    for i, (n, st, sz, cls, lab, sub) in enumerate(parts):
        xx = x0 + st * scale
        ww = sz * scale - 4
        b += f'<rect x="{xx:.1f}" y="{by}" width="{ww:.1f}" height="64" rx="12" class="{cls}" filter="url(#sh)"/>\n'
        tc = "t-h" if n in ("free",) else "t-h t-on"
        if n == "sys":
            b += text(xx + ww / 2, by + 38, lab, "t-b t-on", "middle")
        else:
            b += text(xx + 14, by + 28, lab, tc)
            b += text(xx + 14, by + 48, sub, "t-s" if n == "free" else "t-s t-on", extra='' if n == "free" else ' style="opacity:.85"')
    # usage inside ota_0
    ox = x0 + 2 * scale
    used = 0.79 * (4 * scale - 4)
    b += f'<rect x="{ox + 4:.1f}" y="{by + 56}" width="{used:.1f}" height="4" rx="2" fill="#fff" opacity=".85"/>\n'
    # address ticks
    for mb in (0, 2, 6, 10, 26, 32):
        xx = x0 + mb * scale - (4 if mb == 32 else 0)
        b += text(xx, by + 84, f"0x{int(mb * 1024 * 1024):X}" if mb else "0x0", "t-xs t-mono",
                  "end" if mb == 32 else "start")
    b += text(x0 + 2 * scale + 14, by + 108, "Bắp ≈ 3.3 MB (79% of the slot), the other slot takes the next OTA", "t-s")

    # zoom of the first 2 MB
    zy = 262
    b += rect(x0, zy, x1 - x0, 150, "card", rx=18)
    b += f'<circle cx="{x0 + 28}" cy="{zy + 25}" r="7" class="pinkf"/>\n'
    b += text(x0 + 44, zy + 30, "sys: the first 2 MB, zoomed in (not to scale)", "t-h")
    zparts = [
        ("bootloader", "0x0", "", "beforef", False),
        ("partition table", "0x8000", "", "beforef", False),
        ("nvsfactory", "0x9000", "200 KB · Seeed keys", "dangerf", True),
        ("nvs", "0x3B000", "840 KB · settings", "avof", False),
        ("otadata", "0x10D000", "8 KB", "accent2f", False),
        ("phy_init", "0x10F000", "4 KB", "beforef", False),
        ("free", "0x110000", "", "beforef", False),
    ]
    widths = [96, 116, 178, 154, 108, 100, 96]
    zx = x0 + 20
    for (n, addr, sub, cls, warn), ww in zip(zparts, widths):
        on = cls in ("dangerf", "avof", "accent2f")
        b += f'<rect x="{zx}" y="{zy + 46}" width="{ww - 6}" height="52" rx="10" class="{cls}"/>\n'
        b += text(zx + 10, zy + 68, n, "t-b t-on" if on else "t-b")
        if sub:
            b += text(zx + 10, zy + 88, sub, "t-xs t-on" if on else "t-xs", extra=' style="opacity:.9"' if on else "")
        b += text(zx + 2, zy + 118, addr, "t-xs t-mono")
        if warn:
            p, _ = pill(zx + (ww - 6) / 2, zy + 126, "never written by Bắp", "soft", "t-xs t-danger", h=22, cw=6.6, anchor="middle")
            b += p
        zx += ww

    # RAM panel
    ry = 438
    cw = (x1 - x0 - 24) / 3
    cards = [
        ("Internal SRAM", "55–60 KB", "free when idle", "lowest seen 33 KB during TLS + music", "accentf"),
        ("PSRAM 8 MB", "3.2 MB", "free when idle", "Wi-Fi/lwIP buffers, cJSON, task stacks", "avof"),
        ("Moved to PSRAM", "3 stacks", "opus codec · notify · Zing", "never tasks that write flash/NVS", "cornf"),
    ]
    for i, (t, big, s1, s2, col) in enumerate(cards):
        cx = x0 + i * (cw + 12)
        b += rect(cx, ry, cw, 136, "card", rx=18)
        b += f'<circle cx="{cx + 26}" cy="{ry + 30}" r="7" class="{col}"/>\n'
        b += text(cx + 42, ry + 35, t, "t-h")
        b += text(cx + 20, ry + 80, big, "t-title", extra=' style="font-size:30px"')
        b += text(cx + 20, ry + 104, s1, "t-b")
        b += text(cx + 20, ry + 124, s2, "t-s")
    return svg(W, H, b, "Flash and memory layout",
               "Partition map of the 32 MB flash with nvsfactory protected, plus internal RAM and PSRAM budget.")


# ---------------------------------------------------------------- controls
def face(cx, cy, r):
    s = f'<circle cx="{cx}" cy="{cy}" r="{r + 14}" fill="#2b2838" stroke="#4a4466" stroke-width="2" filter="url(#sh)"/>\n'
    s += f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#15131f"/>\n'
    # status bar
    s += f'<text x="{cx}" y="{cy - r + 42}" text-anchor="middle" style="font-size:15px;fill:#f1eefc;font-weight:700">10:24</text>\n'
    # cloud (Bông)
    s += f'<g transform="translate({cx},{cy + 6})">'
    s += '<path d="M-62,30 C-92,30 -94,-8 -66,-12 C-66,-46 -24,-56 -10,-30 C2,-58 52,-52 50,-16 C80,-16 84,30 56,30 Z" fill="#ffffff" stroke="#9db6e4" stroke-width="3"/>'
    s += '<ellipse cx="-24" cy="2" rx="6" ry="8" fill="#3a2e4c"/><ellipse cx="22" cy="2" rx="6" ry="8" fill="#3a2e4c"/>'
    s += '<circle cx="-22" cy="-1" r="2" fill="#fff"/><circle cx="24" cy="-1" r="2" fill="#fff"/>'
    s += '<ellipse cx="-40" cy="14" rx="9" ry="5" fill="#ff9eb5" opacity=".75"/><ellipse cx="38" cy="14" rx="9" ry="5" fill="#ff9eb5" opacity=".75"/>'
    s += '<path d="M-8,14 Q-1,21 6,14" stroke="#3a2e4c" stroke-width="3" fill="none" stroke-linecap="round"/>'
    s += "</g>\n"
    s += f'<text x="{cx}" y="{cy + 92}" text-anchor="middle" style="font-size:14px;fill:#c9c4e0">Chạm 2 lần để nói chuyện</text>\n'
    return s


def controls():
    W, H = 980, 640
    b = header("Wheel and touch", "Every gesture, by state. Hold anywhere for the round menu.")
    cx, cy, r = 220, 360, 150
    b += face(cx, cy, r)
    # wheel knob on the right edge, upper side like the real Watcher
    kx, ky = cx + r + 14, cy - 120
    b += f'<rect x="{kx - 4}" y="{ky}" width="30" height="70" rx="12" fill="#3a364d"/>\n'
    for i in range(6):
        b += f'<line x1="{kx + 2}" y1="{ky + 10 + i * 10}" x2="{kx + 20}" y2="{ky + 10 + i * 10}" stroke="#56506f" stroke-width="2"/>\n'
    b += f'<path d="M{kx + 40},{ky + 8} q16,27 0,54" class="aline" marker-end="url(#ara)" marker-start="url(#ara)"/>\n'
    b += text(kx + 11, ky + 92, "wheel", "t-b t-accent", "middle")
    # touch ripple
    b += f'<circle cx="{cx - 60}" cy="{cy + 50}" r="16" fill="none" stroke="#a594ff" stroke-width="2.5" opacity=".9"/>\n'
    b += f'<circle cx="{cx - 60}" cy="{cy + 50}" r="27" fill="none" stroke="#a594ff" stroke-width="1.5" opacity=".45"/>\n'
    b += text(cx, cy + r + 52, "412×412 touch screen (SPD2010)", "t-s", "middle")

    def table(x, y, w, title, col, rows):
        h = 50 + len(rows) * 34
        s = rect(x, y, w, h, "card", rx=18)
        s += f'<circle cx="{x + 24}" cy="{y + 27}" r="7" class="{col}"/>\n'
        s += text(x + 40, y + 32, title, "t-h")
        for i, (st, g, act) in enumerate(rows):
            yy = y + 52 + i * 34
            if i:
                s += f'<line x1="{x + 18}" y1="{yy - 8}" x2="{x + w - 18}" y2="{yy - 8}" stroke="var(--line)" stroke-width="1"/>\n'
            p, _ = pill(x + 18, yy - 4, st, "chip", "t-xs", h=22, pad=8, cw=6.4)
            s += p
            s += text(x + 108, yy + 12, g, "t-b")
            s += text(x + 252, yy + 12, act, "t-s")
        return s, h

    tx, tw = 470, 480
    t1, h1 = table(tx, 104, tw, "Wheel", "accentf", [
        ("any", "Rotate", "volume"),
        ("any", "Press", "talk · pause · resume"),
        ("idle", "Double press", "apps list"),
        ("any", "Hold 1 s", "menu (hold again: back)"),
        ("any", "Press 3 times", "power off"),
        ("any", "Hold 20 s", "factory reset"),
    ])
    b += t1
    t2, h2 = table(tx, 104 + h1 + 16, tw, "Touch", "pinkf", [
        ("idle", "Tap", "giggle"),
        ("idle", "Double tap", "start talking"),
        ("idle", "Hold", "menu"),
        ("talking", "Tap", "interrupt"),
        ("music", "Double tap", "stop music"),
        ("menu", "Drag / tap", "move · centre opens"),
    ])
    b += t2
    H = max(H, 104 + h1 + 16 + h2 + 28)
    return svg(W, H, b, "Wheel and touch controls",
               "Illustration of the Watcher with its wheel and a table of every wheel and touch gesture.")


# ---------------------------------------------------------------- data driven
def datadriven():
    W, H = 980, 600
    b = header("Characters and apps are data",
               "assistant.json ships the defaults; the web UI saves overrides in NVS. No C++ per character.")
    # left: assistant.json
    lx, ly, lw = 32, 104, 300
    b += rect(lx, ly, lw, 260, "card", rx=18)
    b += text(lx + 20, ly + 32, "assistant.json", "t-h t-mono", extra=' style="font-size:15px;font-weight:700"')
    b += text(lx + 20, ly + 52, "built into the firmware", "t-s")
    items = [("characters", "5", "pinkf"), ("apps", "11", "avof"), ("radio", "8", "cornf"),
             ("languages", "4", "skyf"), ("common_rules", "1", "accentf")]
    for i, (k, n, col) in enumerate(items):
        yy = ly + 76 + i * 34
        b += rect(lx + 18, yy, lw - 36, 28, "chip", rx=10, shadow=False)
        b += f'<circle cx="{lx + 34}" cy="{yy + 14}" r="5" class="{col}"/>\n'
        b += text(lx + 48, yy + 19, k, "t-mono")
        b += text(lx + lw - 32, yy + 19, n, "t-b t-accent", "end")
    # left bottom: NVS
    ny = ly + 280
    b += rect(lx, ny, lw, 138, "card", rx=18)
    b += text(lx + 20, ny + 32, "NVS overrides", "t-h")
    b += text(lx + 20, ny + 52, "edited in the web UI, per device", "t-s")
    for i, s in enumerate(["name · voice · persona", "memory · city · language", "night · presence · morning"]):
        b += text(lx + 20, ny + 80 + i * 20, "• " + s, "t-s")
    # middle: Config merge
    mx, my, mw = 400, 214, 190
    b += rect(mx, my, mw, 120, "soft", rx=60, shadow=True)
    b += text(mx + mw / 2, my + 54, "Config", "t-h", "middle", extra=' style="font-size:20px"')
    b += text(mx + mw / 2, my + 78, "LoadConfig()", "t-s t-mono", "middle")
    b += f'<path d="M{lx + lw + 6},{ly + 130} C{mx - 30},{ly + 130} {mx - 40},{my + 50} {mx - 6},{my + 50}" class="line" marker-end="url(#ar)"/>\n'
    b += f'<path d="M{lx + lw + 6},{ny + 70} C{mx - 30},{ny + 70} {mx - 40},{my + 76} {mx - 6},{my + 76}" class="line" marker-end="url(#ar)"/>\n'
    # right outputs
    rx_, rw = 654, 296
    outs = [
        ("Localize()", "menu, status and UI strings", "skyf", 104),
        ("FaceShow()", "LVGL face from the look colours", "pinkf", 196),
        ("SystemPrompt()", "persona + rules + language + memory", "accentf", 288),
    ]
    for t, s, col, yy in outs:
        b += rect(rx_, yy, rw, 72, "card", rx=16)
        b += f'<rect x="{rx_}" y="{yy + 14}" width="4" height="44" rx="2" class="{col}"/>\n'
        b += text(rx_ + 20, yy + 30, t, "t-h t-mono", extra=' style="font-size:14.5px;font-weight:700"')
        b += text(rx_ + 20, yy + 52, s, "t-s")
        b += f'<path d="M{mx + mw + 6},{my + 60} C{rx_ - 40},{my + 60} {rx_ - 40},{yy + 36} {rx_ - 6},{yy + 36}" class="line" marker-end="url(#ar)"/>\n'
    gy = 396
    b += f'<rect x="{rx_}" y="{gy}" width="{rw}" height="74" rx="16" fill="url(#grad)" filter="url(#sh)"/>\n'
    b += f'<text x="{rx_ + 20}" y="{gy + 31}" style="font-size:16px;font-weight:800;fill:#fff">Gemini setup message</text>\n'
    b += f'<text x="{rx_ + 20}" y="{gy + 53}" style="font-size:13.5px;fill:#fff;opacity:.9">voice + prompt + tools, about 15 KB</text>\n'
    b += f'<path d="M{rx_ + rw / 2},{366} L{rx_ + rw / 2},{gy - 6}" class="line" marker-end="url(#ar)"/>\n'

    # bottom strip: characters & languages
    sy = 580
    b += text(lx, sy - 10, "CHARACTERS", "t-xs", extra=' style="letter-spacing:1.4px;font-weight:800"')
    chars = [("Bông", "Leda", "#ffffff", "#9db6e4"), ("Bắp", "Puck", "#ffd84d", "#d9a400"),
             ("Bơ", "Achird", "#8bc34a", "#4a7c48"), ("Mochi", "Aoede", "#ffd0de", "#ff96b4"),
             ("Tí", "Kore", "#cee4f6", "#1e2a44")]
    x = lx
    for n, v, fill, stroke in chars:
        w = 112
        b += rect(x, sy, w - 8, 40, "chip", rx=20, shadow=False)
        b += f'<circle cx="{x + 20}" cy="{sy + 20}" r="10" fill="{fill}" stroke="{stroke}" stroke-width="2.5"/>\n'
        b += text(x + 37, sy + 18, n, "t-b")
        b += text(x + 37, sy + 33, v, "t-xs")
        x += w
    lx2 = 654
    b += text(lx2, sy - 10, "LANGUAGES", "t-xs", extra=' style="letter-spacing:1.4px;font-weight:800"')
    x = lx2
    for code, name in [("vi", "Tiếng Việt"), ("en", "English"), ("ko", "한국어"), ("zh", "中文")]:
        w = {"vi": 108, "en": 84, "ko": 62, "zh": 50}[code]
        b += rect(x, sy, w - 6, 40, "chip", rx=20, shadow=False)
        b += text(x + (w - 6) / 2, sy + 18, code, "t-b t-accent", "middle")
        b += text(x + (w - 6) / 2, sy + 33, name, "t-xs", "middle")
        x += w
    H = sy + 70
    return svg(W, H, b, "Data-driven characters",
               "How assistant.json and NVS overrides become the Gemini prompt, UI strings and faces.")


os.makedirs(OUT, exist_ok=True)
for name, fn in [("architecture", architecture), ("session-flow", session), ("performance", performance),
                 ("flash-layout", flash), ("controls", controls), ("data-driven", datadriven)]:
    with open(os.path.join(OUT, name + ".svg"), "w", encoding="utf-8") as f:
        f.write(fn())
print("ok")
