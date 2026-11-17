#!/usr/bin/env python3
"""Validate firmware/data/assistant.json (characters and apps). Standard library only.

Usage: python3 firmware/scripts/validate_data.py [path/to/assistant.json]

Errors make the script exit with status 1; warnings are printed but do not fail.
The rules mirror what the firmware expects (see firmware/src/assistant/assistant_config.cc).
"""

import json
import re
import sys
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
DEFAULT_PATH = FIRMWARE / "data" / "assistant.json"

REQUIRED_CHARACTER = ["id", "name", "voice", "persona", "look.shape"]
REQUIRED_APP = ["id", "title", "prompt"]
# Shapes drawn by BuildBody() in assistant_face.cc; anything else falls back to "cloud".
KNOWN_SHAPES = {"cloud", "corn", "avocado", "mochi", "robot"}
COLOR_KEYS = ["body", "accent", "ink", "cheek"]
COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")
# NVS keys are at most 15 chars; overrides are stored under "c_" + id.
ID_RE = re.compile(r"^[a-z0-9_]{1,13}$")
APP_ID_RE = re.compile(r"^[a-z0-9_]+$")
# build.sh embeds the JSON in a C++ raw string literal with this delimiter.
RAW_STRING_END = ')BAPDATA"'
API_KEY_RE = re.compile(r"AIza[0-9A-Za-z_-]{20,}")
NVS_OVERRIDE_LIMIT = 3900  # SaveCharacterOverride() drops overrides at or above this size
TOOLS_SOURCE = FIRMWARE / "src" / "assistant" / "assistant_tools.cc"
# Tool names are English ("group.action"); Gemini function names allow [A-Za-z0-9_.-], max 64.
TOOL_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$")
TOOL_GROUPS = ("vn", "alarm", "timer", "notes", "memory", "music", "radio", "sports", "app",
               "character", "system", "extension")
TOOL_REF_RE = re.compile(r"\b(?:%s)\.[a-z_]+\b" % "|".join(TOOL_GROUPS))


def ui_strings():
    """On-screen strings the firmware passes through Localize() (menu labels included)."""
    keys = set()
    for path in sorted((FIRMWARE / "src").rglob("*.cc")):
        src = path.read_text(encoding="utf-8")
        keys |= set(re.findall(r'Localize\("((?:[^"\\]|\\.)*)"\)', src))
        keys |= set(re.findall(r'items\.push_back\(\{"((?:[^"\\]|\\.)*)"', src))
        keys |= set(re.findall(r'Confirm\("((?:[^"\\]|\\.)*)"', src))
    return keys


def tool_params():
    """{tool name: set of parameter names} for every mcp.AddTool(...) in assistant_tools.cc."""
    if not TOOLS_SOURCE.is_file():
        return {}
    src = TOOLS_SOURCE.read_text(encoding="utf-8")
    starts = [(m.start(), m.group(1)) for m in re.finditer(r'AddTool\(\s*"([^"]+)"', src)]
    tools = {}
    for k, (pos, name) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(src)
        tools[name] = set(re.findall(r'Property\("([^"]+)"', src[pos:end]))
    return tools


def tool_names():
    return set(tool_params())


def get(obj, dotted):
    for part in dotted.split("."):
        if not isinstance(obj, dict) or part not in obj:
            return None
        obj = obj[part]
    return obj


