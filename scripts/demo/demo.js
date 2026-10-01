/* LUG Manager static demo (GitHub Pages).
 *
 * The pages were exported from a running demo; this script makes them behave:
 *  - htmx GETs, XHR and fetch for app paths are mapped onto the exported files
 *    (<base>/<role>/_frag/<path>/), and pushed URLs onto the full pages;
 *  - anything that would change data (POST/PUT/DELETE, form submits) is
 *    blocked with a "changes aren't saved" notice;
 *  - a banner says it's a demo and switches between Member, Moderator and Admin.
 * Keep the URL encoding in sync with scripts/demo/export.py (rel_path/qenc).
 */
(function () {
  'use strict';
  var meta = document.querySelector('meta[name="lug-demo"]');
  var cfg = meta ? JSON.parse(meta.getAttribute('content')) : { base: '', role: 'admin' };
  var BASE = cfg.base, ROLE = cfg.role;
  var IGNORE_QUERY = ['/api/members/datatable', '/api/discord/forum-threads', '/api/chapter-options', '/api/member-options', '/api/discord/role-options',
                      '/api/discord/channel-options', '/audit/data', '/attendance/overview/data'];

  // Read-only endpoints the app calls with POST (DataTables); served from the export.
  var READ_POSTS = ['/api/members/datatable'];

  function qenc(q) {
    return q.replace(/%/g, '~').replace(/&/g, ',').replace(/\//g, '~2F').replace(/[?#]/g, '_');
  }
  function isApp(url) {
    return typeof url === 'string' && url.charAt(0) === '/' && url.indexOf(BASE + '/') !== 0 && url !== BASE;
  }
  function map(url, frag) {
    var a = document.createElement('a');
    a.href = url;
    var p = a.pathname, q = a.search ? a.search.slice(1) : '';
    if (/^\/(static|uploads|branding)\//.test(p)) return BASE + p + a.search;
    if (IGNORE_QUERY.indexOf(p) >= 0) q = '';
    var seg = p.replace(/^\/+|\/+$/g, '') || 'dashboard';
    return BASE + '/' + ROLE + '/' + (frag ? '_frag/' : '') + seg + (q ? '/__q/' + qenc(q) : '') + '/';
  }
  window.lugDemoMap = map;

  // ── Notice ──
  var toastTimer;
  function notice(msg) {
    var t = document.getElementById('lug-demo-toast');
    if (!t) {
      t = document.createElement('div');
      t.id = 'lug-demo-toast';
      t.setAttribute('role', 'status');
      t.style.cssText = 'position:fixed;left:50%;bottom:72px;transform:translateX(-50%);z-index:10001;' +
        'background:#111827;color:#fff;padding:10px 16px;border-radius:10px;font:14px system-ui,sans-serif;' +
        'box-shadow:0 6px 20px rgba(0,0,0,.3);max-width:90vw;text-align:center';
      document.body.appendChild(t);
    }
    t.textContent = msg || "This is a demo - changes aren't saved.";
    t.style.display = 'block';
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.style.display = 'none'; }, 3500);
  }

  // ── Network: map reads, block writes ──
  var origOpen = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function (method, url) {
    var args = Array.prototype.slice.call(arguments);
    var readPost = isApp(url) && READ_POSTS.indexOf(url.split('?')[0]) >= 0;
    if (readPost) { args[0] = 'GET'; args[1] = map(url, true); }
    else if (isApp(url)) {
      if (String(method).toUpperCase() === 'GET') args[1] = map(url, true);
      else { notice(); args[0] = 'GET'; args[1] = BASE + '/demo-blocked.json'; }
    }
    return origOpen.apply(this, args);
  };
  if (window.fetch) {
    var origFetch = window.fetch;
    window.fetch = function (input, init) {
      var url = typeof input === 'string' ? input : (input && input.url);
      var method = (init && init.method) || (input && input.method) || 'GET';
      if (isApp(url)) {
        if (String(method).toUpperCase() !== 'GET') { notice(); return Promise.resolve(new Response('{}', { status: 200 })); }
        return origFetch(map(url, true), init);
      }
      return origFetch(input, init);
    };
  }
  if (navigator.serviceWorker) navigator.serviceWorker.register = function () { return Promise.resolve(); };

  document.addEventListener('htmx:configRequest', function (e) {
    var d = e.detail;
    if (d.verb !== 'get') { e.preventDefault(); notice(); return; }
    if (isApp(d.path)) d.path = map(d.path, true);
    Object.keys(d.parameters || {}).forEach(function (k) { delete d.parameters[k]; });
  });
  // htmx pushes the partial's URL; show the full page's URL instead (so reload works).
  document.addEventListener('htmx:pushedIntoHistory', function (e) {
    var p = e.detail && e.detail.path;
    if (p && p.indexOf('/_frag/') >= 0) history.replaceState(history.state, '', p.replace('/_frag/', '/'));
  });
  document.addEventListener('htmx:responseError', function () { notice("That part isn't included in the demo."); });
  document.addEventListener('submit', function (e) {
    var f = e.target;
    if (f && (f.getAttribute('method') || 'get').toLowerCase() !== 'get' && !f.hasAttribute('hx-post')) {
      e.preventDefault(); notice();
    }
  }, true);

  // ── Banner with the role switcher ──
  function banner() {
    if (document.getElementById('lug-demo-banner')) return;
    var here = location.pathname.replace(new RegExp('^' + BASE + '/(member|moderator|admin)/'), '');
    var b = document.createElement('div');
    b.id = 'lug-demo-banner';
    b.style.cssText = 'position:fixed;left:0;right:0;bottom:0;z-index:10000;background:#facc15;color:#111827;' +
      'font:13px system-ui,sans-serif;padding:8px 12px;display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;' +
      'justify-content:center;box-shadow:0 -2px 10px rgba(0,0,0,.15)';
    var roles = [['member', 'Member'], ['moderator', 'Moderator'], ['admin', 'Admin']];
    var html = '<strong>LUG Manager demo</strong><span>Brickton LUG is fictional - nothing you do is saved.</span>' +
      '<span>View as:</span>';
    roles.forEach(function (r) {
      var on = r[0] === ROLE;
      html += '<a href="' + BASE + '/' + r[0] + '/' + here + '" style="padding:2px 10px;border-radius:999px;' +
        'text-decoration:none;font-weight:600;' + (on ? 'background:#111827;color:#facc15' : 'background:#fff;color:#111827') +
        '">' + r[1] + '</a>';
    });
    html += '<a href="https://github.com/ArkLUG/lug-manager" style="color:#111827">Get LUG Manager</a>';
    b.innerHTML = html;
    document.body.appendChild(b);
    document.body.style.paddingBottom = '48px';
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', banner); else banner();
})();
