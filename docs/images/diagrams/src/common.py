"""Shared helpers for the Bắp SVG diagrams (theme-aware, hand-written SVG)."""
from xml.sax.saxutils import escape

FONT = "Nunito, Inter, 'Segoe UI', system-ui, -apple-system, sans-serif"

STYLE = """
<style>
  svg { --bg:#f8f6ff; --card:#ffffff; --panel:#f0ecff; --ink:#1e1b2e; --muted:#6b6785;
        --line:#d6cff3; --accent:#7c5cff; --accent2:#a594ff; --soft:#ece6ff; --shadow:#2a1f66;
        --shadowop:0.10; --before:#d9d4ea; --corn:#e8b520; --avo:#6fa83a; --pink:#e57aa3;
        --sky:#3f9fd6; --danger:#e0567a; --ok:#2f9e6a; --chip:#f3f0ff; --onaccent:#ffffff; }
  @media (prefers-color-scheme: dark) {
    svg { --bg:#15131f; --card:#1e1b2e; --panel:#1a1828; --ink:#f1eefc; --muted:#a39fbd;
          --line:#3a3454; --accent:#a594ff; --accent2:#7c5cff; --soft:#2a2540; --shadow:#000000;
          --shadowop:0.45; --before:#3c3756; --corn:#f5c84b; --avo:#8bc34a; --pink:#f7a8c4;
          --sky:#7ec8f0; --danger:#ff7c9c; --ok:#5fd39a; --chip:#262238; --onaccent:#15131f; }
  }
  text { font-family: %FONT%; fill: var(--ink); }
  .bg { fill: var(--bg); }
  .card { fill: var(--card); stroke: var(--line); stroke-width: 1; }
  .panel { fill: var(--panel); stroke: var(--accent2); stroke-width: 1.5; stroke-dasharray: 6 5; }
  .soft { fill: var(--soft); }
  .chip { fill: var(--chip); stroke: var(--line); stroke-width: 1; }
  .accentf { fill: var(--accent); }
  .accent2f { fill: var(--accent2); }
  .beforef { fill: var(--before); }
  .cornf { fill: var(--corn); } .avof { fill: var(--avo); } .pinkf { fill: var(--pink); }
  .skyf { fill: var(--sky); } .dangerf { fill: var(--danger); } .okf { fill: var(--ok); }
  .t-title { font-size: 24px; font-weight: 800; letter-spacing: -0.2px; }
  .t-sub { font-size: 14px; fill: var(--muted); }
  .t-h { font-size: 15.5px; font-weight: 800; }
  .t-b { font-size: 14px; font-weight: 600; }
  .t-s { font-size: 13px; fill: var(--muted); }
  .t-xs { font-size: 12px; fill: var(--muted); }
  .t-mono { font-family: 'JetBrains Mono', 'SFMono-Regular', Menlo, Consolas, monospace; font-size: 13px; }
  .t-on { fill: var(--onaccent); }
  .t-accent { fill: var(--accent); }
  .t-danger { fill: var(--danger); }
  .t-ok { fill: var(--ok); }
  .line { stroke: var(--muted); stroke-width: 1.6; fill: none; }
  .aline { stroke: var(--accent); stroke-width: 2; fill: none; }
  .dline { stroke: var(--muted); stroke-width: 1.4; fill: none; stroke-dasharray: 4 4; }
  .life { stroke: var(--line); stroke-width: 2; stroke-dasharray: 2 6; stroke-linecap: round; }
  .ds { flood-color: var(--shadow); flood-opacity: var(--shadowop); }
  .mk { fill: var(--muted); } .mka { fill: var(--accent); }
</style>
""".replace("%FONT%", FONT)

DEFS = """
<defs>
  <filter id="sh" x="-8%" y="-15%" width="116%" height="140%">
    <feDropShadow class="ds" dx="0" dy="3" stdDeviation="5"/>
  </filter>
  <marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
    <path class="mk" d="M0,1 L10,5 L0,9 z"/>
  </marker>
  <marker id="ara" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
    <path class="mka" d="M0,1 L10,5 L0,9 z"/>
  </marker>
  <linearGradient id="grad" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#8f6bff"/><stop offset="1" stop-color="#5b8cff"/>
  </linearGradient>
  <linearGradient id="gradwarm" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="#ffd84d"/><stop offset="1" stop-color="#ff9eb5"/>
  </linearGradient>
</defs>
"""


def esc(s):
    return escape(str(s))


def svg(w, h, body, title, desc):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
        f'role="img" aria-labelledby="t d">\n<title id="t">{esc(title)}</title>\n<desc id="d">{esc(desc)}</desc>\n'
        f"{STYLE}{DEFS}"
        f'<rect class="bg" x="0" y="0" width="{w}" height="{h}" rx="22"/>\n{body}</svg>\n'
    )


def text(x, y, s, cls="t-b", anchor="start", extra=""):
    a = "" if anchor == "start" else f' text-anchor="{anchor}"'
    return f'<text x="{x}" y="{y}" class="{cls}"{a}{extra}>{esc(s)}</text>\n'


def rect(x, y, w, h, cls="card", rx=16, shadow=True, extra=""):
    f = ' filter="url(#sh)"' if shadow else ""
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" class="{cls}"{f}{extra}/>\n'


def pill(x, y, s, cls="chip", tcls="t-s", h=26, pad=12, cw=7.4, anchor="start"):
    """Pill with text; width estimated from character count."""
    w = int(len(s) * cw + 2 * pad)
    if anchor == "middle":
        x = x - w / 2
    elif anchor == "end":
        x = x - w
    return (
        rect(x, y, w, h, cls, rx=h / 2, shadow=False)
        + text(x + w / 2, y + h / 2 + 4.5, s, tcls, "middle")
    ), w


def header(title, sub, x=32, y=48):
    return text(x, y, title, "t-title") + text(x, y + 24, sub, "t-sub")
