# Browser smoke test

`smoke.py` drives a real (headless) Firefox through the main pages of a
running LUG Manager and reports JS errors, CSP violations and broken
interactions (theme switch, members table, bulk bar, modals, EasyMDE, kiosk
QR, ...). It isn't part of `ctest` because it needs Firefox and a server.

```bash
python3 -m venv /tmp/lm-venv && /tmp/lm-venv/bin/pip install selenium
# start the server on :18089 with a scratch DB, create a member + session
# whose raw token you pass below (see the sessions table: token = sha256(raw))
SE_AVOID_STATS=true /tmp/lm-venv/bin/python tests/e2e/smoke.py <raw-session-token> /tmp/shots
```

Exit output ends with `PROBLEMS: 0` when everything passed; screenshots of
each page land in the given directory.

Other checks, run the same way:
- `mobile.py <token> <dir> [paths...]`: phone width (390px), sideways scrolling, JS errors.
- `editor_check.py <admin-token> <dir>`: the About page's WYSIWYG editor (HTML to Markdown
  conversion cases, toolbar formatting, a hostile paste, photo upload, save, the public
  `/about` page, and reopening without changes). It saves the About page, so use a scratch DB.
- `demo_check.py <url> <dir>`: the exported static demo (see the file for how to serve it).
