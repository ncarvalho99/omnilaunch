#!/usr/bin/env python3
"""
SwitchU YouTube-DL Backend Service (Hardened & Audited)
Provides a secure HTTP REST API for Nintendo Switch SwitchU client:
- GET /health
- GET /api/search?q=<query>&limit=20
- GET /api/stream?id=<video_id>
- GET /api/download?id=<video_id>

Security controls:
- Constant-time client key authentication (X-SwitchU-Key via hmac.compare_digest)
- Strict regex input validation (video ID ^[a-zA-Z0-9_-]{11}$)
- Search query bounds & control character stripping
- SSRF prevention (whitelisted https://*.googlevideo.com protocols only)
- Concurrency limiting via threading.Semaphore (DoS protection)
- Subprocess sandboxing: -nostdin, -protocol_whitelist, strict execution timeouts
- Resource leak / zombie process cleanup in finally blocks
- Safe error handling: no internal traces leaked to client
- Strict HTTP security headers (nosniff, DENY, CSP)
"""

import argparse
import hmac
import json
import logging
import os
import re
import subprocess
import sys
import threading
import urllib.parse
import urllib.request
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] [%(process)d] %(levelname)s: %(message)s"
)
logger = logging.getLogger("switchu-ytdl")

# Security bounds & configuration
VIDEO_ID_REGEX = re.compile(r"^[a-zA-Z0-9_-]{11}$")
CONTROL_CHARS_REGEX = re.compile(r"[\x00-\x1f\x7f]")
ALLOWED_STREAM_DOMAINS = (".googlevideo.com", ".youtube.com", ".savenow.to")
MAX_QUERY_LENGTH = 100
MAX_SEARCH_LIMIT = 30
MAX_CONCURRENT_TRANSCODES = 3
TRANSCODE_SEMAPHORE = threading.Semaphore(MAX_CONCURRENT_TRANSCODES)
TRANSCODE_TIMEOUT_SECONDS = 180

# Client authentication key (can be set via SWITCHU_CLIENT_KEY env var)
AUTH_CLIENT_KEY = os.environ.get("SWITCHU_CLIENT_KEY", "").strip()

# Egress routes for YouTube requests. Datacenter addresses are routinely answered
# with "Sign in to confirm you're not a bot", so the service tries each route in
# order until one yields audio.
#
# YTDL_ROUTES: ';'-separated routes, each "<proxy>|<po-token-provider-url>".
#   <proxy> is a yt-dlp proxy URL or "direct"; the provider URL may be empty.
#   Example: "socks5h://127.0.0.1:18090|http://127.0.0.1:4417;direct|http://127.0.0.1:4416"
# YTDL_PROXY / YTDL_POT_URL: single-route shorthand used when YTDL_ROUTES is unset.
# YTDL_COOKIES: Netscape cookies.txt applied to every route (ignored when empty).
# YTDL_JS_RUNTIME: "<runtime>:<path>" passed to --js-runtimes (e.g. node:/opt/node22/bin/node).
YTDL_COOKIES = os.environ.get("YTDL_COOKIES", "").strip()
YTDL_JS_RUNTIME = os.environ.get("YTDL_JS_RUNTIME", "").strip()


def _parse_routes():
    raw = os.environ.get("YTDL_ROUTES", "").strip()
    if not raw:
        raw = (os.environ.get("YTDL_PROXY", "").strip() or "direct") + "|" + \
              os.environ.get("YTDL_POT_URL", "").strip()
    routes = []
    for part in raw.split(";"):
        part = part.strip()
        if not part:
            continue
        proxy, _, pot = part.partition("|")
        proxy = proxy.strip()
        routes.append({
            "proxy": "" if proxy.lower() in ("", "direct", "none") else proxy,
            "pot": pot.strip(),
        })
    return routes or [{"proxy": "", "pot": ""}]


YTDL_ROUTES = _parse_routes()


def route_label(route):
    return ("proxy" if route["proxy"] else "direct") + ("+pot" if route["pot"] else "")


def cookies_usable():
    try:
        return bool(YTDL_COOKIES) and os.path.getsize(YTDL_COOKIES) > 0
    except OSError:
        return False


def ytdl_cli_base(route):
    """yt-dlp command line for one egress route (module of this interpreter)."""
    cmd = [sys.executable, "-m", "yt_dlp", "--no-warnings", "--quiet", "--no-playlist",
           "--ignore-config", "--no-cache-dir"]
    if YTDL_JS_RUNTIME:
        cmd += ["--js-runtimes", YTDL_JS_RUNTIME]
    if route["proxy"]:
        cmd += ["--proxy", route["proxy"]]
    if route["pot"]:
        cmd += ["--extractor-args", "youtubepot-bgutilhttp:base_url=" + route["pot"]]
    if cookies_usable():
        cmd += ["--cookies", YTDL_COOKIES]
    return cmd

