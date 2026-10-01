// Event wiring for the whole app, so templates need no inline event handlers
// (onclick=..., hx-on::...): with a nonce-based Content-Security-Policy the
// browser refuses to run those. Elements declare behaviour with data-*
// attributes instead; this file (served from 'self') does the rest.
//
//   data-action="..."        click behaviours (see ACTIONS below)
//   data-change="submit|bulk-action|palette"   (palette: switch the colour theme now)
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
  document.addEventListener('htmx:load', function (e) { initQr(e.detail && e.detail.elt); });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { initQr(document); });
  else initQr(document);

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
})();
