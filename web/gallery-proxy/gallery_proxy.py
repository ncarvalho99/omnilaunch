"""Small SteamGridDB proxy used by SwitchU's game-art gallery.

The Switch client never receives the SteamGridDB API key.  This service exposes
only search and read-only artwork metadata, caches upstream responses and is
intended to be reached through the Cloudflare Tunnel at gallery.nclabs.dev.
"""

from __future__ import annotations

import hmac
import ipaddress
import json
import os
import threading
import time
from collections import defaultdict, deque
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

from fastapi import FastAPI, HTTPException, Request as FastApiRequest
from fastapi.responses import JSONResponse


APP_NAME = "switchu-gallery"
STEAMGRIDDB_API = "https://www.steamgriddb.com/api/v2"
CACHE_TTL_SECONDS = 15 * 60
MAX_CACHE_ENTRIES = 2000
RATE_LIMIT_REQUESTS = 40
RATE_LIMIT_WINDOW_SECONDS = 60
GRID_DIMENSIONS = frozenset({"600x900", "342x482", "660x930", "512x512", "1024x1024", "460x215", "920x430"})
HERO_DIMENSIONS = frozenset({"1920x620", "3840x1240", "1600x650"})


def _trusted_networks() -> tuple[ipaddress.IPv4Network | ipaddress.IPv6Network, ...]:
    raw = os.environ.get("SWITCHU_TRUSTED_NETWORKS", "172.26.128.0/26,10.0.0.0/26,127.0.0.0/8,::1/128")
    try:
        return tuple(ipaddress.ip_network(value.strip()) for value in raw.split(",") if value.strip())
    except ValueError as exc:
        raise RuntimeError("SWITCHU_TRUSTED_NETWORKS contains an invalid network") from exc


TRUSTED_NETWORKS = _trusted_networks()

# Official-client gate. Builds made by the project carry a secret key that is
# injected at compile time and never committed, so a build compiled from the
# public source (for example a fork) cannot present one. "monitor" only logs,
# "enforce" rejects. Keys are read from a file so they can be rotated live.
CLIENT_AUTH_MODE = os.environ.get("SWITCHU_CLIENT_AUTH", "monitor").strip().lower()
if CLIENT_AUTH_MODE not in {"off", "monitor", "enforce"}:
    raise RuntimeError("SWITCHU_CLIENT_AUTH must be off, monitor or enforce")
CLIENT_KEYS_FILE = os.environ.get("SWITCHU_CLIENT_KEYS_FILE", "/etc/switchu/client-keys")
CLIENT_KEY_HEADER = "x-switchu-key"
_client_keys_state: tuple[float, tuple[bytes, ...]] = (-1.0, ())
_client_keys_lock = threading.Lock()
_client_auth_logged: dict[str, float] = {}


def _client_keys() -> tuple[bytes, ...]:
    global _client_keys_state
    try:
        mtime = os.stat(CLIENT_KEYS_FILE).st_mtime
    except OSError:
        return ()
    with _client_keys_lock:
        if _client_keys_state[0] != mtime:
            try:
                with open(CLIENT_KEYS_FILE, encoding="utf-8") as handle:
                    lines = (line.strip() for line in handle)
                    keys = tuple(line.encode() for line in lines if line and not line.startswith("#"))
            except OSError:
                return _client_keys_state[1]
            _client_keys_state = (mtime, keys)
        return _client_keys_state[1]


def _client_authorized(request: FastApiRequest) -> bool:
    presented = (request.headers.get("x-switchu-key") or request.headers.get("x-omnilaunch-key") or "").strip().encode()
    if not presented or len(presented) > 256:
        return False
    matched = False
    for key in _client_keys():
        matched |= hmac.compare_digest(presented, key)
    return matched


def _log_unauthorized_client(request: FastApiRequest, identity: str) -> None:
    # One line per client per ten minutes keeps a hostile client from flooding
    # the journal while still showing who is calling without a key.
    now = time.monotonic()
    with _client_keys_lock:
        if len(_client_auth_logged) > 2000:
            for stale in [k for k, t in _client_auth_logged.items() if now - t >= 600]:
                _client_auth_logged.pop(stale, None)
        if now - _client_auth_logged.get(identity, -600.0) < 600:
            return
        _client_auth_logged[identity] = now
    agent = request.headers.get("user-agent", "")[:80]
    print(f"client-auth: no valid key mode={CLIENT_AUTH_MODE} ip={identity} path={request.url.path} ua={agent!r}", flush=True)


_cache: dict[str, tuple[float, Any]] = {}
_cache_lock = threading.Lock()
_rate_windows: dict[str, deque[float]] = defaultdict(deque)
_rate_lock = threading.Lock()

app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None, title=APP_NAME)


def _is_trusted_proxy(host: str | None) -> bool:
    if not host:
        return False
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        return False
    return any(address in network for network in TRUSTED_NETWORKS)