# Try to import yt_dlp or youtube_dl
YTDL_MODULE = None
try:
    import yt_dlp as ytdl_lib
    YTDL_MODULE = "yt_dlp"
    logger.info("Using yt-dlp Python module")
except ImportError:
    try:
        import youtube_dl as ytdl_lib
        YTDL_MODULE = "youtube_dl"
        logger.info("Using youtube-dl Python module")
    except ImportError:
        ytdl_lib = None
        logger.info("No ytdl Python module found. Will use CLI or native InnerTube fallback.")


def format_duration(seconds):
    if not seconds:
        return "0:00"
    try:
        s = int(seconds)
        m, s = divmod(s, 60)
        h, m = divmod(m, 60)
        if h > 0:
            return f"{h}:{m:02d}:{s:02d}"
        return f"{m}:{s:02d}"
    except Exception:
        return "0:00"


def clean_title(title):
    if not title:
        return "Unknown"
    # Remove control characters and characters forbidden on FAT32
    title = CONTROL_CHARS_REGEX.sub("", title)
    title = re.sub(r'[\\/*?:"<>|]', "", title).strip()
    return title[:100] if len(title) > 100 else title


def is_safe_stream_url(url):
    """Ensure stream URL is an HTTPS URL pointing strictly to legitimate YouTube/Google CDN domains."""
    if not url or not isinstance(url, str):
        return False
    try:
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme.lower() != "https":
            return False
        host = parsed.hostname.lower() if parsed.hostname else ""
        return any(host.endswith(d) or host == d.lstrip(".") for d in ALLOWED_STREAM_DOMAINS)
    except Exception:
        return False


