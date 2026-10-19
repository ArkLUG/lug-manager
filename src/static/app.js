// Event wiring for the whole app, so templates need no inline event handlers
// (onclick=..., hx-on::...): with a nonce-based Content-Security-Policy the
// browser refuses to run those. Elements declare behaviour with data-*
// attributes instead; this file (served from 'self') does the rest.
//
//   data-action="..."        click behaviours (see ACTIONS below)
//   data-change="submit|bulk-action|palette|smtp-preset"   (palette: switch the colour theme now)
//   data-after-success="close-modal reload:/path"   after a successful htmx request
//   data-destroy-select="<id>"   destroy a Tom Select before an htmx request
//   data-init-selects            re-init channel/role Tom Selects after settle
//   data-tomselect="multi|single" turn a <select> into a Tom Select after settle
(function () {
  'use strict';

  function call(name) {
    var fn = window[name];
    if (typeof fn === 'function') return fn.apply(null, Array.prototype.slice.call(arguments, 1));
  }

  function copyFrom(btn) {
    var el = document.querySelector(btn.dataset.copyTarget);
    if (!el || !navigator.clipboard) return;
    var text = ('value' in el && el.value !== undefined && el.tagName !== 'DIV' && el.tagName !== 'SPAN' && el.tagName !== 'CODE')
      ? el.value : el.textContent;
    navigator.clipboard.writeText(text).then(function () {
      var old = btn.textContent;
      btn.textContent = 'Copied!';
      setTimeout(function () { btn.textContent = old; }, 1500);
    });
  }

  function startTour(btn) {
    var src = document.getElementById(btn.dataset.tour);
    if (!src) return;
    try { call('pageTour', JSON.parse(src.textContent)); } catch (e) { console.error('tour', e); }
  }

  var ACTIONS = {
    'close-modal':        function () { call('closeModal'); },
    'toggle-sidebar':     function () { call('toggleSidebar'); },
    'close-sidebar':      function () { call('closeSidebar'); },
    'toggle-accordion':   function (el) { call('toggleNavAccordion', el); },
    'set-theme':          function (el) { call('setTheme', el.dataset.themeValue); },
    'show-tab':           function (el) { call('showTab', el.dataset.tab); },
    'start-tour':         function () { call('startTour'); },
    'tour':               startTour,
    'open-map':           function (el) { window.open(call('mapUrl', el.dataset.location), '_blank'); },
    'toggle-next-row':    function (el) { var r = el.closest('tr'); if (r && r.nextElementSibling) r.nextElementSibling.classList.toggle('hidden'); },
    'toggle-attendance':  function (el) { call('toggleAttendance', el, el.dataset.url, el.dataset.target); },
    'set-member-dues':    function (el) { call('setMemberDues', el.dataset.memberId, el.dataset.memberName); },
    'mark-unpaid':        function () { call('markUnpaid'); },
    'close-dues':         function () { call('closeDuesModal'); },
    'bulk-clear':         function () { call('bulkClear'); },
    'copy':               copyFrom,
    'select-self':        function (el) { el.select(); },
    'show-hidden':        function (el) {
      document.querySelectorAll(el.dataset.target).forEach(function (x) { x.classList.remove('hidden'); });
      el.remove();
    },
    'insert-text':        function (el) {
      var t = document.querySelector(el.dataset.target);
      if (!t) return;
      var a = t.selectionStart || 0, b = t.selectionEnd || 0, text = el.dataset.text || '';
      t.value = t.value.slice(0, a) + text + t.value.slice(b);
      t.focus();
      t.selectionStart = t.selectionEnd = a + text.length;
      t.dispatchEvent(new Event('keyup', { bubbles: true }));
    },
    'print':              function () { window.print(); },
    'fullscreen':         function () { var d = document.documentElement; if (d.requestFullscreen) d.requestFullscreen(); }
  };

  document.addEventListener('click', function (e) {
    // Clicking the dimmed backdrop (not the dialog) closes the modal.
    if (e.target && e.target.id === 'modal-backdrop') { call('closeModal'); return; }
    var el = e.target.closest ? e.target.closest('[data-action]') : null;
    if (!el) return;
    var fn = ACTIONS[el.dataset.action];
    if (!fn) return;
    if (el.tagName === 'A') e.preventDefault();
    fn(el, e);
  });

  document.addEventListener('change', function (e) {
    var el = e.target;
    if (!el || !el.dataset) return;
    if (el.dataset.change === 'submit' && el.form) el.form.requestSubmit();
    else if (el.dataset.change === 'bulk-action') call('bulkActionChanged');
    else if (el.dataset.change === 'smtp-preset') {
      // Settings > Email: a provider fills in its server, port and security
      var o = el.options[el.selectedIndex], f = el.form;
      if (!o || !o.dataset.host || !f) return;
      f.elements.smtp_host.value = o.dataset.host;
      f.elements.smtp_port.value = o.dataset.port;
      f.elements.smtp_security.value = o.dataset.sec;
      var hint = document.getElementById('smtp-preset-hint');
      if (hint) hint.textContent = o.dataset.hint || '';
    }
    else if (el.dataset.change === 'palette') {
      document.documentElement.setAttribute('data-palette', el.value);
      document.querySelectorAll('[data-change="palette"]').forEach(function (o) {
        if (o !== el) { if (o.type === 'radio') o.checked = o.value === el.value; else o.value = el.value; }
      });
    }
  });

  function initTomSelect(el) {
    if (!window.TomSelect || el.tomselect) return;
    if (el.dataset.tomselect === 'multi') {
      var ts = new TomSelect(el, { maxOptions: null, plugins: ['remove_button'] });
      ts.on('item_add', function () { ts.setTextboxValue(''); });
    } else {
      new TomSelect(el, { maxOptions: null });
    }
  }

  document.addEventListener('htmx:beforeRequest', function (e) {
    var el = e.detail && e.detail.elt;
    if (el && el.dataset && el.dataset.destroySelect) call('destroyChannelSelect', el.dataset.destroySelect);
  });

  document.addEventListener('htmx:afterSettle', function (e) {
    var el = e.detail && e.detail.elt;
    if (!el || !el.dataset) return;
    if ('initSelects' in el.dataset) call('initChannelSelects');
    if (el.dataset.tomselect) initTomSelect(el);
    // Selects inside swapped-in content whose options are already rendered
    // (no hx-get of their own - those init when their options arrive).
    if (el.querySelectorAll) el.querySelectorAll('select[data-tomselect]:not([hx-get])').forEach(initTomSelect);
  });

  // QR codes: <div data-qr="text"> (two-factor setup), drawn with the vendored QRCode lib.
  function initQr(root) {
    if (typeof QRCode === 'undefined' || !root || !root.querySelectorAll) return;
    root.querySelectorAll('[data-qr]:not([data-qr-done])').forEach(function (el) {
      el.setAttribute('data-qr-done', '1');
      new QRCode(el, { text: el.getAttribute('data-qr'), width: 192, height: 192 });
    });
  }

  // Calendar subscribe blocks: <div data-cal-feed="/path.ics" data-cal-name="..">
  // (utils/web/CalendarLinks.hpp). The feed path becomes full links for this
  // site's address: webcal://, Google "add by URL", Outlook.com and Microsoft
  // 365 "add from web", the copyable link and a QR code.
  function initCalFeeds(root) {
    if (!root || !root.querySelectorAll) return;
    var blocks = Array.prototype.slice.call(root.querySelectorAll('[data-cal-feed]:not([data-cal-done])'));
    if (root.matches && root.matches('[data-cal-feed]:not([data-cal-done])')) blocks.push(root);
    blocks.forEach(function (el) {
      el.setAttribute('data-cal-done', '1');
      var url = window.location.origin + el.getAttribute('data-cal-feed');
      var webcal = url.replace(/^https?:/, 'webcal:');
      var name = encodeURIComponent(el.getAttribute('data-cal-name') || 'Calendar');
      var links = {
        webcal: webcal,
        google: 'https://calendar.google.com/calendar/render?cid=' + encodeURIComponent(webcal),
        outlook: 'https://outlook.live.com/calendar/0/addfromweb?url=' + encodeURIComponent(url) + '&name=' + name,
        m365: 'https://outlook.office.com/calendar/0/addfromweb?url=' + encodeURIComponent(url) + '&name=' + name
      };
      el.querySelectorAll('[data-cal-link]').forEach(function (a) { a.href = links[a.getAttribute('data-cal-link')]; });
      // (a [data-cal-fixed] Google button already points at the shared Google calendar)
      var input = el.querySelector('[data-cal-url]');
      if (input) input.value = url;
      var qr = el.querySelector('[data-cal-qr]');
      if (qr) qr.setAttribute('data-qr', url);
    });
  }
  function initLinks(root) { initCalFeeds(root); initQr(root); }
  document.addEventListener('htmx:load', function (e) { initLinks(e.detail && e.detail.elt); });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { initLinks(document); });
  else initLinks(document);

  function initStaticSelects() {
    document.querySelectorAll('select[data-tomselect]:not([hx-get])').forEach(initTomSelect);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initStaticSelects);
  else initStaticSelects();

  document.addEventListener('htmx:afterRequest', function (e) {
    var el = e.detail && e.detail.elt;
    if (!el || !el.dataset || !el.dataset.afterSuccess || !e.detail.successful) return;
    el.dataset.afterSuccess.split(/\s+/).forEach(function (step) {
      if (step === 'close-modal') call('closeModal');
      else if (step.indexOf('reload:') === 0 && window.htmx)
        htmx.ajax('GET', step.slice(7), { target: '#main-content', swap: 'innerHTML' });
    });
  });

  // ── Error toasts ──
  // htmx doesn't swap 4xx/5xx responses, so a failed save (a missing field,
  // no permission, the server unreachable) used to look like nothing happened.
  // Show the server's message as a toast instead; the form stays as it was.
  function toastBox() {
    var box = document.getElementById('toasts');
    if (!box) {
      box = document.createElement('div');
      box.id = 'toasts';
      box.setAttribute('aria-live', 'assertive');
      document.body.appendChild(box);
    }
    return box;
  }
  window.showToast = function (message, kind) {
    var t = document.createElement('div');
    t.setAttribute('role', kind === 'error' ? 'alert' : 'status');
    t.className = 'lm-toast ' + (kind === 'error' ? 'lm-toast-error' : 'lm-toast-info');   // styled in layout.html
    var text = document.createElement('span');
    text.style.flex = '1';
    text.textContent = message;
    var close = document.createElement('button');
    close.type = 'button';
    close.setAttribute('aria-label', 'Dismiss');
    close.textContent = '\u00d7';
    close.addEventListener('click', function () { t.remove(); });
    t.appendChild(text); t.appendChild(close);
    toastBox().appendChild(t);
    setTimeout(function () { t.remove(); }, kind === 'error' ? 8000 : 4000);
  };
  // The readable part of an error response: JSON {"error": ...}, the alert or
  // red message in an HTML fragment, or its text when short.
  function errorText(xhr) {
    var body = (xhr && xhr.responseText) || '';
    try { var j = JSON.parse(body); if (j && (j.error || j.message)) return String(j.error && j.error.message || j.error || j.message); } catch (_) {}
    if (body) {
      var d = document.createElement('div');
      d.innerHTML = body;
      var el = d.querySelector('[role=alert], .text-red-600, .text-red-700, .bg-red-50, .text-red-500');
      var t = ((el || d).textContent || '').replace(/\s+/g, ' ').trim();
      if (t && t.length <= 300) return t;
    }
    if (xhr && xhr.status === 401) return 'Your session has ended. Sign in again.';
    if (xhr && xhr.status === 403) return "You don't have permission to do that.";
    if (xhr && xhr.status === 404) return "That isn't there any more. Refresh the page.";
    if (xhr && xhr.status === 413) return 'That file is too big.';
    return "That didn't work (error " + (xhr ? xhr.status : '?') + '). Please try again.';
  }
  document.addEventListener('htmx:responseError', function (e) { window.showToast(errorText(e.detail && e.detail.xhr), 'error'); });
  document.addEventListener('htmx:sendError', function () { window.showToast("Couldn't reach the server. Check your connection and try again.", 'error'); });
  document.addEventListener('htmx:timeout', function () { window.showToast('The server took too long to answer. Please try again.', 'error'); });
})();
