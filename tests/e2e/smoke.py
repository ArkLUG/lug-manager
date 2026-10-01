"""Browser smoke test against a local LUG Manager (http://127.0.0.1:18089).

Logs in by planting the session cookie, then visits pages and exercises the
interactive bits, collecting JS errors and CSP violations along the way.
"""
import sys, time, json
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC

BASE = "http://127.0.0.1:18089"
TOKEN = sys.argv[1] if len(sys.argv) > 1 else "devtoken123"
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "/tmp"

opts = Options()
opts.add_argument("-headless")
opts.set_preference("devtools.console.stdout.content", True)
d = webdriver.Firefox(options=opts)
d.set_window_size(1400, 950)
problems = []

HOOK = """
window.__errs = window.__errs || [];
if (!window.__hooked) {
  window.__hooked = true;
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('securitypolicyviolation', e =>
     window.__errs.push('csp: ' + e.violatedDirective + ' ' + e.blockedURI + ' ' + (e.sample||'')));
}
"""

def errs(where):
    try:
        es = d.execute_script("return (window.__errs||[]).splice(0).concat((window.__lmProblems||[]).splice(0))")
    except Exception as ex:
        es = [str(ex)]
    for e in es: problems.append(f"{where}: {e}")

def visit(path, wait_css=None):
    d.get(BASE + path)
    d.execute_script(HOOK)
    if wait_css:
        WebDriverWait(d, 8).until(EC.presence_of_element_located((By.CSS_SELECTOR, wait_css)))
    time.sleep(0.6)
    errs(path)

def shot(name):
    d.save_screenshot(f"{SHOTS}/{name}.png")

def check(cond, msg):
    if not cond: problems.append("FAIL: " + msg)

d.get(BASE + "/login")
d.add_cookie({"name": "session", "value": TOKEN, "path": "/"})

# Dashboard + theme switch
visit("/dashboard", "#main-content")
check("Dashboard" in d.title, "dashboard title")
for t in ("dark", "light", "system"):
    d.find_element(By.CSS_SELECTOR, f'.theme-btn[data-v="{t}"]').click()
    check(d.execute_script("return document.documentElement.getAttribute('data-theme-pref')") == t, f"theme {t}")
d.find_element(By.CSS_SELECTOR, '.theme-btn[data-v="dark"]').click()
shot("dash")

# Members table (DataTables via XHR)
visit("/members", "#members-table")
WebDriverWait(d, 8).until(lambda x: "Loading" not in x.find_element(By.ID, "members-table").text)
check("Aaron" in d.find_element(By.ID, "members-table").text, "members rows loaded")
cbs = d.find_elements(By.CSS_SELECTOR, ".bulk-sel")
if cbs:
    cbs[0].click(); time.sleep(0.3)
    check("hidden" not in d.find_element(By.ID, "bulk-bar").get_attribute("class"), "bulk bar shows")
errs("/members interactions")
shot("members")

# Modal: open a meeting via HTMX and close it
visit("/meetings", "#main-content")
btns = d.find_elements(By.CSS_SELECTOR, 'button[hx-get^="/meetings/"][hx-target="#modal"]')
if btns:
    btns[0].click(); time.sleep(1.0)
    check("open" in d.find_element(By.ID, "modal-backdrop").get_attribute("class"), "modal opens")
    closes = d.find_elements(By.CSS_SELECTOR, '#modal button[aria-label="Close"]')
    if closes:
        closes[0].click(); time.sleep(0.3)
        check("open" not in d.find_element(By.ID, "modal-backdrop").get_attribute("class"), "modal closes")
errs("/meetings modal")
shot("meetings")

# New meeting form: EasyMDE + TomSelect initialise
visit("/meetings", "#main-content")
nb = d.find_elements(By.CSS_SELECTOR, 'button[hx-get="/meetings/new"]')
if nb:
    nb[0].click(); time.sleep(1.2)
    check(len(d.find_elements(By.CSS_SELECTOR, ".EasyMDEContainer")) > 0, "EasyMDE initialised")
errs("/meetings/new form")
shot("meeting_form")

# Settings (TomSelect), backups, attendance overview, help, audit
for p in ("/settings", "/settings/backups", "/attendance/overview", "/help", "/audit", "/chapters", "/events", "/perks"):
    visit(p, "#main-content")
    shot(p.strip("/").replace("/", "_"))

# Sidebar accordion
acc = d.find_elements(By.CSS_SELECTOR, ".nav-accordion-toggle")
if acc:
    acc[0].click(); time.sleep(0.2)
errs("accordion")

# Public check-in page and kiosk
visit("/meetings/1/kiosk", "#kiosk-qr")
time.sleep(1)
check(len(d.find_elements(By.CSS_SELECTOR, "#kiosk-qr canvas, #kiosk-qr img")) > 0, "kiosk QR rendered")
errs("kiosk")
shot("kiosk")

d.quit()
print(json.dumps(problems, indent=1))
print("PROBLEMS:", len(problems))
