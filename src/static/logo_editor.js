// Settings > Branding: crop, size, position and background for the club logo
// before it's uploaded. Works on a 512x512 canvas; the result is uploaded as a
// PNG (field "logo"), with the picture it started from ("original") and the
// editor's settings ("editor_state") so it can be edited again later.
(function () {
  'use strict';
  var SIZE = 512;

  function init(root) {
    var ed = root && root.querySelector ? root.querySelector('[data-logo-editor]:not([data-logo-ready])') : null;
    if (!ed) return;
    ed.setAttribute('data-logo-ready', '1');
    var form = ed.closest('form');
    var fileIn = form.querySelector('[data-logo-file]');
    var origIn = form.querySelector('[data-logo-original]');
    var stateIn = form.querySelector('[data-logo-state]');
    var canvas = ed.querySelector('[data-logo-canvas]');
    var ctx = canvas.getContext('2d');
    var zoomIn = ed.querySelector('[data-logo-zoom]');
    var clear = ed.querySelector('[data-logo-transparent]');
    var color = ed.querySelector('[data-logo-color]');
    var hex = ed.querySelector('[data-logo-hex]');
    var asis = ed.querySelector('[data-logo-asis]');
    var previews = ed.querySelectorAll('[data-logo-preview]');
    var img = null, sourceFile = null, fit = 1, zoom = 1, ox = 0, oy = 0, timer = null;

    function bg() { return clear.checked ? '' : hex.value; }
    function draw() {
      ctx.clearRect(0, 0, SIZE, SIZE);
      if (bg()) { ctx.fillStyle = bg(); ctx.fillRect(0, 0, SIZE, SIZE); }
      if (img) {
        var w = img.naturalWidth * fit * zoom, h = img.naturalHeight * fit * zoom;
        ctx.drawImage(img, (SIZE - w) / 2 + ox, (SIZE - h) / 2 + oy, w, h);
      }
      previews.forEach(function (p) {
        var c = p.getContext('2d');
        c.clearRect(0, 0, p.width, p.height);
        c.drawImage(canvas, 0, 0, p.width, p.height);
      });
      clearTimeout(timer);
      timer = setTimeout(store, 150);
    }
    // Put the result into the form's file inputs (htmx sends them as they are)
    function store() {
      if (!sourceFile || !window.DataTransfer) return;
      if (asis && asis.checked) {
        var keep = new DataTransfer(); keep.items.add(sourceFile); fileIn.files = keep.files;
        origIn.files = new DataTransfer().files;
        stateIn.value = '';
        return;
      }
      canvas.toBlob(function (blob) {
        if (!blob) return;
        var out = new DataTransfer(); out.items.add(new File([blob], 'logo.png', { type: 'image/png' })); fileIn.files = out.files;
        var orig = new DataTransfer(); orig.items.add(sourceFile); origIn.files = orig.files;
        stateIn.value = JSON.stringify({ z: Math.round(zoom * 1000) / 1000, x: Math.round(ox), y: Math.round(oy), bg: bg() });
      }, 'image/png');
    }
    function setZoom(z) { zoom = Math.max(0.1, Math.min(4, z)); zoomIn.value = Math.round(zoom * 100); draw(); }
    function load(src, file, state) {
      var i = new Image();
      i.onload = function () {
        img = i; sourceFile = file;
        fit = Math.min(SIZE / i.naturalWidth, SIZE / i.naturalHeight);
        zoom = 1; ox = 0; oy = 0;
        if (state) {
          zoom = +state.z || 1; ox = +state.x || 0; oy = +state.y || 0;
          if (state.bg) { clear.checked = false; color.value = hex.value = state.bg; }
        }
        zoomIn.value = Math.round(zoom * 100);
        ed.classList.remove('hidden');
        draw();
      };
      i.src = src;
    }

    fileIn.addEventListener('change', function () {
      var f = fileIn.files && fileIn.files[0];
      if (!f) return;   // (setting .files from store() doesn't fire "change")
      if (asis) asis.checked = false;
      load(URL.createObjectURL(f), f, null);
    });
    ed.addEventListener('click', function (e) {
      var b = e.target.closest('[data-logo-do]');
      if (!b || !img) return;
      var what = b.getAttribute('data-logo-do');
      var fill = Math.max(SIZE / img.naturalWidth, SIZE / img.naturalHeight) / fit;
      if (what === 'fit') { ox = 0; oy = 0; setZoom(1); }
      else if (what === 'fill') { ox = 0; oy = 0; setZoom(fill); }
      else if (what === 'center') { ox = 0; oy = 0; draw(); }
    });
    zoomIn.addEventListener('input', function () { setZoom(zoomIn.value / 100); });
    clear.addEventListener('change', draw);
    if (asis) asis.addEventListener('change', store);
    color.addEventListener('input', function () { hex.value = color.value; clear.checked = false; draw(); });
    hex.addEventListener('input', function () {
      if (/^#[0-9a-fA-F]{6}$/.test(hex.value)) { color.value = hex.value; clear.checked = false; draw(); }
    });
    // Drag to move (mouse, pen or finger)
    var drag = null;
    canvas.addEventListener('pointerdown', function (e) { drag = { x: e.clientX, y: e.clientY }; canvas.setPointerCapture(e.pointerId); });
    canvas.addEventListener('pointermove', function (e) {
      if (!drag) return;
      var k = SIZE / canvas.getBoundingClientRect().width;
      ox += (e.clientX - drag.x) * k; oy += (e.clientY - drag.y) * k;
      drag = { x: e.clientX, y: e.clientY };
      draw();
    });
    canvas.addEventListener('pointerup', function () { drag = null; });
    canvas.addEventListener('keydown', function (e) {
      var step = e.shiftKey ? 20 : 4, moved = true;
      if (e.key === 'ArrowLeft') ox -= step; else if (e.key === 'ArrowRight') ox += step;
      else if (e.key === 'ArrowUp') oy -= step; else if (e.key === 'ArrowDown') oy += step;
      else moved = false;
      if (moved) { e.preventDefault(); draw(); }
    });
    // "Edit the current logo": start from the saved original and settings
    var again = form.querySelector('[data-logo-edit-current]');
    if (again) again.addEventListener('click', function () {
      var url = ed.getAttribute('data-original');
      fetch(url, { credentials: 'same-origin' }).then(function (r) { return r.ok ? r.blob() : null; }).then(function (b) {
        if (!b) return;
        var state = null;
        try { state = JSON.parse(ed.getAttribute('data-state') || 'null'); } catch (err) {}
        var ext = (b.type.split('/')[1] || 'png').replace('svg+xml', 'svg').replace('jpeg', 'jpg');
        load(URL.createObjectURL(b), new File([b], 'original.' + ext, { type: b.type }), state);
      });
    });
  }

  document.addEventListener('htmx:load', function (e) { init(e.detail && e.detail.elt); });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { init(document); });
  else init(document);
})();