def _client_identity(request: FastApiRequest) -> str:
    # This header is set by the Cloudflare connector. It is read only after the
    # source network gate, so a public client cannot forge a rate-limit identity.
    forwarded = request.headers.get("cf-connecting-ip", "").strip()
    if forwarded:
        return forwarded
    return request.client.host if request.client else "unknown"


def _allow_request(identity: str) -> bool:
    now = time.monotonic()
    with _rate_lock:
        if len(_rate_windows) > 2000:
            stale_keys = [k for k, w in _rate_windows.items() if not w or now - w[-1] >= RATE_LIMIT_WINDOW_SECONDS]
            for k in stale_keys:
                _rate_windows.pop(k, None)
        window = _rate_windows[identity]
        while window and now - window[0] >= RATE_LIMIT_WINDOW_SECONDS:
            window.popleft()
        if len(window) >= RATE_LIMIT_REQUESTS:
            return False
        window.append(now)
        return True


def _cached(key: str) -> Any | None:
    now = time.monotonic()
    with _cache_lock:
        value = _cache.get(key)
        if value is None or value[0] <= now:
            _cache.pop(key, None)
            return None
        return value[1]


def _store_cache(key: str, value: Any) -> Any:
    now = time.monotonic()
    with _cache_lock:
        if len(_cache) >= MAX_CACHE_ENTRIES:
            expired = [k for k, (exp, _) in _cache.items() if exp <= now]
            for k in expired:
                _cache.pop(k, None)
            if len(_cache) >= MAX_CACHE_ENTRIES:
                overflow = len(_cache) - MAX_CACHE_ENTRIES + 1
                for _ in range(overflow):
                    _cache.pop(next(iter(_cache)), None)
        _cache[key] = (now + CACHE_TTL_SECONDS, value)
    return value


def _steamgriddb_get(path: str) -> list[dict[str, Any]]:
    cache_key = path
    cached = _cached(cache_key)
    if cached is not None:
        return cached

    api_key = os.environ.get("STEAMGRIDDB_API_KEY", "").strip()
    if not api_key:
        raise HTTPException(status_code=503, detail="SteamGridDB key is not configured")

    request = Request(
        f"{STEAMGRIDDB_API}/{path}",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Accept": "application/json",
            "User-Agent": "SwitchU/1.1 (+https://gallery.nclabs.dev)",
        },
        method="GET",
    )
    try:
        with urlopen(request, timeout=12) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise HTTPException(status_code=502, detail="SteamGridDB is unavailable") from exc

    if not payload.get("success") or not isinstance(payload.get("data"), list):
        raise HTTPException(status_code=502, detail="SteamGridDB returned an invalid response")
    return _store_cache(cache_key, payload["data"])


def _asset(item: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": item.get("id"),
        "url": item.get("url"),
        "thumb": item.get("thumb") or item.get("url"),
        "width": item.get("width"),
        "height": item.get("height"),
        "mime": item.get("mime"),
        "style": item.get("style"),
        "score": item.get("score"),
        "author": item.get("author", {}).get("name") if isinstance(item.get("author"), dict) else None,
    }


# ---- SteamGridDB key pairing (phone -> console) -----------------------------
#
# The console asks for a pairing (keyed like every other console call), shows a
# QR code for /pair/<id>, and polls. The phone opens that page (no client key:
# it is a browser), types its own SteamGridDB API key, which is checked against
# SteamGridDB once, and the console then takes it exactly once. Pairings live
# in memory only, expire after PAIR_TTL_SECONDS, and are deleted when read.
PAIR_TTL_SECONDS = 10 * 60
PAIR_MAX_ACTIVE = 500
PAIR_PAGE_LIMIT = 20  # phone page requests per identity per minute
PAIR_ID_ALPHABET = "abcdefghijkmnpqrstuvwxyz23456789"
_pairings: dict[str, dict[str, Any]] = {}
_pair_lock = threading.Lock()
_pair_rate: dict[str, deque[float]] = defaultdict(deque)
PUBLIC_PAIR_PREFIX = "/pair/"


def _pair_rate_ok(identity: str) -> bool:
    now = time.monotonic()
    with _pair_lock:
        if len(_pair_rate) > 2000:
            for k in [k for k, w in _pair_rate.items() if not w or now - w[-1] >= 60]:
                _pair_rate.pop(k, None)
        window = _pair_rate[identity]
        while window and now - window[0] >= 60:
            window.popleft()
        if len(window) >= PAIR_PAGE_LIMIT:
            return False
        window.append(now)
        return True


def _prune_pairings(now: float) -> None:
    for pid in [pid for pid, p in _pairings.items() if p["expires"] <= now]:
        _pairings.pop(pid, None)


