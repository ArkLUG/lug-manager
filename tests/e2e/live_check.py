"""Browser check of live updates against a local LUG Manager (scratch DB).

    python tests/e2e/live_check.py <admin-session-token> <screenshot-dir>

Opens pages in a browser, then changes data from "somewhere else" (plain HTTP
requests with the same session but no X-Live-Tab, like another device) and
checks the open page updates without a reload:
  - /meetings re-fetches itself when a meeting is added;
  - /members reloads its table rows (keeping the table) when a member changes;
  - the kiosk screen shows a check-in within seconds;
  - while a form is being edited, the page is left alone and "Updated -
    refresh" appears instead; clicking it reloads the page.
Fails on JS errors and CSP violations (the websocket must be allowed).
"""
import json, os, sys, time, urllib.parse, urllib.request
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options

BASE = os.environ.get("LUG_BASE", "http://127.0.0.1:18089")
TOKEN = sys.argv[1]
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "/tmp"
opts = Options()
opts.add_argument("-headless")
d = webdriver.Firefox(options=opts)
d.set_window_size(1300, 950)
problems = []
HOOK = """window.__errs = window.__errs || [];
if (!window.__hooked) { window.__hooked = true; window.__marker = 'same-page';
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('securitypolicyviolation', e => window.__errs.push('csp: ' + e.violatedDirective + ' ' + e.blockedURI)); }"""


def check(cond, msg):
    if not cond:
        problems.append(msg)


def errs(where):
    for e in d.execute_script("return (window.__errs||[]).splice(0)"):
        problems.append(f"{where}: {e}")


def post(path, fields):
    req = urllib.request.Request(BASE + path, data=urllib.parse.urlencode(fields).encode(),
                                 headers={"Cookie": "session=" + TOKEN, "HX-Request": "true"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read().decode()


def go(path):
    d.get(BASE + path)
    d.execute_script(HOOK)
    time.sleep(1.5)   # let the websocket connect


def wait_for(fn, secs=6):
    end = time.time() + secs
    while time.time() < end:
        if fn():
            return True
        time.sleep(0.2)
    return False


def same_page():
    return d.execute_script("return window.__marker") == "same-page"


d.get(BASE + "/login")
d.add_cookie({"name": "session", "value": TOKEN, "path": "/"})

# 1. A list page re-fetches itself
go("/meetings?when=all")
title = "Live check %d" % int(time.time())
post("/meetings", {"title": title, "description": "x", "location": "Room 1", "scope": "lug_wide",
                   "start_time": "2026-12-01T19:00", "end_time": "2026-12-01T21:00"})
check(wait_for(lambda: title in d.page_source), "/meetings didn't show a meeting added elsewhere")
check(same_page(), "/meetings reloaded the whole document instead of refreshing in place")
d.save_screenshot(f"{SHOTS}/live_meetings.png")
errs("meetings")

# 2. The members table reloads its rows
go("/members")
rows = json.loads(post("/api/members/datatable", {"draw": 1, "start": 0, "length": 5, "search": "",
                                                  "order_col_name": "display_name", "order_dir": "asc"}))["data"]
m = rows[0]
new_first = "Livewire"
post("/members/%s" % m["id"], {"first_name": new_first, "last_name": m.get("last_name") or "Member"})
check(wait_for(lambda: new_first in d.find_element(By.ID, "members-table").text), "/members table didn't pick up a renamed member")
check(same_page(), "/members reloaded the whole document")
errs("members")

# 3. Someone mid-edit: the page waits and offers a refresh
go("/settings/sign-in")
radio = d.find_element(By.CSS_SELECTOR, "input[name=require_2fa][value=everyone]")
was = radio.is_selected()
radio.click()
title2 = "Live editing %d" % int(time.time())
post("/meetings", {"title": title2, "description": "x", "location": "Room 1", "scope": "lug_wide",
                   "start_time": "2026-12-02T19:00", "end_time": "2026-12-02T21:00"})
check(wait_for(lambda: d.find_elements(By.ID, "live-pill")), "no 'Updated - refresh' button while a form was being edited")
check(d.find_element(By.CSS_SELECTOR, "input[name=require_2fa][value=everyone]").is_selected() != was,
      "the form being edited was replaced")
d.save_screenshot(f"{SHOTS}/live_pill.png")
d.find_element(By.ID, "live-pill").click()
check(wait_for(lambda: not d.find_elements(By.ID, "live-pill")), "the refresh button didn't go away")
check(wait_for(lambda: d.find_element(By.CSS_SELECTOR, "input[name=require_2fa][value=everyone]").is_selected() == was),
      "refresh didn't reload the page")
errs("editing")

# 4. The kiosk screen updates the moment someone is checked in (not on a timer)
title3 = "Kiosk live %d" % int(time.time())
post("/meetings", {"title": title3, "description": "x", "location": "Room 1", "scope": "lug_wide",
                   "start_time": "2026-12-03T19:00", "end_time": "2026-12-03T21:00"})
go("/meetings?when=all")
import re
mid = max(int(x) for x in re.findall(r'/meetings/(\d+)', d.page_source))   # the newest: the one just added
go("/meetings/%d/kiosk" % mid)
time.sleep(1.0)
before = d.find_element(By.ID, "kiosk-recent").text
post("/attendance/admin/checkin", {"entity_type": "meeting", "entity_id": mid, "member_id": rows[1]["id"]})
check(wait_for(lambda: d.find_element(By.ID, "kiosk-recent").text != before, 5), "kiosk didn't update within 5 s of a check-in")
d.save_screenshot(f"{SHOTS}/live_kiosk.png")
errs("kiosk")

d.quit()
print("PROBLEMS", len(problems))
for p in problems:
    print(" -", p)
sys.exit(1 if problems else 0)
