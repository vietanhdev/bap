#!/usr/bin/env python3
"""Zing MP3 web API prototype (stdlib only).

Signing scheme (extracted from https://zmdjs.zmdcdn.me/zmp3-desktop/v1.22.4/static/js/main.min.js,
module exporting apiKey/secret, function `v(e,t)` next to `k(e,t)` that sets ctime/version/sig/apiKey):

    SIG_KEYS = ["ctime", "id", "type", "page", "count", "version"]
    params_str = "".join(f"{k}={v}" for k, v in sorted(params) if k in SIG_KEYS and v != "")
                 (keys and values are encodeURIComponent()'d; NO separator between pairs)
    h   = sha256_hex(params_str)
    sig = hmac_sha512_hex(key=SECRET, msg=path + h)        # path like "/api/v2/song/get/streaming"
    GET https://zingmp3.vn<path>?<params>&ctime=..&version=..&sig=..&apiKey=..

Note `q` (search query) is NOT part of the signature.

Usage:
    python3 zing_proto.py "Hà Nội mùa thu"
    python3 zing_proto.py --sig-vectors     # print fixed test vectors (no network)
"""
import gzip
import hashlib
import hmac
import http.cookiejar
import json
import sys
import time
import urllib.parse
import urllib.request

BASE = "https://zingmp3.vn"
VERSION = "1.22.4"
API_KEY = "X5BM3w8N7MKozC0B85o4KMlzLZKhV00y"
SECRET = "acOrvUS15XRW2o9JksiK1KgQ6Vbds8ZW"
SIG_KEYS = ("ctime", "id", "type", "page", "count", "version")
UA = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/130.0 Safari/537.36")


def enc(s):
    # JS encodeURIComponent equivalent
    return urllib.parse.quote(str(s), safe="-_.!~*'()")


def make_sig(path, params):
    parts = [f"{enc(k)}={enc(params[k])}" for k in sorted(params)
             if k in SIG_KEYS and params[k] not in (None, "")]
    h = hashlib.sha256("".join(parts).encode()).hexdigest()
    return hmac.new(SECRET.encode(), (path + h).encode(), hashlib.sha512).hexdigest()


class Zing:
    def __init__(self):
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def _open(self, url, max_bytes=None):
        req = urllib.request.Request(url, headers={
            "User-Agent": UA, "Accept-Encoding": "gzip", "Referer": BASE + "/"})
        with self.opener.open(req, timeout=15) as r:
            body = r.read(max_bytes) if max_bytes else r.read()
            if r.headers.get("Content-Encoding") == "gzip" and not max_bytes:
                body = gzip.decompress(body)
            return r.status, dict(r.headers), body

    def init_cookies(self):
        st, hdr, _ = self._open(BASE + "/")
        print(f"[cookies] GET / -> {st}; cookies: {[c.name for c in self.jar]}")

    def api(self, path, **params):
        path = "/api/v2" + path
        params = {k: str(v) for k, v in params.items()}
        params["ctime"] = str(int(time.time()))
        params["version"] = VERSION
        params["sig"] = make_sig(path, params)
        params["apiKey"] = API_KEY
        url = BASE + path + "?" + urllib.parse.urlencode(params, quote_via=urllib.parse.quote)
        st, hdr, body = self._open(url)
        j = json.loads(body)
        print(f"[api] {path} -> HTTP {st}, err={j.get('err')} msg={j.get('msg')!r} ({len(body)} B)")
        return j


def song_brief(s):
    return {
        "id": s.get("encodeId"), "title": s.get("title"), "artists": s.get("artistsNames"),
        "duration": s.get("duration"), "streamingStatus": s.get("streamingStatus"),
        "isWorldWide": s.get("isWorldWide"), "previewInfo": bool(s.get("previewInfo")),
    }


def is_vip(s):
    # streamingStatus 1 = free, 2 = VIP/Premium-only (preview only)
    return s.get("streamingStatus", 1) != 1


def main():
    if "--sig-vectors" in sys.argv:
        for path, p in [
            ("/api/v2/song/get/streaming", {"id": "ZWZB969E", "ctime": "1700000000", "version": "1.22.4"}),
            ("/api/v2/search", {"type": "song", "page": "1", "count": "10", "ctime": "1700000000",
                                "version": "1.22.4", "q": "Hà Nội mùa thu"}),
            ("/api/v2/search/multi", {"ctime": "1700000000", "version": "1.22.4", "q": "Sơn Tùng"}),
        ]:
            print(path, p, "\n  sig =", make_sig(path, p))
        return
    query = sys.argv[1] if len(sys.argv) > 1 else "Hà Nội mùa thu"
    z = Zing()
    z.init_cookies()
    j = z.api("/search", q=query, type="song", page=1, count=10)
    items = (j.get("data") or {}).get("items") or []
    print(f"[search] {len(items)} songs; data keys: {list((j.get('data') or {}).keys())}")
    for s in items:
        print("   ", song_brief(s))
    free = [s for s in items if not is_vip(s)]
    if not free:
        print("no free song"); return
    song = free[0]
    sj = z.api("/song/get/streaming", id=song["encodeId"])
    data = sj.get("data") or {}
    print("[stream] data:", {k: (v[:90] + "...") if isinstance(v, str) else v for k, v in data.items()})
    url = data.get("128")
    if not url or url == "VIP":
        print("no 128 URL"); return
    st, hdr, chunk = z._open(url, max_bytes=65536)
    print(f"[audio] HTTP {st} type={hdr.get('Content-Type')} len={hdr.get('Content-Length')} "
          f"read={len(chunk)} head={chunk[:4]!r}")
    # URL form: https://a128-z3.zmdcdn.me/<hash>?authen=exp=<unix>~acl=/<hash>*~hmac=<hex>
    exp = url.split("exp=", 1)[1].split("~", 1)[0] if "exp=" in url else None
    if exp:
        print(f"[audio] URL expires in {int(exp) - int(time.time())} s")


if __name__ == "__main__":
    main()
