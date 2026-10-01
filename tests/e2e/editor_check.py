"""Browser check of the About page's WYSIWYG editor against a local LUG Manager.

    python tests/e2e/editor_check.py <admin-session-token> <screenshot-dir>

Checks the editor's HTML -> Markdown conversion on fixed cases, types and
formats text with the toolbar, pastes hostile HTML, uploads a photo, saves,
and checks the public /about page and that re-opening the editor gives back
the same Markdown. Fails on JS errors and CSP violations.
"""
import os, sys, time, struct, zlib, tempfile
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.firefox.options import Options

BASE = os.environ.get("LUG_BASE", "http://127.0.0.1:18089")
TOKEN = sys.argv[1] if len(sys.argv) > 1 else "devtoken123"
SHOTS = sys.argv[2] if len(sys.argv) > 2 else "/tmp"

opts = Options()
opts.add_argument("-headless")
d = webdriver.Firefox(options=opts)
d.set_window_size(1300, 1000)
problems = []

HOOK = """
window.__errs = window.__errs || [];
if (!window.__hooked) {
  window.__hooked = true;
  window.addEventListener('error', e => window.__errs.push('error: ' + e.message));
  document.addEventListener('securitypolicyviolation', e => window.__errs.push('csp: ' + e.violatedDirective + ' ' + e.blockedURI));
}
"""

def errs(where):
    for e in d.execute_script("return (window.__errs||[]).splice(0)"):
        problems.append(f"{where}: {e}")

def check(cond, msg):
    if not cond:
        problems.append(msg)

def png(path):
    # 8x8 yellow PNG
    raw = b"".join(b"\x00" + b"\xff\xd7\x00" * 8 for _ in range(8))
    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

def open_editor():
    d.get(BASE + "/settings/about")
    d.execute_script(HOOK)
    time.sleep(1.0)

d.get(BASE + "/login")
d.add_cookie({"name": "session", "value": TOKEN, "path": "/"})
open_editor()
check(d.find_elements(By.CSS_SELECTOR, "[data-rich-editor][data-rich-ready]"), "editor didn't start")

# ── 1. HTML -> Markdown cases ──
CASES = [
    ("<p>Plain text</p>", "Plain text"),
    ("<h2>Head</h2><p>Body</p>", "## Head\n\nBody"),
    ("<h1>Big</h1><h4>Small</h4>", "## Big\n\n### Small"),
    ("<p>a <b>bold</b> and <i>it</i> and <s>gone</s></p>", "a **bold** and *it* and ~~gone~~"),
    ("<p><b>bold </b>word</p>", "**bold** word"),
    ("<ul><li>one</li><li>two<ul><li>inner</li></ul></li></ul>", "- one\n- two\n  - inner"),
    ("<ol><li>first</li><li>second</li></ol>", "1. first\n2. second"),
    ("<blockquote><p>quoted</p><p>more</p></blockquote>", "> quoted\n>\n> more"),
    ("<p>line one<br>line two</p>", "line one  \nline two"),
    ("<p>para one<br><br>para two</p>", "para one\n\npara two"),
    ("<p>x</p><hr><p>y</p>", "x\n\n---\n\ny"),
    ("<p><a href=\"https://ex.org/a b\">link</a></p>", "[link](https://ex.org/a%20b)"),
    ("<p><a href=\"javascript:alert(1)\">bad</a></p>", "bad"),
    ("<p><img src=\"/about/photos/x.png\" alt=\"A [table]\"></p>", "![A \\[table\\]](/about/photos/x.png)"),
    ("<p><img src=\"data:image/png;base64,AAA\"></p>", ""),
    ("<p># not a heading</p><p>1. not a list</p><p>- nor this</p>", "\\# not a heading\n\n1\\. not a list\n\n\\- nor this"),
    ("<p>2*3 = 6_ok [x] `c` &lt;b&gt;</p>", "2\\*3 = 6\\_ok \\[x\\] \\`c\\` \\<b\\>"),
    ("<div>loose text</div>", "loose text"),
    ("text at top level", "text at top level"),
    ("<p>&nbsp;</p><p>after empty</p>", "after empty"),
]
for html, want in CASES:
    got = d.execute_script("var t=document.createElement('div'); t.innerHTML=arguments[0]; return LugRichEditor.toMarkdown(t);", html)
    check(got == want, f"toMarkdown({html!r}) = {got!r}, want {want!r}")

# ── 2. Type and format with the toolbar ──
area = d.find_element(By.CSS_SELECTOR, ".rich-area")
d.execute_script("arguments[0].innerHTML='<p><br></p>'; arguments[0].focus();", area)
area.click()
area.send_keys("Welcome builders")
d.find_element(By.CSS_SELECTOR, '[data-cmd="h2"]').click()
area.send_keys(Keys.END, Keys.ENTER)
d.find_element(By.CSS_SELECTOR, '[data-cmd="p"]').click()
area.send_keys("We meet ")
d.find_element(By.CSS_SELECTOR, '[data-cmd="bold"]').click()
area.send_keys("every month")
d.find_element(By.CSS_SELECTOR, '[data-cmd="bold"]').click()
area.send_keys(" at the library.", Keys.ENTER)
d.find_element(By.CSS_SELECTOR, '[data-cmd="ul"]').click()
area.send_keys("Meetings", Keys.ENTER, "Public shows", Keys.ENTER, Keys.ENTER)
area.send_keys("Find us here")
# Select "here" and make it a link (the prompt is answered)
d.execute_script("""
  var a = arguments[0], ps = a.querySelectorAll('p'), p = ps[ps.length-1], t = p.firstChild, r = document.createRange();
  r.setStart(t, t.length - 4); r.setEnd(t, t.length);
  var s = window.getSelection(); s.removeAllRanges(); s.addRange(r);
  a.dispatchEvent(new KeyboardEvent('keyup'));""", area)
