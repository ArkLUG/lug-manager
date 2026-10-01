"""Check the exported static demo (scripts/demo/build.sh) in a browser.

Serve the export so it appears under /lug-manager/, e.g.
    mkdir -p /tmp/site && ln -s <out-dir> /tmp/site/lug-manager
    python3 -m http.server 18097 --directory /tmp/site
then run:  python tests/e2e/demo_check.py http://127.0.0.1:18097/lug-manager <screenshot-dir>

Clicks every sidebar link for each viewpoint, waits for the page, and fails
on JS errors, "not included in the demo" notices, broken reloads, or a write
that isn't blocked.
"""
import sys, time
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options

BASE = sys.argv[1].rstrip("/")
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "/tmp"
opts = Options(); opts.add_argument("-headless")
d = webdriver.Firefox(options=opts); d.set_window_size(1400, 950)
problems = []
HOOK = """window.__errs = window.__errs || [];
if (!window.__hooked) { window.__hooked = true;
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('htmx:responseError', e => window.__errs.push('missing: ' + (e.detail.pathInfo||{}).requestPath)); }"""

def errs(where):
    for e in d.execute_script("return window.__errs || []"):
        problems.append(f"{where}: {e}")
    d.execute_script("window.__errs = []")

def toast():
    return d.execute_script("var t=document.getElementById('lug-demo-toast'); return t && t.style.display!=='none' ? t.textContent : ''")

d.get(BASE + "/")
time.sleep(0.5)
if "Try the demo" not in d.page_source: problems.append("landing page")
d.save_screenshot(f"{SHOTS}/demo_landing.png")

for role in ("member", "moderator", "admin"):
    d.get(f"{BASE}/{role}/dashboard/"); d.execute_script(HOOK); time.sleep(1.0)
    if "lug-demo-banner" not in d.page_source: problems.append(f"{role}: no demo banner")
    errs(f"{role} dashboard")
    links = [a.get_attribute("hx-get") for a in d.find_elements(By.CSS_SELECTOR, "aside a[hx-get]")]
    for href in links:
        d.get(f"{BASE}/{role}/dashboard/"); d.execute_script(HOOK); time.sleep(0.4)
        el = d.find_elements(By.CSS_SELECTOR, f'aside a[hx-get="{href}"]')
        if not el: continue
        d.execute_script("arguments[0].click()", el[0]); time.sleep(1.0)
        main = d.find_element(By.ID, "main-content").get_attribute("innerHTML").strip()
        if len(main) < 50: problems.append(f"{role} {href}: empty page")
        if "isn't included" in toast(): problems.append(f"{role} {href}: not included")
        if not d.execute_script("return location.pathname").startswith(BASE.split('//',1)[1].split('/',1)[1] and '/' + BASE.split('//',1)[1].split('/',1)[1] or '/'):
            problems.append(f"{role} {href}: pushed URL outside the demo")
        # reload the pushed URL: must still be a full page
        d.refresh(); d.execute_script(HOOK); time.sleep(0.6)
        if "lug-demo-banner" not in d.page_source: problems.append(f"{role} {href}: reload broke ({d.current_url})")
        errs(f"{role} {href}")
    d.get(f"{BASE}/{role}/dashboard/"); time.sleep(0.5)
    d.save_screenshot(f"{SHOTS}/demo_{role}.png")

# Writes are blocked: RSVP button on an event page (member)
d.get(f"{BASE}/member/events/"); d.execute_script(HOOK); time.sleep(0.8)
ev = d.find_elements(By.CSS_SELECTOR, '#main-content [hx-get^="/events/"]')
if ev:
    d.execute_script("arguments[0].click()", ev[0]); time.sleep(1.2)
    btn = d.find_elements(By.CSS_SELECTOR, 'button[hx-post*="/rsvp"]')
    if btn:
        d.execute_script("arguments[0].click()", btn[0]); time.sleep(0.3)
        try:
            d.switch_to.alert.accept(); time.sleep(0.4)     # "Cancel your RSVP?"
        except Exception:
            pass
        if "aren't saved" not in toast(): problems.append("RSVP click not blocked with a notice")
    else:
        problems.append("no RSVP button on the event page")
    errs("event page")
    d.save_screenshot(f"{SHOTS}/demo_event.png")
else:
    problems.append("no event links on the events page")

# Member list (DataTables from a static JSON) and a member modal
d.get(f"{BASE}/admin/members/"); d.execute_script(HOOK); time.sleep(1.5)
rows = d.find_elements(By.CSS_SELECTOR, "#members-table tbody tr")
if len(rows) < 5: problems.append(f"members table has {len(rows)} rows")
# Search, sort and paging work in the static demo
box = d.find_elements(By.CSS_SELECTOR, ".dt-search input, .dataTables_filter input")
if box:
    box[0].send_keys("Torres"); time.sleep(0.8)
    shown = [r.text for r in d.find_elements(By.CSS_SELECTOR, "#members-table tbody tr")]
    if len(shown) != 1 or "Maya" not in shown[0]: problems.append(f"members search: {shown}")
    box[0].clear(); box[0].send_keys(" "); box[0].clear(); time.sleep(0.5)
    d.execute_script("arguments[0].value=''; arguments[0].dispatchEvent(new Event('input'))", box[0]); time.sleep(0.6)
else:
    problems.append("no members search box")
head = d.find_elements(By.CSS_SELECTOR, "#members-table thead th")
if head:
    d.execute_script("arguments[0].click()", head[0]); time.sleep(0.5)
    first_asc = d.find_element(By.CSS_SELECTOR, "#members-table tbody tr td").text
    d.execute_script("arguments[0].click()", head[0]); time.sleep(0.5)
    first_desc = d.find_element(By.CSS_SELECTOR, "#members-table tbody tr td").text
    if first_asc == first_desc: problems.append("members sort didn't change order")
view = d.find_elements(By.CSS_SELECTOR, '#members-table button[hx-get$="/view"]')
if view:
    d.execute_script("arguments[0].click()", view[0]); time.sleep(0.8)
    if len(d.find_element(By.ID, "modal").get_attribute("innerHTML").strip()) < 50: problems.append("member modal empty")
errs("members")
d.save_screenshot(f"{SHOTS}/demo_members.png")

d.get(f"{BASE}/shows/"); d.execute_script(HOOK); time.sleep(0.8)
if "Brickton" not in d.page_source: problems.append("public shows page")
if "lug-demo-banner" not in d.page_source: problems.append("shows page: no demo banner")
plan = d.find_elements(By.XPATH, "//button[contains(., 'I plan to come')]")
if plan:
    url_before = d.current_url
    d.execute_script("arguments[0].click()", plan[0]); time.sleep(0.6)
    if d.current_url != url_before: problems.append("I plan to come navigated away")
    if "You're coming" not in d.page_source: problems.append("I plan to come didn't toggle")
    if "planning to come" not in d.page_source: problems.append("I plan to come: no count")
else:
    problems.append("no 'I plan to come' button")
errs("shows")
d.save_screenshot(f"{SHOTS}/demo_shows.png")

d.quit()
for p in problems: print(p)
print("PROBLEMS:", len(problems))
