"""Phone-width check (390x844) of the main pages against a local LUG Manager.

Flags pages that scroll sideways at phone width (content wider than the
screen - tables inside overflow-x-auto wrappers are fine), plus JS errors
and CSP violations. Saves a full-page screenshot of each page.

    python tests/e2e/mobile.py <session-token> <screenshot-dir> [extra paths...]
"""
import sys, time
from selenium import webdriver
from selenium.webdriver.firefox.options import Options

BASE = "http://127.0.0.1:18089"
TOKEN = sys.argv[1] if len(sys.argv) > 1 else "devtoken123"
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "/tmp"
PAGES = ["/dashboard", "/account", "/inventory", "/treasury", "/challenges", "/members", "/events",
         "/meetings", "/settings/backups", "/settings", "/help", "/shows", "/shows?embed=1"] + sys.argv[3:]

opts = Options()
opts.add_argument("-headless")
d = webdriver.Firefox(options=opts)
d.set_window_size(390, 844)
problems = []

HOOK = """
window.__errs = window.__errs || [];
if (!window.__hooked) {
  window.__hooked = true;
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('securitypolicyviolation', e => window.__errs.push('csp: ' + e.violatedDirective));
}
"""
# Elements sticking out past the right edge (ignoring ones inside a horizontal scroller).
WIDE = """
const w = document.documentElement.clientWidth, out = [];
for (const el of document.querySelectorAll('body *')) {
  const r = el.getBoundingClientRect();
  if (r.width === 0 || r.right <= w + 1) continue;
  let p = el.parentElement, scroller = false;
  while (p) { const s = getComputedStyle(p).overflowX; if (s === 'auto' || s === 'scroll' || s === 'hidden') { scroller = true; break; } p = p.parentElement; }
  if (scroller) continue;
  out.push(el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') + '.' + (el.className.baseVal ?? el.className).toString().split(' ').slice(0, 3).join('.') + ' right=' + Math.round(r.right));
  if (out.length > 4) break;
}
return [document.documentElement.scrollWidth, w, out];
"""

d.get(BASE + "/login")
d.add_cookie({"name": "session", "value": TOKEN, "path": "/"})
for p in PAGES:
    d.get(BASE + p)
    d.execute_script(HOOK)
    time.sleep(1.2)
    sw, w, wide = d.execute_script(WIDE)
    if sw > w + 1:
        problems.append(f"{p}: page scrolls sideways ({sw}px > {w}px): {wide}")
    for e in d.execute_script("return window.__errs || []"):
        problems.append(f"{p}: {e}")
    name = p.strip("/").replace("/", "_").replace("?", "_").replace("=", "") or "root"
    try:
        d.get_full_page_screenshot_as_file(f"{SHOTS}/m_{name}.png")
    except Exception:
        d.save_screenshot(f"{SHOTS}/m_{name}.png")

d.quit()
for x in problems:
    print(x)
print("PROBLEMS:", len(problems))
