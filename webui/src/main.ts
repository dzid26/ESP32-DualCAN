import { mount } from 'svelte';
import App from './App.svelte';

// Service worker is production-only: in dev it caches the app shell and
// intercepts module requests, which leaves the page unstyled/broken when the
// dev server restarts or a module changes under it.
if (import.meta.env.PROD && 'serviceWorker' in navigator) {
  navigator.serviceWorker.register('/sw.js').catch(() => {/* non-fatal */});
}

const app = mount(App, { target: document.getElementById('app')! });
export default app;
