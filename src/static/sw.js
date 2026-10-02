// Minimal service worker: makes the app installable and shows a friendly
// page when offline. It never caches app pages or API responses (they are
// per-user and must stay fresh) - only the offline fallback itself.
const OFFLINE = '/static/offline.html';
self.addEventListener('install', (e) => {
  e.waitUntil(caches.open('lm-v2').then((c) => c.add(OFFLINE)));
  self.skipWaiting();
});
self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));
self.addEventListener('fetch', (e) => {
  if (e.request.mode !== 'navigate') return;
  e.respondWith(fetch(e.request).catch(() => caches.match(OFFLINE)));
});
