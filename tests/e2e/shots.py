"""Screenshots of pages at desktop (1300px) and phone (390px) widths, flagging
any page that scrolls sideways on a phone, JS errors and CSP violations.

    python tests/e2e/shots.py <base-url> <session-token> <out-dir> [--dark] <paths...>
    (a path "page>fragment" opens the fragment in the modal over the page, e.g. /members>/members/3)

Prints SIDEWAYS / ERROR lines and ends with "PROBLEMS: n".
"""
import os, sys, time
from selenium import webdriver
from selenium.webdriver.firefox.options import Options

args = [a for a in sys.argv[1:] if not a.startswith("--")]
dark = "--dark" in sys.argv
base, token, out, paths = args[0].rstrip("/"), args[1], args[2], args[3:]
os.makedirs(out, exist_ok=True)
o = Options(); o.add_argument("-headless")
d = webdriver.Firefox(options=o)
d.get(base + "/login"); d.add_cookie({"name": "session", "value": token, "path": "/"})
if dark:
    d.execute_script("try{localStorage.setItem('lm-theme','dark')}catch(e){}")
HOOK = """window.__errs=[];window.addEventListener('error',e=>window.__errs.push('error: '+e.message));
document.addEventListener('securitypolicyviolation',e=>window.__errs.push('csp: '+e.violatedDirective+' '+(e.blockedURI||'')));"""
problems = 0
for w, h, tag in ((1300, 950, "d"), (390, 844, "m")):
    d.set_window_size(w, h)
    for p in paths:
        # "page>fragment" opens the fragment in the modal over the page (e.g. /members>/members/3)
        page, _, modal = p.partition(">")
        page, _, click = page.partition("!")   # "page!<css selector>": click the first match first
        d.get(base + page); d.execute_script(HOOK); time.sleep(1.2)
        if click:
            d.execute_script("var e=document.querySelector(arguments[0]); if(e){e.scrollIntoView({block:'center'}); e.click();}", click); time.sleep(1.5)
        if modal:
            modal, _, focus = modal.partition("@")   # "@<css selector>": scroll that into view
            d.execute_script("htmx.ajax('GET', arguments[0], '#modal')", modal); time.sleep(1.2)
            if focus:
                d.execute_script("var e=document.querySelector(arguments[0]); if(e) e.scrollIntoView({block:'center'})", focus); time.sleep(0.3)
        name = p.strip("/").replace("/", "_").replace("?", "_").replace("&", "_").replace(">", "__").replace("!", "_").replace("[", "").replace("]", "").replace("=", "").replace("@", "_").replace(".", "").replace(" ", "") or "root"
        if modal: d.save_screenshot(f"{out}/{tag}_{name}.png")
        else: d.get_full_page_screenshot_as_file(f"{out}/{tag}_{name}.png")
        errs = d.execute_script("return window.__errs || []")
        for e in errs:
            print("ERROR", tag, p, e); problems += 1
        if tag == "m":
            sw, cw = d.execute_script("return [document.documentElement.scrollWidth, document.documentElement.clientWidth]")
            if sw > cw + 1:
                print("SIDEWAYS", p, sw, cw); problems += 1
d.quit()
print("PROBLEMS:", problems)
