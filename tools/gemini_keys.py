#!/usr/bin/env python3
"""Create and manage Google Gemini API keys for SenseCap devices.

Stdlib only. Shells out to the logged-in `gcloud` CLI (always with --format=json).

Safety rules this tool follows:
  * Key strings are never written to stdout/stderr; only a masked form (first 4 + last 4).
  * Keys are sent to Google / the device in headers or request bodies, never in URLs or argv.
  * Key files are written 0600, their directory 0700.
  * `delete` refuses without --yes; `rotate` never deletes the old key, it prints the command.
  * `--dry-run` on create/rotate/delete prints the mutating gcloud commands instead of running them.

Usage:
  tools/gemini_keys.py list
  tools/gemini_keys.py create NAME [--dry-run]
  tools/gemini_keys.py rotate NAME [--device HOST] [--dry-run]
  tools/gemini_keys.py check [NAME | --file PATH]
  tools/gemini_keys.py delete UID --yes [--dry-run]
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT_FILE = ROOT / ".gcp-project"
SECRETS_DIR = ROOT / "secrets"
KEYS_DIR = SECRETS_DIR / "keys"
ACTIVE_KEY_FILE = SECRETS_DIR / "GEMINI_API_KEY"

API_SERVICE = "generativelanguage.googleapis.com"
MODELS_URL = "https://generativelanguage.googleapis.com/v1beta/models"
DEFAULT_DEVICE = "bap.local"
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,47}$")
UID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")

BILLING_HINT = (
    "hint: 429 / RESOURCE_EXHAUSTED means the free-tier quota is used up (or the model is not\n"
    "      available on the free tier). For the paid tier, link a billing account to the project:\n"
    "        gcloud billing accounts list\n"
    "        gcloud billing projects link {project} --billing-account=XXXXXX-XXXXXX-XXXXXX\n"
    "      or open https://aistudio.google.com/apikey and choose 'Set up billing' for this project."
)


class ToolError(Exception):
    pass


# ---------------------------------------------------------------- helpers


def mask(key: str) -> str:
    if len(key) < 12:
        return "****"
    return f"{key[:4]}...{key[-4:]}"


def err(msg: str) -> None:
    print(msg, file=sys.stderr)


def read_project(override: str | None) -> str:
    if override:
        return override
    try:
        p = PROJECT_FILE.read_text().strip()
    except FileNotFoundError:
        raise ToolError(f"no --project given and {PROJECT_FILE} does not exist")
    if not p:
        raise ToolError(f"{PROJECT_FILE} is empty")
    return p


def check_name(name: str) -> str:
    if not NAME_RE.match(name):
        raise ToolError(f"invalid key name {name!r}: use letters, digits, '-' or '_' (max 48 chars)")
    return name


def read_key_file(path: Path) -> str:
    try:
        st = path.stat()
    except FileNotFoundError:
        raise ToolError(f"key file not found: {path}")
    if st.st_mode & 0o077:
        err(f"warning: {path} is mode {oct(st.st_mode & 0o777)}; fixing to 0600")
        os.chmod(path, 0o600)
    key = path.read_text().strip()
    if not key:
        raise ToolError(f"key file is empty: {path}")
    return key


def write_key_file(path: Path, key: str, *, overwrite: bool) -> None:
    """Write a key with 0600 perms; directory 0700. Atomic replace when overwriting."""
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    if not overwrite:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(key + "\n")
        return
    fd, tmp = tempfile.mkstemp(prefix=".tmp-", dir=path.parent)  # mkstemp creates 0600
    try:
        with os.fdopen(fd, "w") as f:
            f.write(key + "\n")
        os.chmod(tmp, 0o600)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        raise


def show_cmd(cmd: list[str]) -> None:
    print("  [dry-run] would run: " + shlex.join(cmd))


def gcloud(args: list[str], project: str) -> object:
    """Run a gcloud command with JSON output and return the parsed result.

    stdout is captured (it may contain a key string) and never echoed. stderr is captured and
    only shown on failure, after being scrubbed of anything that looks like an API key.
    """
    cmd = ["gcloud", *args, f"--project={project}", "--format=json", "--quiet"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except FileNotFoundError:
        raise ToolError("gcloud not found on PATH")
    if proc.returncode != 0:
        msg = re.sub(r"AIza[0-9A-Za-z_\-]{20,}", "<redacted>", proc.stderr.strip())
        raise ToolError(f"gcloud {' '.join(args[:3])} failed (exit {proc.returncode}):\n{msg}")
    out = proc.stdout.strip()
    return json.loads(out) if out else None


def gcloud_cmd(args: list[str], project: str) -> list[str]:
    return ["gcloud", *args, f"--project={project}", "--format=json", "--quiet"]


def list_keys(project: str) -> list[dict]:
    return gcloud(["services", "api-keys", "list"], project) or []


def fmt_restrictions(r: dict | None) -> str:
    if not r:
        return "NONE (unrestricted!)"
    parts = []
    for t in r.get("apiTargets", []):
        s = t.get("service", "?")
        if t.get("methods"):
            s += "[" + ",".join(t["methods"]) + "]"
        parts.append("api:" + s)
    for k, label in (
        ("browserKeyRestrictions", "referrers"),
        ("serverKeyRestrictions", "ips"),
        ("androidKeyRestrictions", "android"),
        ("iosKeyRestrictions", "ios"),
    ):
        if k in r:
            parts.append(label)
    return ", ".join(parts) or "NONE (unrestricted!)"


# ---------------------------------------------------------------- Gemini API probe


def probe_key(key: str) -> tuple[int, str, list[str]]:
    """GET /v1beta/models with the key in a header.

    Returns (http_status, error_status_or_empty, live_model_names). The body is parsed in
    memory only; nothing from it is printed except model names / Google's error status code.
    """
    live: list[str] = []
    page_token = ""
    for _ in range(20):  # pagination guard
        q = {"pageSize": "1000"}
        if page_token:
            q["pageToken"] = page_token
        req = urllib.request.Request(
            MODELS_URL + "?" + urllib.parse.urlencode(q),
            headers={"x-goog-api-key": key, "Accept": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=20) as resp:
                status = resp.status
                body = json.loads(resp.read() or b"{}")
        except urllib.error.HTTPError as e:
            reason = ""
            try:
                reason = json.loads(e.read() or b"{}").get("error", {}).get("status", "")
            except (ValueError, AttributeError):
                pass
            return e.code, reason, []
        except urllib.error.URLError as e:
            raise ToolError(f"network error reaching Gemini API: {e.reason}")
        for m in body.get("models", []):
            if "bidiGenerateContent" in m.get("supportedGenerationMethods", []):
                live.append(m.get("name", "?").removeprefix("models/"))
        page_token = body.get("nextPageToken", "")
        if not page_token:
            return status, "", sorted(set(live))
    return status, "", sorted(set(live))


def report_probe(status: int, reason: str, project: str) -> None:
    print(f"  HTTP {status}" + (f" ({reason})" if reason else ""))
    if status == 429 or reason == "RESOURCE_EXHAUSTED":
        print(BILLING_HINT.format(project=project))


def verify_new_key(key: str, project: str, wait_s: int = 120) -> bool:
    """New keys can take a little while to propagate; retry on 400/403 for up to wait_s."""
    deadline = time.monotonic() + wait_s
    delay = 5
    while True:
        status, reason, _ = probe_key(key)
        if status == 200:
            print("  verify: HTTP 200 OK")
            return True
        if status == 429 or reason == "RESOURCE_EXHAUSTED":
            report_probe(status, reason, project)
            return False
        if status not in (400, 403) or time.monotonic() > deadline:
            print(f"  verify: HTTP {status}" + (f" ({reason})" if reason else ""))
            return False
        print(f"  verify: HTTP {status} ({reason or 'not ready'}), key may still be propagating; retry in {delay}s")
        time.sleep(delay)
        delay = min(delay * 2, 30)


# ---------------------------------------------------------------- device


def _device_opener() -> urllib.request.OpenerDirector:
    return urllib.request.build_opener(urllib.request.ProxyHandler({}))  # LAN: never via a proxy


def _device_url(host: str) -> str:
    host = host.strip().rstrip("/")
    if not re.match(r"^[A-Za-z0-9.\-]+(:\d+)?$", host):
        raise ToolError(f"invalid device host {host!r}")
    return f"http://{host}/api/config"


def device_masked(host: str) -> str:
    try:
        with _device_opener().open(_device_url(host), timeout=10) as r:
            return json.loads(r.read()).get("api_key_masked", "")
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise ToolError(f"cannot read config from device {host}: {getattr(e, 'reason', e)}")


def device_push(host: str, key: str) -> None:
    req = urllib.request.Request(
        _device_url(host),
        data=json.dumps({"api_key": key}).encode(),
        headers={"Content-Type": "application/json", "X-Bap": "1"},
        method="POST",
    )
    try:
        with _device_opener().open(req, timeout=15) as r:
            ok = json.loads(r.read() or b"{}").get("ok")
    except urllib.error.HTTPError as e:
        raise ToolError(f"device {host} rejected the update: HTTP {e.code}")
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise ToolError(f"cannot push to device {host}: {getattr(e, 'reason', e)}")
    if ok is not True:
        raise ToolError(f"device {host} did not acknowledge the update")


def device_matches(masked: str, key: str) -> bool:
    # Firmware masks as first4 + bullets + last4.
    return len(masked) >= 8 and masked[:4] == key[:4] and masked[-4:] == key[-4:]


# ---------------------------------------------------------------- key creation


def create_key(display_name: str, project: str, dry_run: bool) -> tuple[str, str] | None:
    args = [
        "services", "api-keys", "create",
        f"--display-name={display_name}",
        f"--api-target=service={API_SERVICE}",
    ]
    if dry_run:
        show_cmd(gcloud_cmd(args, project))
        return None
    op = gcloud(args, project) or {}
    resp = op.get("response", {}) if isinstance(op, dict) else {}
    uid = resp.get("uid", "")
    key = resp.get("keyString", "")
    if not key and resp.get("name"):
        key = (gcloud(["services", "api-keys", "get-key-string", resp["name"]], project) or {}).get("keyString", "")
    if not uid or not key:
        raise ToolError("gcloud create did not return a key uid/string; check `list` and the console")
    return uid, key


def ensure_name_free(name: str, project: str) -> None:
    if (KEYS_DIR / name).exists():
        raise ToolError(f"{KEYS_DIR / name} already exists; pick another name")
    if any(k.get("displayName") == name for k in list_keys(project)):
        raise ToolError(f"a key with display name {name!r} already exists in {project}")


# ---------------------------------------------------------------- commands


def cmd_list(a: argparse.Namespace) -> int:
    keys = list_keys(a.project)
    if not keys:
        print(f"No API keys in project {a.project}.")
        return 0
    keys.sort(key=lambda k: k.get("createTime", ""))
    print(f"API keys in project {a.project}:")
    rows = [("DISPLAY NAME", "UID", "CREATED (UTC)", "RESTRICTIONS")]
    for k in keys:
        rows.append((
            k.get("displayName", "") or "(none)",
            k.get("uid", ""),
            k.get("createTime", "")[:19].replace("T", " "),
            fmt_restrictions(k.get("restrictions")),
        ))
    w = [max(len(r[i]) for r in rows) for i in range(3)]
    for r in rows:
        print(f"  {r[0]:<{w[0]}}  {r[1]:<{w[1]}}  {r[2]:<{w[2]}}  {r[3]}")
    local = sorted(p.name for p in KEYS_DIR.iterdir()) if KEYS_DIR.is_dir() else []
    print(f"Local key files in {KEYS_DIR}: {', '.join(local) if local else '(none)'}")
    return 0


def cmd_create(a: argparse.Namespace) -> int:
    name = check_name(a.name)
    ensure_name_free(name, a.project)
    path = KEYS_DIR / name
    print(f"Creating key {name!r} in {a.project} (restricted to {API_SERVICE})")
    res = create_key(name, a.project, a.dry_run)
    if res is None:
        print(f"  [dry-run] would write key to {path} (0600, dir 0700)")
        return 0
    uid, key = res
    write_key_file(path, key, overwrite=False)
    print(f"  uid:  {uid}")
    print(f"  key:  {mask(key)}")
    print(f"  file: {path} (0600)")
    return 0


def find_uid_for_key(key: str, keys: list[dict], project: str, dry_run: bool) -> str | None:
    """Find which project key holds `key` by fetching key strings into memory (never printed)."""
    for k in keys:
        args = ["services", "api-keys", "get-key-string", k["name"]]
        if dry_run:
            show_cmd(gcloud_cmd(args, project))
            continue
        try:
            ks = (gcloud(args, project) or {}).get("keyString", "")
        except ToolError:
            continue
        if ks and ks == key:
            return k.get("uid")
    return None


def cmd_rotate(a: argparse.Namespace) -> int:
    base = check_name(a.name)
    new_name = f"{base}-{_dt.date.today():%Y%m%d}"
    check_name(new_name)
    project, dry = a.project, a.dry_run
    print(f"Rotating: new key {new_name!r} in {project}" + (" [DRY RUN]" if dry else ""))

    # 1. Identify the currently active key (read-only).
    keys = list_keys(project)
    if (KEYS_DIR / new_name).exists() or any(k.get("displayName") == new_name for k in keys):
        raise ToolError(f"key {new_name!r} already exists (rotate at most once per day per NAME)")
    old_key = read_key_file(ACTIVE_KEY_FILE) if ACTIVE_KEY_FILE.exists() else None
    old_uid = None
    if old_key:
        print(f"Step 1: locate the uid of the current key in {ACTIVE_KEY_FILE.name} ({mask(old_key)})")
        old_uid = find_uid_for_key(old_key, keys, project, dry)
        if old_uid:
            print(f"  current key uid: {old_uid}")
        elif not dry:
            print("  current key not found in this project (it may live elsewhere)")
    if a.device:
        print(f"Step 1b: check device {a.device} is reachable")
        print(f"  device currently uses: {device_masked(a.device) or '(no key)'}")

    # 2. Create + store the new key.
    print(f"Step 2: create key {new_name!r}")
    res = create_key(new_name, project, dry)
    if dry:
        print(f"  [dry-run] would write new key to {KEYS_DIR / new_name} (0600)")
        print(f"  [dry-run] would verify it: GET {MODELS_URL} with header x-goog-api-key (expect HTTP 200)")
        if a.device:
            print(f"  [dry-run] would POST {_device_url(a.device)} (X-Bap: 1, body {{\"api_key\": ...}})")
            print("  [dry-run] would GET it back and compare api_key_masked")
        print(f"  [dry-run] would replace {ACTIVE_KEY_FILE} (0600)")
        if old_uid or old_key:
            print("  [dry-run] would print the delete command for the old key (not run):")
            print("    " + shlex.join(["gcloud", "services", "api-keys", "delete", old_uid or "<OLD_UID>", f"--project={project}"]))
        return 0
    assert res is not None
    new_uid, new_key = res
    write_key_file(KEYS_DIR / new_name, new_key, overwrite=False)
    print(f"  uid {new_uid}, key {mask(new_key)}, saved to {KEYS_DIR / new_name}")

    cleanup = shlex.join(["gcloud", "services", "api-keys", "delete", new_uid, f"--project={project}"])

    # 3. Verify it works.
    print("Step 3: verify new key against the Gemini API")
    if not verify_new_key(new_key, project):
        err("ABORT: new key did not verify. Device and GEMINI_API_KEY were NOT changed.")
        err(f"  To discard the new key: {cleanup}")
        return 1

    # 4. Push to device.
    if a.device:
        print(f"Step 4: push new key to device {a.device}")
        try:
            device_push(a.device, new_key)
            got = device_masked(a.device)
        except ToolError as e:
            err(f"ABORT: {e}. {ACTIVE_KEY_FILE.name} was NOT changed; the device may still use the old key.")
            return 1
        if not device_matches(got, new_key):
            err(f"ABORT: device reports {got!r}, expected {mask(new_key)}. {ACTIVE_KEY_FILE.name} NOT changed.")
            return 1
        print(f"  device confirms: {got}")
    else:
        print("Step 4: no --device given, skipping device push")

    # 5. Update the active key file.
    write_key_file(ACTIVE_KEY_FILE, new_key, overwrite=True)
    print(f"Step 5: {ACTIVE_KEY_FILE} now holds {mask(new_key)} (0600)")

    # 6. Old key: print, never delete.
    print("Done. The old key was NOT deleted.")
    if old_uid:
        print(f"  old key uid: {old_uid}")
        print("  once everything using it is updated, delete it with:")
        print("    " + shlex.join(["gcloud", "services", "api-keys", "delete", old_uid, f"--project={project}"]))
        print(f"  or: tools/gemini_keys.py delete {old_uid} --yes")
    elif old_key:
        print("  the previous key's uid could not be found in this project; check `list`.")
    return 0


def cmd_check(a: argparse.Namespace) -> int:
    if a.file:
        path = Path(a.file).expanduser()
    elif a.name:
        path = KEYS_DIR / check_name(a.name)
    else:
        path = ACTIVE_KEY_FILE
    key = read_key_file(path)
    print(f"Checking {path} ({mask(key)})")
    status, reason, live = probe_key(key)
    report_probe(status, reason, a.project)
    if status != 200:
        return 1
    print(f"  Live models visible ({len(live)}):" if live else "  Live models visible: none")
    for m in live:
        print(f"    {m}")
    return 0


def cmd_delete(a: argparse.Namespace) -> int:
    uid = a.uid.strip().lower()
    if not UID_RE.match(uid):
        raise ToolError(f"{a.uid!r} does not look like a key uid (see `list`)")
    match = next((k for k in list_keys(a.project) if k.get("uid") == uid), None)
    if match is None:
        raise ToolError(f"no key with uid {uid} in project {a.project}")
    label = match.get("displayName") or "(no display name)"
    if not a.yes:
        err(f"Refusing to delete key {label!r} ({uid}) without --yes.")
        return 2
    args = ["services", "api-keys", "delete", uid]
    print(f"Deleting key {label!r} ({uid}) from {a.project}")
    if a.dry_run:
        show_cmd(gcloud_cmd(args, a.project))
        return 0
    gcloud(args, a.project)
    print("  deleted (can be restored within 30 days: gcloud services api-keys undelete " + uid + ")")
    return 0


# ---------------------------------------------------------------- main


def main(argv: list[str] | None = None) -> int:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--project", help=f"GCP project id (default: contents of {PROJECT_FILE.name})")
    dry = argparse.ArgumentParser(add_help=False)
    dry.add_argument("--dry-run", action="store_true", help="print mutating gcloud commands instead of running them")

    p = argparse.ArgumentParser(description="Manage Gemini API keys for SenseCap devices (keys are never printed).")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("list", parents=[common], help="list keys in the project")
    s.set_defaults(func=cmd_list)

    s = sub.add_parser("create", parents=[common, dry], help="create a Gemini-restricted key, save to secrets/keys/NAME")
    s.add_argument("name")
    s.set_defaults(func=cmd_create)

    s = sub.add_parser("rotate", parents=[common, dry], help="create NAME-yyyymmdd, verify, push to device, make active")
    s.add_argument("name")
    s.add_argument("--device", nargs="?", const=DEFAULT_DEVICE, metavar="HOST",
                   help=f"push to this device (bare --device = {DEFAULT_DEVICE})")
    s.set_defaults(func=cmd_rotate)

    s = sub.add_parser("check", parents=[common], help="verify a stored key and list visible Live models")
    g = s.add_mutually_exclusive_group()
    g.add_argument("name", nargs="?", help="key under secrets/keys/ (default: secrets/GEMINI_API_KEY)")
    g.add_argument("--file", metavar="PATH", help="key file to check")
    s.set_defaults(func=cmd_check)

    s = sub.add_parser("delete", parents=[common, dry], help="delete a key by uid (requires --yes)")
    s.add_argument("uid")
    s.add_argument("--yes", action="store_true", help="really delete")
    s.set_defaults(func=cmd_delete)

    a = p.parse_args(argv)
    os.umask(0o077)
    try:
        a.project = read_project(a.project)
        return a.func(a)
    except ToolError as e:
        err(f"error: {e}")
        return 1
    except KeyboardInterrupt:
        err("interrupted")
        return 130


if __name__ == "__main__":
    sys.exit(main())