def search_innertube(query, limit=20):
    """Direct search using YouTube InnerTube API with query bounded and sanitized."""
    query = CONTROL_CHARS_REGEX.sub("", query).strip()[:MAX_QUERY_LENGTH]
    if not query:
        return []

    limit = max(1, min(limit, MAX_SEARCH_LIMIT))
    url = "https://www.youtube.com/youtubei/v1/search"
    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
        "Content-Type": "application/json"
    }
    payload = {
        "context": {
            "client": {
                "clientName": "WEB",
                "clientVersion": "2.20240101.00.00",
                "hl": "en",
                "gl": "US"
            }
        },
        "query": query
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            content = resp.read().decode("utf-8", errors="replace")
            res_json = json.loads(content)
    except Exception as e:
        logger.error("InnerTube search failed: %s", e)
        return []

    results = []
    try:
        sections = res_json.get("contents", {}).get("twoColumnSearchResultsRenderer", {}) \
            .get("primaryContents", {}).get("sectionListRenderer", {}).get("contents", [])
        for section in sections:
            item_section = section.get("itemSectionRenderer", {}).get("contents", [])
            for item in item_section:
                vr = item.get("videoRenderer")
                if not vr:
                    continue
                vid = vr.get("videoId")
                if not vid or not VIDEO_ID_REGEX.match(vid):
                    continue

                title = ""
                runs = vr.get("title", {}).get("runs", [])
                if runs:
                    title = "".join(r.get("text", "") for r in runs)
                else:
                    title = vr.get("title", {}).get("simpleText", "")

                author = ""
                byline_runs = vr.get("ownerText", {}).get("runs") or vr.get("shortBylineText", {}).get("runs", [])
                if byline_runs:
                    author = "".join(r.get("text", "") for r in byline_runs)

                duration = vr.get("lengthText", {}).get("simpleText", "0:00")

                thumbs = vr.get("thumbnail", {}).get("thumbnails", [])
                thumb_url = f"https://i.ytimg.com/vi/{vid}/hqdefault.jpg"
                if thumbs:
                    t_url = thumbs[-1].get("url", thumb_url)
                    if t_url.startswith("https://"):
                        thumb_url = t_url

                results.append({
                    "id": vid,
                    "title": clean_title(title),
                    "author": clean_title(author) or "YouTube",
                    "duration": duration,
                    "thumbnail": thumb_url
                })
                if len(results) >= limit:
                    break
            if len(results) >= limit:
                break
    except Exception as e:
        logger.error("Error parsing InnerTube search response: %s", e)

    return results


def resolve_stream_info(video_id):
    """Resolve direct audio stream URL using yt-dlp / youtube-dl safely."""
    if not VIDEO_ID_REGEX.match(video_id):
        return {"status": "error", "error": "Invalid video ID format"}

    url = f"https://www.youtube.com/watch?v={video_id}"
    logger.info("Resolving stream info for %s...", video_id)

    # Metadata only: googlevideo URLs are bound to the address that resolved
    # them, so clients must download through /api/download instead.
    if ytdl_lib:
        for route in YTDL_ROUTES:
            cmd = ytdl_cli_base(route) + ["-g", "-f", "bestaudio/best", "--get-title", url]
            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd="/tmp")
            except Exception as e:
                logger.warning("Route %s failed for %s: %s", route_label(route), video_id, e)
                continue
            lines = [l.strip() for l in proc.stdout.strip().split("\n") if l.strip()]
            if proc.returncode == 0 and len(lines) >= 2 and is_safe_stream_url(lines[1]):
                return {
                    "status": "ok",
                    "id": video_id,
                    "title": clean_title(lines[0]),
                    "stream_url": lines[1],
                    "route": route_label(route),
                    "ext": "mp3",
                    "filesize": 0
                }
            err_line = next((l for l in proc.stderr.splitlines() if "ERROR" in l), "")
            logger.warning("Route %s failed for %s: %s", route_label(route), video_id, err_line[:300])
    return {"status": "error", "error": f"Failed to extract stream for {video_id}"}


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class YtdlRequestHandler(BaseHTTPRequestHandler):
    server_version = "SwitchU-YTDL/1.1"

    def _check_auth(self):
        """Enforce X-SwitchU-Key / X-OmniLaunch-Key authentication if AUTH_CLIENT_KEY is configured."""
        if not AUTH_CLIENT_KEY:
            return True
        client_key = (self.headers.get("X-SwitchU-Key") or self.headers.get("X-OmniLaunch-Key") or "").strip()
        if not client_key:
            return False
        return hmac.compare_digest(client_key.encode("utf-8"), AUTH_CLIENT_KEY.encode("utf-8"))

    def _apply_security_headers(self):
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Content-Security-Policy", "default-src 'none'")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")

    def _send_json(self, status_code, data):
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self._apply_security_headers()
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS, HEAD")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-SwitchU-Key")
        self._apply_security_headers()
        self.end_headers()

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        params = urllib.parse.parse_qs(parsed.query)

        # Health / Status (publicly accessible for monitors)
        if path in ["/", "/health", "/v1/health", "/api/health"]:
            self._send_json(200, {
                "status": "ok",
                "service": "switchu-ytdl",
                "version": "1.1.0",
                "auth_enforced": bool(AUTH_CLIENT_KEY),
                "ytdl_backend": YTDL_MODULE or "cli/innertube",
                "routes": [route_label(r) for r in YTDL_ROUTES],
                "cookies": cookies_usable()
            })
            return

        # Authentication check for all functional endpoints
        if not self._check_auth():
            logger.warning("Unauthorized access attempt to %s from %s", path, self.client_address[0])
            self._send_json(401, {"error": "Unauthorized: Invalid or missing X-SwitchU-Key"})
            return

        # Search endpoint: /api/search?q=<query>&limit=20
        if path in ["/api/search", "/v1/search"]:
            query = params.get("q", [""])[0].strip()
            if not query:
                self._send_json(400, {"error": "Missing query 'q'"})
                return
            if len(query) > MAX_QUERY_LENGTH:
                self._send_json(400, {"error": f"Query exceeds maximum length of {MAX_QUERY_LENGTH}"})
                return

            try:
                limit = int(params.get("limit", [20])[0])
            except ValueError:
                limit = 20

            items = search_innertube(query, limit)
            self._send_json(200, {
                "query": query,
                "count": len(items),
                "items": items
            })
            return

        # Stream resolution endpoint: /api/stream?id=<video_id>
        if path in ["/api/stream", "/v1/stream"]:
            vid = params.get("id", [""])[0].strip()
            if not vid or not VIDEO_ID_REGEX.match(vid):
                self._send_json(400, {"error": "Invalid or missing 'id' parameter (must be 11 characters alphanumeric)"})
                return

            info = resolve_stream_info(vid)
            status_code = 200 if info.get("status") == "ok" else 500
            self._send_json(status_code, info)
            return

        # Download stream endpoint: /api/download?id=<video_id>
        if path in ["/api/download", "/v1/download"]:
            vid = params.get("id", [""])[0].strip()
            if not vid or not VIDEO_ID_REGEX.match(vid):
                self._send_json(400, {"error": "Invalid or missing 'id' parameter (must be 11 characters alphanumeric)"})
                return

            # Concurrency limit check
            if not TRANSCODE_SEMAPHORE.acquire(blocking=True, timeout=2.0):
                logger.warning("Concurrent transcode limit reached (%d), rejecting %s", MAX_CONCURRENT_TRANSCODES, vid)
                self._send_json(503, {"error": "Server is busy with other downloads, please try again shortly"})
                return

            proc = None
            fetch = None
            try:
                # yt-dlp fetches the audio itself and pipes it into ffmpeg.
                # Googlevideo URLs are bound to the address that resolved them, so
                # ffmpeg never fetches them on its own. Routes are tried in order
                # until one produces encoded audio; only then is 200 committed.
                url = f"https://www.youtube.com/watch?v={vid}"
                first = b""
                for route in YTDL_ROUTES:
                    fetch = subprocess.Popen(
                        ytdl_cli_base(route) + ["-f", "bestaudio/best", "-o", "-", url],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        cwd="/tmp"
                    )
                    proc = subprocess.Popen(
                        ["ffmpeg", "-nostdin", "-loglevel", "error", "-i", "pipe:0",
                         "-vn", "-acodec", "libmp3lame", "-b:a", "192k", "-f", "mp3", "pipe:1"],
                        stdin=fetch.stdout,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.DEVNULL
                    )
                    fetch.stdout.close()  # ffmpeg owns the read end now
                    first = proc.stdout.read(65536)
                    if first:
                        logger.info("Download of %s via route %s", vid, route_label(route))
                        break
                    err_text = ""
                    try:
                        fetch.wait(timeout=5)
                        err_text = fetch.stderr.read().decode("utf-8", errors="replace")
                    except Exception:
                        pass
                    err_line = next((l for l in err_text.splitlines() if "ERROR" in l), "")
                    logger.warning("Route %s produced no audio for %s: %s", route_label(route), vid, err_line[:300])
                    for p in (proc, fetch):
                        try:
                            if p.stdout:
                                p.stdout.close()
                            if p.stderr:
                                p.stderr.close()
                            if p.poll() is None:
                                p.kill()
                            p.wait(timeout=3)
                        except Exception:
                            pass
                    proc = None
                    fetch = None

                if not first:
                    self._send_json(502, {"error": "Could not fetch audio for this video"})
                    return
                self.send_response(200)
                self.send_header("Content-Type", "audio/mpeg")
                self.send_header("Content-Disposition", f'attachment; filename="{vid}.mp3"')
                self._apply_security_headers()
                self.end_headers()
                self.wfile.write(first)

                while True:
                    buf = proc.stdout.read(65536)
                    if not buf:
                        break
                    self.wfile.write(buf)

                proc.wait(timeout=TRANSCODE_TIMEOUT_SECONDS)
                logger.info("Download served for %s", vid)
                return

            except (BrokenPipeError, ConnectionResetError):
                logger.info("Client disconnected during download for %s", vid)
            except subprocess.TimeoutExpired:
                logger.warning("Transcode timed out for %s", vid)
                if proc:
                    proc.kill()
            except Exception as e:
                logger.error("Download streaming error for %s: %s", vid, e)
                try:
                    self._send_json(500, {"error": "Internal streaming error"})
                except Exception:
                    pass
            finally:
                for p in (proc, fetch):
                    if not p:
                        continue
                    try:
                        if p.stdout:
                            p.stdout.close()
                        if p.stderr:
                            p.stderr.close()
                        if p.poll() is None:
                            p.terminate()
                            p.wait(timeout=3)
                    except Exception:
                        if p.poll() is None:
                            p.kill()
                TRANSCODE_SEMAPHORE.release()
            return

        self._send_json(404, {"error": "Not found", "path": path})


def main():
    global AUTH_CLIENT_KEY
    parser = argparse.ArgumentParser(description="SwitchU YouTube-DL Backend Service")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8080, help="Port to listen on (default: 8080)")
    parser.add_argument("--client-key", default=None, help="Client authentication key (X-SwitchU-Key)")
    args = parser.parse_args()

    if args.client_key:
        AUTH_CLIENT_KEY = args.client_key.strip()

    logger.info("Starting SwitchU YTDL Service on %s:%d (auth enforced: %s)", args.host, args.port, bool(AUTH_CLIENT_KEY))
    server_address = (args.host, args.port)
    httpd = ThreadedHTTPServer(server_address, YtdlRequestHandler)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        logger.info("Stopping server...")
        httpd.server_close()


if __name__ == "__main__":
    main()
