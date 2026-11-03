import { test, expect } from '@playwright/test';

/**
 * The worker in `public/sw.js` saves what it fetches, so a start with no
 * connection has to work. This drives the built app in a browser:
 *
 *   1. load the page, the worker installs and takes the page over
 *   2. load it again, now the worker sees the requests and saves the bundles
 *   3. cut the network, drop the browser's own HTTP cache and load it again
 *
 * Step 2 is needed because the worker cannot see the requests of the very
 * first load. It does not exist yet when the page asks for its scripts. The
 * first launch from the home screen icon behaves like step 2, because the
 * worker was already installed during the visit that produced the icon.
 *
 * Step 3 drops the HTTP cache on purpose. Without that, the browser could
 * answer the reload from its own cache and the test would pass even if the
 * worker saved nothing.
 */
test('the app starts with the network cut', async ({ page, context }) => {
  // './' keeps the deployment base: the root install and the sub-path
  // install have to reach their own copy of the app.
  await page.goto('./');
  await expect(page.locator('#app')).not.toBeEmpty();
  await page.waitForFunction(() => navigator.serviceWorker.controller !== null);

  await page.reload();
  await expect(page.locator('#app')).not.toBeEmpty();

  await context.setOffline(true);

  const cdp = await context.newCDPSession(page);
  await cdp.send('Network.enable');
  await cdp.send('Network.clearBrowserCache');

  // Prove the network really is cut before trusting the reload below.
  const reachable = await page.evaluate(async () => {
    try {
      await fetch(`/not-cached-${Date.now()}`);
      return true;
    } catch {
      return false;
    }
  });
  expect(reachable, 'the page should not reach the network while offline').toBe(false);

  await page.reload();
  await expect(page.locator('#app')).not.toBeEmpty();
  await expect(page.locator('#app .shell')).toBeVisible();
});
