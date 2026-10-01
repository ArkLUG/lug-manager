// Live updates: when someone else changes something, the page reloads the parts
// that show it. The server (/live, a websocket) only says what changed, e.g.
// "event:12"; the page re-fetches itself through its normal routes.
//
// What refreshes:
//   - the page (#main-content), re-fetched from its URL, keeping the scroll
//     position and open <details>;
//   - the pop-up (#modal), re-fetched from the URL that filled it;
//   - page hooks: LiveSync.onChange(el, fn) - while `el` is on the page,
//     fn(changes) runs first and returns true when it handled them (the
//     members table reloads its rows this way, keeping its paging/search).
// A page or pop-up root may narrow what it listens to with
//   data-live="event:12 attendance member"   (a type alone matches every id)
// and data-live="off" turns it off. Default: any change.
//
// Never under someone's hands: if they're typing, have changed a field, or have
// opened/expanded things in place (a GET that didn't change the URL), the page
// isn't replaced - an "Updated - refresh" button appears instead.
(function () {
  'use strict';
  if (!document.body || document.body.dataset.live !== '1' || !('WebSocket' in window)) return;

  var tab = Math.random().toString(36).slice(2, 12) + Date.now().toString(36);
  var hooks = [];
  var pending = [];          // changes waiting to be applied (page hidden, or batching)
  var timer = null;
  var main = document.getElementById('main-content');
  var modal = document.getElementById('modal');
  var state = {
    main:  { customized: false, url: null },
    modal: { customized: false, url: null }
  };
  var LIVE_HEADER = 'X-Live-Refresh';

  window.LiveSync = {
    onChange: function (el, fn) { hooks.push({ el: el, fn: fn }); },
    tab: tab
  };

  // Our own requests say which tab they come from, so the server doesn't echo
  // our changes back to us.
  document.addEventListener('htmx:configRequest', function (e) {
    e.detail.headers['X-Live-Tab'] = tab;
  });

  function areaOf(el) {
    if (!el) return null;
    if (modal && modal.contains(el)) return 'modal';
    if (main && main.contains(el)) return 'main';
    return null;
  }

  // Track what a refetch would undo: GETs the user started that only change
  // part of the page (expanding a panel, paging, filtering) without a URL.
  document.addEventListener('htmx:beforeRequest', function (e) {
    var d = e.detail, cfg = d.requestConfig || {};
    if (cfg.headers && cfg.headers[LIVE_HEADER]) return;
    var target = d.target;
    if (target === modal && cfg.verb === 'get') {          // a new pop-up
      state.modal = { customized: false, url: d.pathInfo && d.pathInfo.requestPath };
      return;
    }
    if (target === main) return;                            // navigation
    if (cfg.verb !== 'get') return;                         // a saved change: a refetch shows it too
    var trig = cfg.triggeringEvent && cfg.triggeringEvent.type;
    if (!trig || trig === 'load' || trig === 'revealed' || trig === 'intersect') return;
    var src = d.elt;
    if (src && src.closest && src.closest('[hx-push-url="true"]')) return;
    var area = areaOf(target) || areaOf(src);
    if (area) state[area].customized = true;
  });
  document.addEventListener('htmx:afterSettle', function (e) {
    if (e.detail.target === main) {
      var refresh = e.detail.requestConfig && e.detail.requestConfig.headers && e.detail.requestConfig.headers[LIVE_HEADER];
      if (!refresh) state.main.customized = false;
      hidePill();
    }
  });

  // ── Matching ──
  function topicsOf(root) {
    var el = root && (root.querySelector('[data-live]') || (root.dataset && root.dataset.live ? root : null));
    var v = el ? el.getAttribute('data-live') : '';
    if (v === 'off') return [];
    return v ? v.split(/\s+/) : ['*'];
  }
  function matches(root, changes) {
    var topics = topicsOf(root);
    if (!topics.length) return false;
    if (topics.indexOf('*') >= 0 || changes === 'all') return true;
    return changes.some(function (c) {
      var type = c.split(':')[0];
      return topics.indexOf(c) >= 0 || topics.indexOf(type) >= 0;
    });
  }

  // ── Is someone working here? ──
  function edited(root) {
    // Fields of forms someone might be filling in (search boxes and table
    // controls outside forms re-run their own requests; libraries like
    // DataTables set values from script, which would look like edits).
    var fields = root.querySelectorAll('form input, form textarea, form select, textarea');
    for (var i = 0; i < fields.length; i++) {
      var f = fields[i];
      if (f.type === 'hidden' || f.type === 'submit' || f.type === 'button' || f.disabled) continue;
      if (f.closest('[data-live-ignore], .dataTables_wrapper')) continue;
      if (f.type === 'checkbox' || f.type === 'radio') { if (f.checked !== f.defaultChecked) return true; continue; }
      if (f.tagName === 'SELECT') {
        if (f.multiple) {
          for (var j = 0; j < f.options.length; j++)
            if (f.options[j].selected !== f.options[j].defaultSelected) return true;
        } else if (f.options.length) {
          var def = 0;
          for (var k = 0; k < f.options.length; k++) if (f.options[k].defaultSelected) { def = k; break; }
          if (f.selectedIndex !== def) return true;
        }
        continue;
      }
      if (f.value !== f.defaultValue) return true;
    }
    return false;
  }
  function busy(root, area) {
    var a = document.activeElement;
    if (a && root.contains(a) && (a.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(a.tagName))) return true;
    if (root.querySelector('[contenteditable="true"]')) return true;   // rich editors keep their own state
    if (root.querySelector('.htmx-request')) return true;
    return state[area].customized || edited(root);
  }

  // ── Refreshing ──
  function openDetails(root) {
    var out = [];
    root.querySelectorAll('details').forEach(function (d, i) { if (d.open) out.push(d.id ? '#' + d.id : i); });
    return out;
  }
  function restoreDetails(root, open) {
    var all = root.querySelectorAll('details');
    open.forEach(function (k) {
      var d = typeof k === 'string' ? root.querySelector(k) : all[k];
      if (d) d.open = true;
    });
  }
  function refetch(root, url) {
    var scroll = root.scrollTop, winScroll = window.scrollY, open = openDetails(root);
    var headers = {}; headers[LIVE_HEADER] = '1';
    var done = function () {
      restoreDetails(root, open);
      root.scrollTop = scroll;
      window.scrollTo(0, winScroll);
    };
    var p = htmx.ajax('GET', url, { target: root, swap: 'innerHTML', headers: headers });
    if (p && p.then) p.then(done);
  }

  var pill = null;
  function showPill() {
    if (pill) return;
    pill = document.createElement('button');
    pill.type = 'button';
    pill.id = 'live-pill';
    pill.className = 'fixed bottom-4 right-4 z-50 px-4 py-2 rounded-full shadow-lg bg-gray-900 text-white text-sm font-medium hover:bg-gray-700';
    pill.textContent = 'Updated – refresh';
    pill.setAttribute('aria-live', 'polite');
    pill.addEventListener('click', function () {
      state.main.customized = false;
      hidePill();
      refetch(main, location.pathname + location.search);
    });
    document.body.appendChild(pill);
  }
  function hidePill() { if (pill) { pill.remove(); pill = null; } }

  function apply(changes) {
    hooks = hooks.filter(function (h) { return h.el && h.el.isConnected; });
    for (var i = 0; i < hooks.length; i++) {
      try { if (hooks[i].fn(changes)) return; } catch (e) { console.error('live hook', e); }
    }
    var modalOpen = modal && modal.innerHTML.trim() && document.getElementById('modal-backdrop').classList.contains('open');
    if (modalOpen && state.modal.url && matches(modal, changes) && !busy(modal, 'modal'))
      refetch(modal, state.modal.url);
    if (main && matches(main, changes)) {
      if (modalOpen || busy(main, 'main')) showPill();
      else refetch(main, location.pathname + location.search);
    }
  }

  function queue(changes) {
    if (changes === 'all') pending = 'all';
    else if (pending !== 'all') pending = pending.concat(changes);
    if (document.hidden) return;                  // applied when the tab is shown again
    clearTimeout(timer);
    timer = setTimeout(function () {
      var c = pending; pending = [];
      if (c === 'all' || c.length) apply(c);
    }, 250);
  }
  document.addEventListener('visibilitychange', function () {
    if (!document.hidden && (pending === 'all' || pending.length)) queue([]);
  });

  // ── Connection ──
  var backoff = 1000, lost = false;
  function connect() {
    var ws;
    try {
      ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/live?tab=' + tab);
    } catch (e) { return retry(); }
    ws.onopen = function () {
      backoff = 1000;
      if (lost) { lost = false; queue('all'); }   // we may have missed changes while away
    };
    ws.onmessage = function (e) {
      var msg;
      try { msg = JSON.parse(e.data); } catch (err) { return; }
      if (msg.all) queue('all');
      else if (msg.changes && msg.changes.length) queue(msg.changes);
    };
    ws.onclose = function () { lost = true; retry(); };
  }
  function retry() {
    setTimeout(connect, backoff);
    backoff = Math.min(backoff * 2, 30000);
  }
  connect();
})();
