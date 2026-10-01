#!/usr/bin/env python3
"""Export a running demo LUG Manager (seeded by seed.py, started with
LUG_OFFLINE=1) as a static site for GitHub Pages.

    python3 scripts/demo/export.py <server-url> <out-dir> <site-base> '<tokens-json>' <static-dir> <uploads-dir>

For each viewpoint (member / moderator / admin) it crawls every page reachable
from the sidebar, following links and htmx GETs, and saves:
  <out>/<role>/<path>/index.html          full pages (as a browser would load them)
  <out>/<role>/_frag/<path>/index.html    htmx partials (as htmx would load them)
A query string becomes a /__q/<encoded> sub-folder. Assets go to
<out>/static and <out>/uploads. demo/demo.js (copied to <out>/demo.js) maps
htmx/XHR requests onto these files at runtime and blocks anything that would
change data. Only GET requests are ever made to the server.
"""
import html
import json
import os
import re
import shutil
import sys
import urllib.error
import urllib.parse
import urllib.request
from collections import deque

SERVER, OUT, BASE, TOKENS = sys.argv[1].rstrip("/"), sys.argv[2], sys.argv[3].rstrip("/"), json.loads(sys.argv[4])
STATIC_DIR, UPLOADS_DIR = sys.argv[5], sys.argv[6]
HERE = os.path.dirname(os.path.abspath(__file__))

START = ["/dashboard", "/members", "/meetings", "/events", "/chapters", "/attendance", "/attendance/overview",
         "/challenges", "/inventory", "/treasury", "/account", "/help", "/reports/annual", "/perks",
         "/settings", "/settings/features", "/settings/backups", "/settings/roles", "/settings/calendar",
         "/settings/branding", "/settings/api-keys", "/settings/discord-matches", "/setup", "/audit",
         "/meetings/series", "/members/me", "/account/sessions", "/members/merge"]
# Endpoints whose query strings only page/sort/filter: one file serves all.
IGNORE_QUERY = {"/api/members/datatable", "/api/discord/forum-threads", "/api/chapter-options", "/api/member-options", "/api/discord/role-options",
                "/api/discord/channel-options", "/audit/data", "/attendance/overview/data"}
SKIP = re.compile(r"^/(static|uploads|branding|auth|api/v1|login|logout|checkin|kiosk|calendar/me|"
                  r"settings/backups/lug-|settings/backups/photos\.zip|account/export|shows)")
ATTR = re.compile(r'''(href|src|action|hx-get|data-url)=(["'])(/[^"'#]*)\2''')
LIMIT_PER_ROLE = 1500


def qenc(q):
    return q.replace("%", "~").replace("&", ",").replace("/", "~2F").replace("?", "_").replace("#", "_")


def split(path):
    u = urllib.parse.urlsplit(path)
    p = u.path or "/"
    qs = "" if p in IGNORE_QUERY else u.query
    return p, qs


def rel_path(path, frag):
    p, qs = split(path)
    seg = p.strip("/") or "dashboard"
    out = ("_frag/" if frag else "") + seg
    if qs:
        out += "/__q/" + qenc(qs)
    return out


def site_url(role, path):
    """URL of the saved full page for an app path."""
    p, _ = split(path)
    if re.match(r"^/(static|uploads|branding)/", p):
        return BASE + path
    return f"{BASE}/{role}/{rel_path(path, False)}/"


# Read-only endpoints the app calls with POST (DataTables sends paging in the body).
READ_POSTS = {"/api/members/datatable": b"draw=1&start=0&length=200&order[0][column]=0&order[0][dir]=asc"}


def fetch(path, token, htmx):
    p = urllib.parse.urlsplit(path).path
    req = urllib.request.Request(SERVER + path, data=READ_POSTS.get(p))
    if p in READ_POSTS:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    req.add_header("Cookie", f"session={token}")
    if htmx:
        req.add_header("HX-Request", "true")
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.headers.get("Content-Type", ""), r.read()
    except urllib.error.HTTPError as e:
        return e.code, "", b""
    except Exception:
        return 0, "", b""


def links(text):
    found = set()
    for m in ATTR.finditer(text):
        found.add(html.unescape(m.group(3)))
    # hx-get built in inline scripts, e.g. DataTables row buttons: '/members/' + row.id + '/view'
    return found