def validate(path):
    errors, warnings = [], []
    text = path.read_text(encoding="utf-8")
    if RAW_STRING_END in text:
        errors.append(f"contains the raw-string terminator {RAW_STRING_END!r} used by build.sh")
    if API_KEY_RE.search(text):
        errors.append("contains something that looks like a Google API key (AIza...)")
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        return [f"invalid JSON: {e}"], warnings

    if not isinstance(data, dict):
        return ["top level must be an object"], warnings
    if not isinstance(data.get("common_rules"), str) or not data["common_rules"].strip():
        warnings.append("common_rules is missing or empty")

    chars = data.get("characters")
    if not isinstance(chars, list) or not chars:
        errors.append("characters must be a non-empty array")
        chars = []

    ids = set()
    for i, c in enumerate(chars):
        where = f"characters[{i}]"
        if not isinstance(c, dict):
            errors.append(f"{where}: must be an object")
            continue
        cid = c.get("id")
        if isinstance(cid, str):
            where = f"character '{cid}'"
        for field in REQUIRED_CHARACTER:
            v = get(c, field)
            if not isinstance(v, str) or not v.strip():
                errors.append(f"{where}: missing or empty '{field}'")
        if isinstance(cid, str):
            if not ID_RE.match(cid):
                errors.append(f"{where}: id must match {ID_RE.pattern} (lowercase, NVS key limit)")
            if cid in ids:
                errors.append(f"{where}: duplicate id")
            ids.add(cid)
        look = c.get("look") if isinstance(c.get("look"), dict) else {}
        shape = look.get("shape")
        if isinstance(shape, str) and shape and shape not in KNOWN_SHAPES:
            warnings.append(f"{where}: shape '{shape}' is not drawn natively, falls back to cloud")
        for key in COLOR_KEYS:
            v = look.get(key)
            if v is None:
                warnings.append(f"{where}: look.{key} not set")
            elif not isinstance(v, str) or not COLOR_RE.match(v):
                errors.append(f"{where}: look.{key} must be '#RRGGBB', got {v!r}")
        persona = c.get("persona")
        if isinstance(persona, str) and len(persona.encode("utf-8")) > NVS_OVERRIDE_LIMIT - 200:
            warnings.append(f"{where}: persona is long; an edited copy may not fit in NVS")
        for opt in ("kind", "tagline"):
            if not c.get(opt):
                warnings.append(f"{where}: '{opt}' is empty (used in the prompt and on screen)")
        faces = FIRMWARE / "assets" / "faces"
        if isinstance(cid, str) and faces.is_dir() and not any(faces.glob(f"{cid}_*.gif")):
            warnings.append(f"{where}: no assets/faces/{cid}_*.gif (run assets/make_faces.py)")

    default = data.get("default_character")
    if not isinstance(default, str) or default not in ids:
        errors.append(f"default_character {default!r} is not one of the character ids {sorted(ids)}")

    apps = data.get("apps", [])
    if not isinstance(apps, list):
        errors.append("apps must be an array")
        apps = []
    app_ids = set()
    for i, a in enumerate(apps):
        where = f"apps[{i}]"
        if not isinstance(a, dict):
            errors.append(f"{where}: must be an object")
            continue
        if isinstance(a.get("id"), str):
            where = f"app '{a['id']}'"
        for field in REQUIRED_APP:
            v = a.get(field)
            if not isinstance(v, str) or not v.strip():
                errors.append(f"{where}: missing or empty '{field}'")
        aid = a.get("id")
        if isinstance(aid, str):
            if not APP_ID_RE.match(aid):
                errors.append(f"{where}: id must match {APP_ID_RE.pattern}")
            if aid in app_ids:
                errors.append(f"{where}: duplicate id")
            app_ids.add(aid)

    # Every tool mentioned in a prompt must exist, and every tool name must be English snake_case.
    tools = tool_names()
    for name in sorted(tools):
        if not TOOL_NAME_RE.match(name) or len(name) > 64:
            errors.append(f"tool '{name}': name must match {TOOL_NAME_RE.pattern}")
    if tools:
        for ref in sorted(set(TOOL_REF_RE.findall(text))):
            if ref not in tools:
                errors.append(f"prompt mentions tool '{ref}', which assistant_tools.cc does not register")

    # Tool calls made by the settings page must use real tools and parameter names.
    params = tool_params()
    page = FIRMWARE / "web" / "index.html"
    if params and page.is_file():
        html = page.read_text(encoding="utf-8")
        for name, args in re.findall(r"name:'([a-z_.]+)',arguments:\{([^}]*)\}", html):
            if name not in params:
                errors.append(f"index.html calls unknown tool '{name}'")
                continue
            for arg in re.findall(r"([A-Za-z_]\w*)\s*:", args):
                if arg not in params[name]:
                    errors.append(f"index.html passes '{arg}' to '{name}', which takes {sorted(params[name])}")

    # Every on-screen string needs a translation in every non-Vietnamese language.
    keys = ui_strings()
    for lang in data.get("languages", []):
        if not isinstance(lang, dict) or lang.get("id") == "vi":
            continue
        ui = lang.get("ui") or {}
        missing = sorted(k for k in keys if k not in ui)
        if missing:
            errors.append(f"language '{lang.get('id')}': no translation for {missing}")

    return errors, warnings


def main(argv):
    path = Path(argv[1]) if len(argv) > 1 else DEFAULT_PATH
    errors, warnings = validate(path)
    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    if errors:
        print(f"{path}: {len(errors)} error(s)")
        return 1
    data = json.loads(path.read_text(encoding="utf-8"))
    print(f"{path}: OK ({len(data['characters'])} characters, {len(data.get('apps', []))} apps, "
          f"{len(warnings)} warning(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
