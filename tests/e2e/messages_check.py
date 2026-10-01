"""Browser check of Settings > Message wording and Settings > Discord.

    python tests/e2e/messages_check.py <admin-session-token> <screenshot-dir>

Opens the message list and an editor, clicks a placeholder chip to insert it,
waits for the live preview to update, saves, then resets. Checks the Discord
settings page renders its sections. Fails on JS errors and CSP violations.
Uses a scratch DB (it saves a template, then resets it).
"""
import os, sys, time
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options

BASE = os.environ.get("LUG_BASE", "http://127.0.0.1:18089")
TOKEN = sys.argv[1] if len(sys.argv) > 1 else "devtoken123"
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "/tmp"
opts = Options()
opts.add_argument("-headless")
d = webdriver.Firefox(options=opts)
d.set_window_size(1300, 950)
problems = []
HOOK = """window.__errs = window.__errs || [];
if (!window.__hooked) { window.__hooked = true;
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('securitypolicyviolation', e => window.__errs.push('csp: ' + e.violatedDirective + ' ' + e.blockedURI));
  document.addEventListener('htmx:responseError', e => window.__errs.push('htmx: ' + e.detail.xhr.status + ' ' + (e.detail.pathInfo||{}).requestPath)); }"""

def go(path):
    d.get(BASE + path)
    d.execute_script(HOOK)
    time.sleep(1.0)

def errs(where):
    for e in d.execute_script("return (window.__errs||[]).splice(0)"):
        problems.append(f"{where}: {e}")

def check(cond, msg):
    if not cond:
        problems.append(msg)

d.get(BASE + "/login")
d.add_cookie({"name": "session", "value": TOKEN, "path": "/"})

go("/settings/messages")
check("Event announcement" in d.page_source and "Sign-in emails" in d.page_source, "message list incomplete")
d.save_screenshot(f"{SHOTS}/messages_list.png")
d.find_element(By.XPATH, "//a[contains(., 'Meeting reminder')]").click()
time.sleep(1.2)
d.execute_script(HOOK)
body = d.find_element(By.ID, "msg-body")
body.clear()
body.send_keys("Coming up: ")
d.find_element(By.CSS_SELECTOR, "button[data-text='{title}']").click()
time.sleep(1.2)
preview = d.find_element(By.ID, "msg-preview").text
check("Coming up: October meeting" in preview, f"preview didn't update: {preview!r}")
d.save_screenshot(f"{SHOTS}/messages_edit.png")
d.find_element(By.XPATH, "//form//button[normalize-space()='Save']").click()
time.sleep(1.2)
check("Saved." in d.page_source, "save didn't confirm")
d.find_element(By.XPATH, "//button[contains(., 'Reset to built-in')]").click()
time.sleep(0.5)
try:
    d.switch_to.alert.accept()
except Exception:
    pass
time.sleep(1.0)
check("Back to the built-in wording" in d.page_source, "reset didn't confirm")
errs("messages")

go("/settings")
for text in ("Server, channels and roles", "What to post", "Quiet mode", "Members", "Discord nicknames"):
    check(text in d.page_source, f"Discord settings missing {text!r}")
d.save_screenshot(f"{SHOTS}/discord_settings.png")
errs("settings")

go("/settings/chat-activity")
check("Chat activity" in d.page_source, "activity page")
errs("activity")

d.set_window_size(390, 844)
for path in ("/settings/messages/event.announcement", "/settings", "/settings/chat-activity"):
    go(path)
    sw, w = d.execute_script("return [document.documentElement.scrollWidth, document.documentElement.clientWidth]")
    check(sw <= w + 1, f"{path} scrolls sideways on a phone ({sw} > {w})")
    errs(path + " (phone)")
d.save_screenshot(f"{SHOTS}/messages_phone.png")

d.quit()
print("PROBLEMS", len(problems))
for p in problems:
    print(" -", p)
sys.exit(1 if problems else 0)
