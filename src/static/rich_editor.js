// Small WYSIWYG editor for [data-rich-editor] (the About page).
//
// The editable area shows formatted text; what's saved is Markdown, kept in
// the block's <textarea name="markdown">. The server renders that Markdown
// with raw HTML switched off and links checked, so nothing typed or pasted
// here can put script on the public page. Pasted content is cleaned down to
// headings, paragraphs, lists, quotes, links, images, bold/italic/strike.
(function () {
  'use strict';

  var BLOCK = { P: 1, DIV: 1, H1: 1, H2: 1, H3: 1, H4: 1, H5: 1, H6: 1, UL: 1, OL: 1, LI: 1,
                BLOCKQUOTE: 1, HR: 1, PRE: 1, TABLE: 1, TBODY: 1, THEAD: 1, TR: 1, SECTION: 1, ARTICLE: 1 };

  function safeUrl(u) {
    if (!u) return '';
    u = u.trim();
    if (!/^(https?:|mailto:|\/|#)/i.test(u) || /^\/\//.test(u)) return '';
    return u.replace(/ /g, '%20').replace(/\(/g, '%28').replace(/\)/g, '%29');
  }

  // ── DOM -> Markdown ──

  function esc(t) { return t.replace(/([\\`*_\[\]~<>])/g, '\\$1'); }
  // Things that would start a heading, quote or list at the start of a line.
  function escStart(line) {
    return line.replace(/^(\s*)(\d+)([.)])(?=\s|$)/, '$1$2\\$3').replace(/^(\s*)([#>+=-])/, '$1\\$2');
  }
  function wrap(s, m) {
    var core = s.trim();
    if (!core) return s;
    return s.match(/^\s*/)[0] + m + core + m + s.match(/\s*$/)[0];
  }
  function hasBlock(el) {
    for (var c = el.firstElementChild; c; c = c.nextElementSibling)
      if (BLOCK[c.tagName] || hasBlock(c)) return true;
    return false;
  }

  function one(n) {
    if (n.nodeType === 3) return esc(n.nodeValue.replace(/[\s ]+/g, ' '));
    if (n.nodeType !== 1) return '';
    switch (n.tagName) {
      case 'BR': return '\n';
      case 'B': case 'STRONG': return wrap(inline(n), '**');
      case 'I': case 'EM': return wrap(inline(n), '*');
      case 'S': case 'DEL': case 'STRIKE': return wrap(inline(n), '~~');
      case 'CODE': return '`' + n.textContent.replace(/`/g, '') + '`';
      case 'A': {
        var href = safeUrl(n.getAttribute('href')), text = inline(n);
        return href && text.trim() ? '[' + text.trim() + '](' + href + ')' : text;
      }
      case 'IMG': {
        var src = safeUrl(n.getAttribute('src'));
        return src ? '![' + esc(n.getAttribute('alt') || '') + '](' + src + ')' : '';
      }
      default: return inline(n);
    }
  }
  function inline(el) {
    var out = '';
    for (var c = el.firstChild; c; c = c.nextSibling) out += one(c);
    return out;
  }
  function oneLine(s) { return s.replace(/\s*\n\s*/g, ' ').trim(); }

  function list(el, ordered, indent) {
    var lines = [], i = 1;
    for (var li = el.firstElementChild; li; li = li.nextElementSibling) {
      if (li.tagName === 'UL' || li.tagName === 'OL') {   // execCommand nests lists directly
        lines.push(list(li, li.tagName === 'OL', indent + '   '));
        continue;
      }
      var marker = ordered ? (i++) + '. ' : '- ', text = '', subs = [];
      for (var c = li.firstChild; c; c = c.nextSibling) {
        if (c.nodeType === 1 && (c.tagName === 'UL' || c.tagName === 'OL'))
          subs.push(list(c, c.tagName === 'OL', indent + new Array(marker.length + 1).join(' ')));
        else if (c.nodeType === 1 && BLOCK[c.tagName]) text += (text ? ' ' : '') + inline(c);
        else text += one(c);
      }
      text = oneLine(text);
      if (text) lines.push(indent + marker + escStart(text));
      subs.forEach(function (s) { if (s) lines.push(s); });
    }
    return lines.join('\n');
  }

  function blocks(el, out) {
    var para = '';
    // Line breaks become hard breaks; an empty line starts a new paragraph.
    function flush() {
      var cur = [];
      para.split('\n').forEach(function (l) {
        l = l.trim();
        if (l) cur.push(escStart(l));
        else if (cur.length) { out.push(cur.join('  \n')); cur = []; }
      });
      if (cur.length) out.push(cur.join('  \n'));
      para = '';
    }
    for (var n = el.firstChild; n; n = n.nextSibling) {
      var tag = n.nodeType === 1 ? n.tagName : '';
      if (!tag || (!BLOCK[tag] && !hasBlock(n))) { para += one(n); continue; }
      flush();
      if (/^H[1-6]$/.test(tag)) {
        var t = oneLine(inline(n));
        if (t) out.push((tag === 'H1' || tag === 'H2' ? '## ' : '### ') + t);
      } else if (tag === 'UL' || tag === 'OL') {
        var l = list(n, tag === 'OL', '');
        if (l) out.push(l);
      } else if (tag === 'BLOCKQUOTE') {
        var inner = [];
        blocks(n, inner);
        if (inner.length) out.push(inner.join('\n\n').split('\n').map(function (x) { return x ? '> ' + x : '>'; }).join('\n'));
      } else if (tag === 'HR') {
        out.push('---');
      } else if (tag === 'TR') {
        var cells = [];
        for (var c = n.firstElementChild; c; c = c.nextElementSibling) { var v = oneLine(inline(c)); if (v) cells.push(v); }
        if (cells.length) out.push(escStart(cells.join(' · ')));
      } else if (tag === 'PRE') {
        para = esc(n.textContent);
        flush();
      } else if (hasBlock(n)) {
        blocks(n, out);
      } else {
        para = inline(n);
        flush();
      }
    }
    flush();
  }

  function toMarkdown(root) {
    var out = [];
    blocks(root, out);
    return out.join('\n\n').replace(/\n{3,}/g, '\n\n').trim();
  }

  // ── Pasted HTML -> the few tags we keep ──

  var KEEP = { P: 1, H1: 1, H2: 1, H3: 1, H4: 1, H5: 1, H6: 1, UL: 1, OL: 1, LI: 1, BLOCKQUOTE: 1, B: 1, STRONG: 1,
               I: 1, EM: 1, S: 1, DEL: 1, A: 1, IMG: 1, BR: 1, HR: 1 };
  var DROP = { SCRIPT: 1, STYLE: 1, META: 1, TITLE: 1, LINK: 1, IFRAME: 1, OBJECT: 1, EMBED: 1, svg: 1, SVG: 1,
               TEMPLATE: 1, NOSCRIPT: 1, BUTTON: 1, INPUT: 1, SELECT: 1, TEXTAREA: 1, FORM: 1 };

  function unwrap(n) {
    var p = n.parentNode;
    while (n.firstChild) p.insertBefore(n.firstChild, n);
    p.removeChild(n);
  }
  function rename(n, tag) {
    var r = n.ownerDocument.createElement(tag);
    while (n.firstChild) r.appendChild(n.firstChild);
    n.parentNode.replaceChild(r, n);
    return r;
  }
  function clean(node) {
    Array.prototype.slice.call(node.childNodes).forEach(function (n) {
      if (n.nodeType === 3) return;
      if (n.nodeType !== 1) { node.removeChild(n); return; }
      var tag = n.tagName;
      if (DROP[tag]) { node.removeChild(n); return; }
      var style = (n.getAttribute('style') || '').toLowerCase();
      // Word / Google Docs mark bold and italic with styles on spans.
      if (tag === 'SPAN' || tag === 'FONT') {
        var bold = /font-weight:\s*(bold|[6-9]00)/.test(style), ital = /font-style:\s*italic/.test(style);
        if (bold || ital) {
          n = rename(n, bold ? 'strong' : 'em');
          if (bold && ital) { var e = n.ownerDocument.createElement('em'); while (n.firstChild) e.appendChild(n.firstChild); n.appendChild(e); }
          tag = n.tagName;
        }
      }
      clean(n);
      // Google Docs wraps the whole paste in <b style="font-weight:normal">.
      if (!KEEP[tag] || (tag === 'B' && /font-weight:\s*(normal|400)/.test(style))) {
        if (tag === 'DIV' || tag === 'TR' || tag === 'TABLE' || tag === 'SECTION' || tag === 'ARTICLE') n = rename(n, 'p');
        else { unwrap(n); return; }
      }
      var allowed = n.tagName === 'A' ? ['href'] : n.tagName === 'IMG' ? ['src', 'alt'] : [];
      Array.prototype.slice.call(n.attributes).forEach(function (a) {
        if (allowed.indexOf(a.name) < 0) n.removeAttribute(a.name);
      });
      if (n.tagName === 'A' && !safeUrl(n.getAttribute('href'))) unwrap(n);
      else if (n.tagName === 'IMG' && !safeUrl(n.getAttribute('src'))) n.parentNode.removeChild(n);
    });
  }

  function escHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  // ── Wiring ──

  function init(box) {
    if (box.dataset.richReady) return;
    box.dataset.richReady = '1';
    var area = box.querySelector('.rich-area'), field = box.querySelector('textarea[name="markdown"]');
    var fileInput = box.querySelector('[data-photo-input]'), status = box.querySelector('.rich-status');
    var max = parseInt(box.dataset.max || '0', 10), saved = null;
    if (!area || !field) return;
    if (!area.innerHTML.trim()) area.innerHTML = '<p><br></p>';

    function say(msg, bad) {
      if (!status) return;
      status.textContent = msg || '';
      status.className = 'rich-status text-xs mt-1 ' + (bad ? 'text-red-600' : 'text-gray-500');
    }
    function sync() {
      field.value = toMarkdown(area);
      if (max && field.value.length > max * 0.9)
        say(field.value.length + ' of ' + max + ' characters', field.value.length > max);
      else if (status && /characters$/.test(status.textContent)) say('');
    }
    function saveSel() {
      var s = window.getSelection();
      if (s.rangeCount && area.contains(s.anchorNode)) saved = s.getRangeAt(0).cloneRange();
    }
    function restoreSel() {
      area.focus();
      if (!saved) return;
      var s = window.getSelection();
      s.removeAllRanges();
      s.addRange(saved);
    }
    function exec(cmd, arg) {
      area.focus();
      document.execCommand(cmd, false, arg);
      sync();
    }
    function inside(tag) {
      var s = window.getSelection();
      for (var n = s.anchorNode; n && n !== area; n = n.parentNode) if (n.nodeName === tag) return true;
      return false;
    }
    function addLink() {
      saveSel();
      var url = window.prompt('Link address (for example https://example.org or mailto:hello@example.org)', 'https://');
      if (!url || url === 'https://') { restoreSel(); return; }
      url = url.trim();
      if (!/^(https?:|mailto:|\/|#)/i.test(url)) url = (/@/.test(url) && !/\//.test(url) ? 'mailto:' : 'https://') + url;
      if (!safeUrl(url)) { restoreSel(); say('That link address isn\'t allowed. Use an https:// or mailto: address.', true); return; }
      restoreSel();
      if (window.getSelection().isCollapsed) exec('insertHTML', '<a href="' + escHtml(url) + '">' + escHtml(url) + '</a>');
      else exec('createLink', url);
    }
    function upload(file) {
      if (!file) return;
      if (!/^image\//.test(file.type)) { say('Choose a JPEG, PNG, GIF or WebP photo.', true); return; }
      say('Uploading ' + file.name + '…');
      var data = new FormData();
      data.append('photo', file);
      fetch(box.dataset.upload, { method: 'POST', body: data, credentials: 'same-origin' })
        .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, j: j }; }); })
        .then(function (r) {
          if (!r.ok || !r.j.url) { say(r.j.error || 'Upload failed.', true); return; }
          var alt = window.prompt('Describe the photo in a few words (read aloud to people who can\'t see it):', '') || '';
          restoreSel();
          exec('insertHTML', '<img src="' + escHtml(r.j.url) + '" alt="' + escHtml(alt) + '">');
          say('Photo added. Press Save to publish it.');
        })
        .catch(function () { say('Upload failed. Check your connection and try again.', true); });
    }

    // Pasted HTML is parsed inertly (DOMParser runs no scripts) and cleaned
    // before it reaches the page.
    function pasteHtml(html, text) {
      if (html) {
        var doc = new DOMParser().parseFromString(html, 'text/html');
        clean(doc.body);
        exec('insertHTML', doc.body.innerHTML);
      } else if (text) {
        exec('insertText', text);
      }
    }
    box.richPaste = pasteHtml;   // browser tests can't fake clipboard contents

    var actions = {
      p: function () { exec('formatBlock', '<p>'); },
      h2: function () { exec('formatBlock', '<h2>'); },
      h3: function () { exec('formatBlock', '<h3>'); },
      bold: function () { exec('bold'); },
      italic: function () { exec('italic'); },
      strike: function () { exec('strikeThrough'); },
      ul: function () { exec('insertUnorderedList'); },
      ol: function () { exec('insertOrderedList'); },
      quote: function () { exec('formatBlock', inside('BLOCKQUOTE') ? '<p>' : '<blockquote>'); },
      hr: function () { exec('insertHorizontalRule'); },
      link: addLink,
      unlink: function () { exec('unlink'); },
      image: function () { saveSel(); if (fileInput) fileInput.click(); },
      undo: function () { exec('undo'); },
      redo: function () { exec('redo'); }
    };

    box.querySelectorAll('[data-cmd]').forEach(function (b) {
      // Keep the text selection when a toolbar button is pressed.
      b.addEventListener('mousedown', function (e) { e.preventDefault(); });
      b.addEventListener('click', function (e) {
        e.preventDefault();
        var f = actions[b.dataset.cmd];
        if (f) f();
      });
    });
    if (fileInput) fileInput.addEventListener('change', function () {
      upload(fileInput.files[0]);
      fileInput.value = '';
    });

    area.addEventListener('focus', function () {
      try { document.execCommand('defaultParagraphSeparator', false, 'p'); } catch (e) { /* older browsers */ }
    });
    area.addEventListener('input', sync);
    area.addEventListener('keyup', saveSel);
    area.addEventListener('mouseup', saveSel);
    area.addEventListener('keydown', function (e) {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') { e.preventDefault(); addLink(); }
    });
    area.addEventListener('paste', function (e) {
      var cd = e.clipboardData;
      if (!cd) return;
      e.preventDefault();
      var img = Array.prototype.find.call(cd.files || [], function (f) { return /^image\//.test(f.type); });
      if (img) { saveSel(); upload(img); return; }
      pasteHtml(cd.getData('text/html'), cd.getData('text/plain'));
    });
    area.addEventListener('drop', function (e) {
      var dt = e.dataTransfer;
      e.preventDefault();
      if (!dt) return;
      var img = Array.prototype.find.call(dt.files || [], function (f) { return /^image\//.test(f.type); });
      if (img) { saveSel(); upload(img); return; }
      var text = dt.getData('text/plain');
      if (text) exec('insertText', text);
    });
    sync();
  }

  function scan(root) {
    (root.querySelectorAll ? root : document).querySelectorAll('[data-rich-editor]').forEach(init);
    if (root.matches && root.matches('[data-rich-editor]')) init(root);
  }

  // The Markdown goes with the form however it's submitted.
  document.addEventListener('htmx:configRequest', function (e) {
    var form = e.detail.elt;
    if (!form || !form.querySelectorAll) return;
    form.querySelectorAll('[data-rich-editor]').forEach(function (box) {
      var area = box.querySelector('.rich-area');
      if (area) e.detail.parameters.markdown = toMarkdown(area);
    });
  });
  document.addEventListener('htmx:load', function (e) { scan(e.detail.elt); });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { scan(document); });
  else scan(document);

  window.LugRichEditor = { toMarkdown: toMarkdown, clean: clean };
})();