def _new_pair_id() -> str:
    import secrets

    return "".join(secrets.choice(PAIR_ID_ALPHABET) for _ in range(10))


def _valid_pair_id(pair_id: str) -> bool:
    return len(pair_id) == 10 and all(c in PAIR_ID_ALPHABET for c in pair_id)


def _steamgriddb_key_works(api_key: str) -> bool | None:
    """True/False from SteamGridDB, None when it could not be asked."""
    request = Request(
        f"{STEAMGRIDDB_API}/search/autocomplete/mario",
        headers={"Authorization": f"Bearer {api_key}", "Accept": "application/json",
                 "User-Agent": "OmniLaunch-pairing/1.0 (+https://gallery.nclabs.dev)"},
        method="GET",
    )
    try:
        with urlopen(request, timeout=10) as response:
            payload = json.loads(response.read().decode("utf-8"))
            return bool(payload.get("success"))
    except HTTPError as exc:
        if exc.code in (401, 403):
            return False
        return None
    except (URLError, TimeoutError, json.JSONDecodeError):
        return None


_PAIR_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>OmniLaunch - SteamGridDB key</title>
<style>
body{{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#111418;color:#eef1f5;margin:0;padding:24px;}}
main{{max-width:440px;margin:0 auto;}}
h1{{font-size:1.35rem;margin:0 0 8px;}}
p{{color:#b9c2cd;line-height:1.45;}}
input{{width:100%;box-sizing:border-box;padding:14px;border-radius:10px;border:1px solid #39424d;background:#1b2027;color:#fff;font-size:1rem;}}
button{{margin-top:14px;width:100%;padding:14px;border:0;border-radius:10px;background:#3d7cf4;color:#fff;font-size:1rem;font-weight:600;}}
.msg{{margin-top:16px;padding:12px;border-radius:10px;background:#1b2027;}}
.ok{{border-left:4px solid #3fb950;}} .err{{border-left:4px solid #f85149;}}
a{{color:#7fb0ff;}}
</style></head><body><main>
<h1>SteamGridDB key for your console</h1>
{body}
</main></body></html>"""

_PAIR_FORM = """<p>Paste your SteamGridDB API key. You can find it at
<a href="https://www.steamgriddb.com/profile/preferences/api" rel="noreferrer noopener">steamgriddb.com &gt; Preferences &gt; API</a>.
It is sent once to your console and is not stored here.</p>
<form method="post" action="/pair/{pair_id}" autocomplete="off">
<input name="key" inputmode="text" autocapitalize="off" spellcheck="false" maxlength="64" placeholder="API key" required>
<button type="submit">Send to console</button>
</form>{note}"""


def _pair_html(body: str, status: int = 200) -> Any:
    from fastapi.responses import HTMLResponse

    return HTMLResponse(
        _PAIR_PAGE.format(body=body),
        status_code=status,
        headers={
            "Cache-Control": "no-store",
            "Referrer-Policy": "no-referrer",
            "X-Content-Type-Options": "nosniff",
            "X-Frame-Options": "DENY",
            "Content-Security-Policy": "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'",
        },
    )


def _pair_state(pair_id: str) -> dict[str, Any] | None:
    now = time.monotonic()
    with _pair_lock:
        _prune_pairings(now)
        return _pairings.get(pair_id)


@app.middleware("http")
async def restrict_to_tunnel(request: FastApiRequest, call_next: Any) -> Any:
    client_host = request.client.host if request.client else None
    if not _is_trusted_proxy(client_host):
        return JSONResponse(status_code=403, content={"detail": "Tunnel access required"})
    if request.url.path == "/health":
        return await call_next(request)
    if request.url.path.startswith(PUBLIC_PAIR_PREFIX):
        # The phone's browser: no client key, its own tighter rate limit.
        if not _pair_rate_ok(_client_identity(request)):
            return _pair_html('<div class="msg err">Too many attempts. Wait a minute.</div>', 429)
        return await call_next(request)
    identity = _client_identity(request)
    if CLIENT_AUTH_MODE != "off" and not _client_authorized(request):
        _log_unauthorized_client(request, identity)
        if CLIENT_AUTH_MODE == "enforce":
            return JSONResponse(status_code=401, content={"detail": "Official client required"})
    if not _allow_request(identity):
        return JSONResponse(status_code=429, content={"detail": "Too many requests"})
    return await call_next(request)


@app.get("/health")
def health() -> dict[str, object]:
    return {
        "service": APP_NAME,
        "status": "ok",
        "clientAuth": CLIENT_AUTH_MODE,
        "clientKeysLoaded": len(_client_keys()),
        "steamgriddbConfigured": bool(os.environ.get("STEAMGRIDDB_API_KEY", "").strip()),
    }


@app.get("/v1/search")
def search(query: str) -> dict[str, list[dict[str, Any]]]:
    query = " ".join(query.split())
    if not 2 <= len(query) <= 120:
        raise HTTPException(status_code=422, detail="query must contain 2 to 120 characters")

    games = _steamgriddb_get(f"search/autocomplete/{quote(query, safe='')}")
    return {
        "games": [
            {"id": game.get("id"), "name": game.get("name"), "types": game.get("types", [])}
            for game in games[:12]
        ]
    }


def _game_assets(game_id: int, kind: str, dimensions: str) -> dict[str, list[dict[str, Any]]]:
    if game_id <= 0:
        raise HTTPException(status_code=422, detail="game id must be positive")
    allowed_dimensions = GRID_DIMENSIONS if kind == "grids" else HERO_DIMENSIONS
    if dimensions not in allowed_dimensions:
        raise HTTPException(status_code=422, detail="unsupported artwork dimensions")
    items = _steamgriddb_get(f"{kind}/game/{game_id}?dimensions={quote(dimensions, safe=',x')}")
    # nxui decodes JPEG and PNG. Do not hand WebP/animated formats to the
    # Switch menu only for texture decoding to fail after the network cost.
    compatible = (item for item in items if item.get("mime") in {"image/jpeg", "image/png"})
    return {kind: [_asset(item) for item in list(compatible)[:48]]}


@app.get("/v1/games/{game_id}/grids")
def grids(game_id: int, dimensions: str = "600x900") -> dict[str, list[dict[str, Any]]]:
    return _game_assets(game_id, "grids", dimensions)


@app.get("/v1/games/{game_id}/heroes")
def heroes(game_id: int, dimensions: str = "1920x620") -> dict[str, list[dict[str, Any]]]:
    return _game_assets(game_id, "heroes", dimensions)


@app.post("/v1/pair")
def pair_create() -> dict[str, Any]:
    now = time.monotonic()
    with _pair_lock:
        _prune_pairings(now)
        if len(_pairings) >= PAIR_MAX_ACTIVE:
            raise HTTPException(status_code=503, detail="Too many pairings, try again later")
        pair_id = _new_pair_id()
        while pair_id in _pairings:
            pair_id = _new_pair_id()
        _pairings[pair_id] = {"expires": now + PAIR_TTL_SECONDS, "key": None}
    return {"id": pair_id, "url": f"https://gallery.nclabs.dev/pair/{pair_id}", "expiresIn": PAIR_TTL_SECONDS}


@app.get("/v1/pair/{pair_id}")
def pair_poll(pair_id: str) -> dict[str, Any]:
    if not _valid_pair_id(pair_id):
        raise HTTPException(status_code=404, detail="Unknown pairing")
    now = time.monotonic()
    with _pair_lock:
        _prune_pairings(now)
        pairing = _pairings.get(pair_id)
        if pairing is None:
            raise HTTPException(status_code=404, detail="Unknown pairing")
        key = pairing.get("key")
        if not key:
            return {"status": "waiting", "expiresIn": int(pairing["expires"] - now)}
        _pairings.pop(pair_id, None)  # handed over exactly once
    return {"status": "ready", "key": key}


@app.get("/pair/{pair_id}")
def pair_page(pair_id: str) -> Any:
    if not _valid_pair_id(pair_id) or _pair_state(pair_id) is None:
        return _pair_html('<div class="msg err">This code has expired. Open the pairing again on the console.</div>', 404)
    return _pair_html(_PAIR_FORM.format(pair_id=pair_id, note=""))


@app.post("/pair/{pair_id}")
async def pair_submit(pair_id: str, request: FastApiRequest) -> Any:
    if not _valid_pair_id(pair_id) or _pair_state(pair_id) is None:
        return _pair_html('<div class="msg err">This code has expired. Open the pairing again on the console.</div>', 404)
    from urllib.parse import parse_qs

    raw = (await request.body())[:512].decode("utf-8", "replace")
    key = "".join((parse_qs(raw).get("key") or [""])[0].split())
    if not (16 <= len(key) <= 64) or not key.isalnum():
        note = '<div class="msg err">That does not look like a SteamGridDB API key.</div>'
        return _pair_html(_PAIR_FORM.format(pair_id=pair_id, note=note), 422)
    works = _steamgriddb_key_works(key)
    if works is False:
        note = '<div class="msg err">SteamGridDB did not accept this key. Check it and try again.</div>'
        return _pair_html(_PAIR_FORM.format(pair_id=pair_id, note=note), 422)
    with _pair_lock:
        pairing = _pairings.get(pair_id)
        if pairing is None or pairing["expires"] <= time.monotonic():
            return _pair_html('<div class="msg err">This code has expired. Open the pairing again on the console.</div>', 404)
        pairing["key"] = key
    checked = "" if works else " SteamGridDB could not be reached to check it."
    return _pair_html(f'<div class="msg ok">Sent. Your console will save the key in a moment.{checked}</div>')
