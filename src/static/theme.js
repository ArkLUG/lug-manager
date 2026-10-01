// Light / dark theme. Loaded synchronously in <head> so the theme is applied
// before first paint (no white flash for dark-mode users).
//
// Preference: localStorage "lm-theme" = "light" | "dark"; absent = follow the
// OS (prefers-color-scheme), including live changes. Sets
// <html data-theme="light|dark">; the dark styles live in /static/theme.css.
(function () {
  var KEY = 'lm-theme';
  var media = window.matchMedia ? window.matchMedia('(prefers-color-scheme: dark)') : null;

  function stored() {
    try { var v = localStorage.getItem(KEY); return v === 'light' || v === 'dark' ? v : 'system'; }
    catch (e) { return 'system'; }
  }

  function apply() {
    var pref = stored();
    var dark = pref === 'dark' || (pref === 'system' && media && media.matches);
    document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
    document.documentElement.setAttribute('data-theme-pref', pref);
  }

  apply();
  if (media) {
    if (media.addEventListener) media.addEventListener('change', apply);
    else if (media.addListener) media.addListener(apply);
  }

  // Called by the Light / System / Dark switcher in the sidebar.
  window.setTheme = function (pref) {
    try {
      if (pref === 'light' || pref === 'dark') localStorage.setItem(KEY, pref);
      else localStorage.removeItem(KEY);
    } catch (e) {}
    apply();
  };
})();
