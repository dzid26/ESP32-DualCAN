import { mount } from 'svelte';
import App from './App.svelte';

// Service worker is production-only: in dev it caches the app shell and
// intercepts module requests, which leaves the page unstyled/broken when the
// dev server restarts or a module changes under it.
// The worker sits next to the app, so it is registered under the deployment
// base. On a sub-path install (GitHub Pages serves the app under the project
// name) the origin root belongs to something else, and '/sw.js' there is a
// 404, which leaves the app with no worker at all.
if (import.meta.env.PROD && 'serviceWorker' in navigator) {
  navigator.serviceWorker
    .register(`${import.meta.env.BASE_URL}sw.js`)
    .catch(() => {/* non-fatal */});
}

const app = mount(App, { target: document.getElementById('app')! });
export default app;