d.find_element(By.CSS_SELECTOR, '[data-cmd="link"]').click()
time.sleep(0.3)
al = d.switch_to.alert
al.send_keys("example.org/join")
al.accept()
time.sleep(0.3)
md = d.execute_script("return document.querySelector('textarea[name=markdown]').value")
check("## Welcome builders" in md, f"heading missing: {md!r}")
check("We meet **every month** at the library." in md, f"bold missing: {md!r}")
check("- Meetings\n- Public shows" in md, f"list missing: {md!r}")
check("Find us [here](https://example.org/join)" in md, f"link missing: {md!r}")

# ── 3. Hostile paste is cleaned ──
d.execute_script("""
  var a = arguments[0]; a.focus();
  var r = document.createRange(); r.selectNodeContents(a); r.collapse(false);
  var s = window.getSelection(); s.removeAllRanges(); s.addRange(r);
  // Same path as a real paste (Firefox blanks clipboard data on synthetic paste events).
  a.closest('[data-rich-editor]').richPaste('<b style="font-weight:normal" id="docs-internal"><p>Pasted <span style="font-weight:700">strong</span> '
    + '<img src=x onerror="window.__pwned=1"> <a href="javascript:window.__pwned=2">bad</a> <a href="https://ok.example/" onclick="x()">ok</a>'
    + '<iframe src="https://evil.example"></iframe></p><script>window.__pwned=3</script></b>', 'Pasted strong bad ok');
""", area)
time.sleep(0.4)
html = area.get_attribute("innerHTML")
check(not d.execute_script("return window.__pwned || 0"), "paste ran script")
check("onerror" not in html and "onclick" not in html and "iframe" not in html and "javascript:" not in html,
      f"paste kept unsafe markup: {html[-400:]!r}")
check("<strong>strong</strong>" in html, f"pasted bold lost: {html[-300:]!r}")
check('href="https://ok.example/"' in html, "pasted safe link lost")

# ── 4. Photo upload ──
tmp = tempfile.mkdtemp()
pic = os.path.join(tmp, "table.png")
png(pic)
d.execute_script("""var a=arguments[0]; a.focus(); var r=document.createRange(); r.selectNodeContents(a); r.collapse(false);
  var s=window.getSelection(); s.removeAllRanges(); s.addRange(r); a.dispatchEvent(new KeyboardEvent('keyup'));""", area)
d.find_element(By.CSS_SELECTOR, "[data-photo-input]").send_keys(pic)
for _ in range(20):
    time.sleep(0.25)
    try:
        al = d.switch_to.alert
        al.send_keys("Our club table")
        al.accept()
        break
    except Exception:
        pass
time.sleep(0.5)
img = d.execute_script("var i=document.querySelector('.rich-area img'); return i ? [i.getAttribute('src'), i.alt, i.naturalWidth] : null")
check(img and img[0].startswith("/about/photos/") and img[1] == "Our club table" and img[2] == 8, f"photo not inserted: {img}")

# ── 5. Save, then the public page ──
box = d.find_element(By.NAME, "enabled")
if not box.is_selected():
    box.click()
d.save_screenshot(f"{SHOTS}/about_editor.png")
d.find_element(By.CSS_SELECTOR, "#about-editor form button:not([data-cmd])").click()
time.sleep(1.2)
d.execute_script(HOOK)
check("public at" in d.find_element(By.ID, "about-editor").text, "save didn't confirm")
saved = d.execute_script("return document.querySelector('textarea[name=markdown]').value")
again = d.execute_script("return LugRichEditor.toMarkdown(document.querySelector('.rich-area'))")
check(saved == again, f"round trip changed the text:\n{saved!r}\n{again!r}")
errs("editor")

d.get(BASE + "/about")
d.execute_script(HOOK)
time.sleep(0.8)
page = d.page_source
check("<h2>Welcome builders</h2>" in page, "public page: heading")
check("<strong>every month</strong>" in page, "public page: bold")
check('href="https://example.org/join"' in page, "public page: link")
check('alt="Our club table"' in page, "public page: photo")
check(d.execute_script("var i=document.querySelector('article img'); return i ? i.naturalWidth : 0") == 8, "public page: photo didn't load")
check(not d.execute_script("return window.__pwned || 0"), "public page ran pasted script")
d.save_screenshot(f"{SHOTS}/about_public.png")
d.set_window_size(390, 844)
time.sleep(0.4)
sw, w = d.execute_script("return [document.documentElement.scrollWidth, document.documentElement.clientWidth]")
check(sw <= w + 1, f"/about scrolls sideways on a phone ({sw} > {w})")
d.save_screenshot(f"{SHOTS}/about_public_phone.png")
errs("/about")

# ── 6. Re-open the editor: same Markdown ──
d.set_window_size(1300, 1000)
open_editor()
reopened = d.execute_script("return LugRichEditor.toMarkdown(document.querySelector('.rich-area'))")
check(reopened == saved, f"re-opened editor differs:\n{saved!r}\n{reopened!r}")
errs("editor reopened")

d.quit()
print("PROBLEMS", len(problems))
for p in problems:
    print(" -", p)
sys.exit(1 if problems else 0)
