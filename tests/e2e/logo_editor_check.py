"""Settings > Branding logo editor: pick a picture, size it, set a background
colour by hex, save; the saved logo is a 512x512 PNG and "Edit the current
logo" brings the editor back with the same settings.

    python tests/e2e/logo_editor_check.py <base-url> <admin-token> <out-dir>
"""
import base64, os, struct, sys, time, urllib.request, zlib
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.firefox.options import Options

base, token, out = sys.argv[1].rstrip("/"), sys.argv[2], sys.argv[3]
problems = []
def check(ok, what):
    print(("OK   " if ok else "FAIL ") + what)
    if not ok: problems.append(what)

# A 300x100 red PNG to upload
def png(w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
src = os.path.join(out, "source.png")
open(src, "wb").write(png(300, 100, (220, 30, 30)))

o = Options(); o.add_argument("-headless")
d = webdriver.Firefox(options=o); d.set_window_size(1200, 1000)
d.get(base + "/login"); d.add_cookie({"name": "session", "value": token, "path": "/"})
d.get(base + "/settings/branding"); time.sleep(1)
ed = d.find_element(By.ID, "logo-editor")
check("hidden" in ed.get_attribute("class"), "editor hidden until a picture is picked")
d.find_element(By.ID, "settings-branding-logo").send_keys(src); time.sleep(1)
check("hidden" not in ed.get_attribute("class"), "editor opens on pick")
hexbox = d.find_element(By.ID, "logo-hex"); hexbox.clear(); hexbox.send_keys("#123456"); time.sleep(0.5)
check(not d.find_element(By.ID, "logo-transparent").is_selected(), "typing a colour turns transparency off")
px = d.execute_script("var c=document.querySelector('[data-logo-canvas]').getContext('2d');return Array.from(c.getImageData(5,5,1,1).data)")
check(px[:3] == [0x12, 0x34, 0x56], f"corner shows the background colour {px}")
d.find_element(By.CSS_SELECTOR, '[data-logo-do="fill"]').click(); time.sleep(0.5)
px = d.execute_script("var c=document.querySelector('[data-logo-canvas]').getContext('2d');return Array.from(c.getImageData(5,5,1,1).data)")
check(px[0] > 200 and px[1] < 60, f"fill covers the corner with the picture {px}")
d.save_screenshot(out + "/logo_editor.png")
time.sleep(0.5)
d.find_element(By.XPATH, "//button[normalize-space()='Save logo']").click(); time.sleep(2)
req = urllib.request.Request(base + "/branding/logo?x=1")
data = urllib.request.urlopen(req).read()
w, h = struct.unpack(">II", data[16:24])
check(data[:4] == b"\x89PNG" and (w, h) == (512, 512), f"saved logo is a 512x512 PNG ({w}x{h})")
d.get(base + "/settings/branding"); time.sleep(1)
d.find_element(By.CSS_SELECTOR, "[data-logo-edit-current]").click(); time.sleep(1.5)
check("hidden" not in d.find_element(By.ID, "logo-editor").get_attribute("class"), "Edit the current logo reopens the editor")
check(d.find_element(By.ID, "logo-hex").get_attribute("value") == "#123456", "with the saved background")
d.quit()
print("PROBLEMS:", len(problems))