def rewrite(text, role, full_page, page_role=None):
    """role: whose pages links lead to; page_role: what demo.js treats this page as."""
    def sub(m):
        attr, quote, url = m.group(1), m.group(2), html.unescape(m.group(3))
        if attr in ("hx-get", "data-url"):
            return m.group(0)                       # mapped at runtime by demo.js
        if attr in ("src", "href") and re.match(r"^/(static|uploads|branding)/", url):
            return f'{attr}={quote}{BASE}{html.escape(url)}{quote}'
        if attr == "action":
            return m.group(0)                       # forms are blocked by demo.js
        return f'{attr}={quote}{html.escape(site_url(role, url))}{quote}'
    text = ATTR.sub(sub, text)
    # Server-side tables (members) would ask the server to search/sort/page;
    # in the static demo the exported JSON holds every row, so let DataTables
    # do it in the browser instead.
    text = text.replace("serverSide: true,", "serverSide: false,")
    if full_page:
        cfg = html.escape(json.dumps({"base": BASE, "role": page_role or role}), quote=True)
        inject = (f'<meta name="lug-demo" content="{cfg}">'
                  f'<script src="{BASE}/demo.js"></script>')
        text = re.sub(r"<head>", "<head>" + inject, text, count=1)
        text = re.sub(r"<script[^>]*/manifest|<link rel=\"manifest\"[^>]*>", "", text)
    return text


def save(rel, body):
    path = os.path.join(OUT, rel, "index.html")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(body)


def worth_crawling(path, variants):
    """Year pickers and sort/paging links would otherwise go on forever."""
    p, qs = split(path)
    params = urllib.parse.parse_qs(qs)
    import datetime
    this_year = datetime.date.today().year
    for y in params.get("year", []):
        if not y.isdigit() or not (this_year - 2 <= int(y) <= this_year + 1):
            return False
    if qs:
        variants[p] = variants.get(p, 0) + 1
        if variants[p] > 12:
            return False
    return True


def crawl(role, token):
    seen, queue, n = set(), deque(START), 0
    member_ids = set()
    variants = {}
    while queue and n < LIMIT_PER_ROLE:
        path = queue.popleft()
        key = split(path)
        if key in seen or SKIP.match(path):
            continue
        seen.add(key)
        if not worth_crawling(path, variants):
            continue
        n += 1
        for htmx in (False, True):
            status, ctype, body = fetch(path, token, htmx)
            if status != 200:
                continue
            text = body.decode("utf-8", "replace")
            is_html = "text/html" in ctype
            if path.split("?")[0] == "/api/members/datatable":
                # DataTables: drop "draw" so the static copy is accepted for any request
                try:
                    j = json.loads(text)
                    j.pop("draw", None)
                    member_ids.update(str(r.get("id")) for r in j.get("data", []))
                    text = json.dumps(j)
                except ValueError:
                    pass
            full = is_html and not htmx and "<html" in text[:400].lower()
            out = rewrite(text, role, full) if is_html else text
            save(f"{role}/{rel_path(path, htmx)}", out.encode("utf-8"))
            for link in links(text):
                if link.startswith("/") and split(link) not in seen:
                    queue.append(link)
        if path == "/members":
            queue.append("/api/members/datatable")
    # Row buttons are built in JavaScript, so add them explicitly.
    for mid in member_ids:
        for p in (f"/members/{mid}/view", f"/members/{mid}", f"/members/{mid}/dues",
                  f"/attendance/member/{mid}/detail"):
            for htmx in (True,):
                status, ctype, body = fetch(p, token, htmx)
                if status == 200:
                    save(f"{role}/{rel_path(p, True)}", rewrite(body.decode("utf-8", "replace"), role, False).encode())
    print(f"[demo] {role}: {n} paths", file=sys.stderr)


def main():
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    shutil.copytree(STATIC_DIR, os.path.join(OUT, "static"))
    if os.path.isdir(UPLOADS_DIR):
        shutil.copytree(UPLOADS_DIR, os.path.join(OUT, "uploads"))
    for role, token in TOKENS.items():
        crawl(role, token)
    # Public pages (no login): the shows page
    status, _, body = fetch("/shows", "", False)
    if status == 200:
        # A visitor's page: demo.js on, links into the member view.
        save("shows", rewrite(body.decode("utf-8", "replace"), "member", True, page_role="public").encode())
    shutil.copy(os.path.join(HERE, "demo.js"), os.path.join(OUT, "demo.js"))
    with open(os.path.join(HERE, "index.html"), encoding="utf-8") as f:
        landing = f.read().replace("{{BASE}}", BASE)
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write(landing)
    with open(os.path.join(OUT, "404.html"), "w", encoding="utf-8") as f:
        f.write(f'<!doctype html><meta charset="utf-8"><title>Not in the demo</title>'
                f'<p style="font-family:sans-serif">That page isn\'t part of the demo. '
                f'<a href="{BASE}/">Back to the start</a></p>')
    open(os.path.join(OUT, ".nojekyll"), "w").close()
    with open(os.path.join(OUT, "demo-blocked.json"), "w") as f:
        f.write("{}")


main()
