"""Browser check of password sign-in and two-factor against a local LUG Manager.

    python tests/e2e/accounts_check.py <session-token> <email-of-that-member> <screenshot-dir>

The member must have that email and no password or two-factor yet (use a
scratch DB). Sets a password and turns on two-factor through the UI, then
signs out and back in with email + password + code. Fails on JS errors and
CSP violations, and checks the sign-in pages at phone width.
"""
import base64, hashlib, hmac, os, struct, sys, time
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options

BASE = os.environ.get("LUG_BASE", "http://127.0.0.1:18089")
TOKEN, EMAIL = sys.argv[1], sys.argv[2]
SHOTS = sys.argv[3] if len(sys.argv) > 3 else "/tmp"
PASSWORD = "bricks all the way down"

opts = Options()
opts.add_argument("-headless")
d = webdriver.Firefox(options=opts)
d.set_window_size(1300, 950)
problems = []
HOOK = """window.__errs = window.__errs || [];
if (!window.__hooked) { window.__hooked = true;
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('securitypolicyviolation', e => window.__errs.push('csp: ' + e.violatedDirective + ' ' + e.blockedURI)); }"""

def errs(where):
    for e in d.execute_script("return (window.__errs||[]).splice(0)"):
        problems.append(f"{where}: {e}")

def check(cond, msg):
    if not cond:
        problems.append(msg)

def totp(secret, offset=0):
    key = base64.b32decode(secret.replace(" ", "").upper())
    step = int(time.time()) // 30 + offset
    mac = hmac.new(key, struct.pack(">Q", step), hashlib.sha1).digest()
    o = mac[-1] & 15
    return "%06d" % ((struct.unpack(">I", mac[o:o + 4])[0] & 0x7fffffff) % 1000000)

def go(path):
    d.get(BASE + path)
    d.execute_script(HOOK)
    time.sleep(0.8)

def phone_ok(name):
    d.set_window_size(390, 844)
    time.sleep(0.4)
    sw, w = d.execute_script("return [document.documentElement.scrollWidth, document.documentElement.clientWidth]")
    check(sw <= w + 1, f"{name} scrolls sideways on a phone ({sw} > {w})")
    d.save_screenshot(f"{SHOTS}/acct_{name}_phone.png")
    d.set_window_size(1300, 950)

# ── Signed in (session cookie): set a password, then two-factor ──
d.get(BASE + "/login")
d.add_cookie({"name": "session", "value": TOKEN, "path": "/"})
go("/account/security")
check("Set a password to sign in with" in d.page_source, "security page: no 'set a password'")
d.find_element(By.NAME, "password").send_keys(PASSWORD)
d.find_element(By.NAME, "confirm").send_keys(PASSWORD)
d.find_element(By.XPATH, "//button[contains(., 'Set password')]").click()
time.sleep(1.0)
check("Password set" in d.page_source, "password wasn't set")

d.find_element(By.XPATH, "//button[contains(., 'Set up two-factor')]").click()
time.sleep(1.2)
qr = d.execute_script("var q=document.querySelector('[data-qr]'); return q ? q.querySelectorAll('canvas,img').length : -1")
check(qr and qr > 0, f"QR code wasn't drawn ({qr})")
secret = d.find_element(By.CSS_SELECTOR, "code.select-all").text
check(len(secret.replace(" ", "")) == 32, f"secret looks wrong: {secret!r}")
d.save_screenshot(f"{SHOTS}/acct_setup.png")
phone_ok("setup")
d.find_element(By.NAME, "code").send_keys(totp(secret))
d.find_element(By.XPATH, "//button[contains(., 'Turn on')]").click()
time.sleep(1.2)
codes = d.find_elements(By.CSS_SELECTOR, "ul[aria-label='Recovery codes'] li")
check(len(codes) == 10, f"expected 10 recovery codes, got {len(codes)}")
d.save_screenshot(f"{SHOTS}/acct_codes.png")
errs("account security")

# ── Signed out: email + password, then the code ──
d.delete_all_cookies()
go("/login")
d.save_screenshot(f"{SHOTS}/acct_login.png")
phone_ok("login")
d.find_element(By.ID, "login-email").send_keys(EMAIL)
d.find_element(By.ID, "login-password").send_keys(PASSWORD)
d.find_element(By.XPATH, "//form[@action='/auth/password']//button").click()
time.sleep(1.2)
check("/auth/2fa" in d.current_url, f"expected the code page, got {d.current_url}")
d.execute_script(HOOK)
phone_ok("code")
d.find_element(By.NAME, "code").send_keys("000000")
d.find_element(By.XPATH, "//button[contains(., 'Sign in')]").click()
time.sleep(1.0)
check("Tries left: 4" in d.page_source, "wrong code wasn't reported")
d.find_element(By.NAME, "code").send_keys(totp(secret, 1))     # the setup code's step is used up
d.find_element(By.XPATH, "//button[contains(., 'Sign in')]").click()
time.sleep(1.5)
check("/dashboard" in d.current_url, f"didn't reach the dashboard: {d.current_url}")
d.execute_script(HOOK)
errs("sign-in")

d.quit()
print("PROBLEMS", len(problems))
for p in problems:
    print(" -", p)
sys.exit(1 if problems else 0)
