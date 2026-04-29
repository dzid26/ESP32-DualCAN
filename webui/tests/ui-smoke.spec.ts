import { test, expect, type Page } from '@playwright/test';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Inject a mock BLE transport so the app thinks it's connected without
 *  needing real hardware. The mock intercepts window.navigator.bluetooth and
 *  provides a simple loopback for Protocol calls. */
async function mockConnected(page: Page, actionNames: string[] = []) {
  await page.addInitScript((actions: string[]) => {
    // Encode a JSON-framed response
    function frame(obj: object): Uint8Array {
      const json = JSON.stringify(obj);
      const encoded = new TextEncoder().encode(json);
      const buf = new Uint8Array(4 + encoded.length);
      const len = encoded.length;
      buf[0] = len & 0xff; buf[1] = (len >> 8) & 0xff;
      buf[2] = (len >> 16) & 0xff; buf[3] = (len >> 24) & 0xff;
      buf.set(encoded, 4);
      return buf;
    }

    // Simple request parser for the framed protocol
    function handleRequest(data: Uint8Array, notify: (d: Uint8Array) => void) {
      let off = 0;
      while (off + 4 <= data.length) {
        const len = data[off] | (data[off+1]<<8) | (data[off+2]<<16) | (data[off+3]<<24);
        if (off + 4 + len > data.length) break;
        const json = new TextDecoder().decode(data.subarray(off + 4, off + 4 + len));
        off += 4 + len;
        try {
          const req = JSON.parse(json);
          let result: object | null = null;
          if (req.op === 'ping') result = 'pong';
          else if (req.op === 'script.list') result = { scripts: [] };
          else if (req.op === 'action.list') result = { actions };
          else if (req.op === 'action.invoke') result = null;
          notify(frame({ id: req.id, ok: true, result }));
        } catch {}
      }
    }

    // Stub navigator.bluetooth so BleTransport.connect() succeeds
    let _notifyCb: ((d: Uint8Array) => void) | null = null;
    const fakeChar = {
      writeValueWithoutResponse: (data: ArrayBuffer) => {
        if (_notifyCb) handleRequest(new Uint8Array(data), _notifyCb);
        return Promise.resolve();
      },
      startNotifications: () => Promise.resolve(fakeChar),
      addEventListener: (_: string, cb: EventListenerOrEventListenerObject) => {
        // store notification callback; fire connected event
        _notifyCb = (d: Uint8Array) => {
          // Event.target is read-only on real Event objects, so use a plain
          // object instead — BleTransport only accesses event.target.value.
          const ev = { target: { value: new DataView(d.buffer) } };
          if (typeof cb === 'function') cb(ev as any);
          else (cb as EventListenerObject).handleEvent(ev as any);
        };
      },
    };
    const fakeService = {
      getCharacteristic: () => Promise.resolve(fakeChar),
    };
    const fakeServer = {
      connected: true,
      getPrimaryService: () => Promise.resolve(fakeService),
      disconnect: () => {},
    };
    const fakeDevice = {
      name: 'Dorky-Test',
      gatt: { connect: () => Promise.resolve(fakeServer) },
      addEventListener: () => {},
    };
    (window.navigator as any).bluetooth = {
      requestDevice: () => Promise.resolve(fakeDevice),
    };
  }, actionNames);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test.describe('page structure', () => {
  test('loads and shows nav', async ({ page }) => {
    await page.goto('/');
    await expect(page.getByText('Dorky Commander')).toBeVisible();
    await expect(page.getByRole('button', { name: 'Scripts' })).toBeVisible();
    await expect(page.getByRole('button', { name: 'DBC' })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Dashboard' })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Connect BLE' })).toBeVisible();
  });

  test('Scripts view is default', async ({ page }) => {
    await page.goto('/');
    await expect(page.getByRole('heading', { name: 'Scripts' })).toBeVisible();
    await expect(page.locator('textarea')).toBeVisible();
    await expect(page.getByRole('button', { name: 'Upload' })).toBeVisible();
  });

  test('Upload button disabled when not connected', async ({ page }) => {
    await page.goto('/');
    await expect(page.getByRole('button', { name: 'Upload' })).toBeDisabled();
  });

  test('Dashboard view shows connect prompt when not connected', async ({ page }) => {
    await page.goto('/');
    await page.getByRole('button', { name: 'Dashboard' }).click();
    await expect(page.getByText('Connect to the device via BLE')).toBeVisible();
  });

  test('DBC view is reachable', async ({ page }) => {
    await page.goto('/');
    await page.getByRole('button', { name: 'DBC' }).click();
    await expect(page.getByRole('heading', { name: 'DBC' })).toBeVisible();
  });
});

test.describe('mock BLE connect', () => {
  test('Connect BLE button transitions to connected state', async ({ page }) => {
    await mockConnected(page);
    await page.goto('/');
    await page.getByRole('button', { name: 'Connect BLE' }).click();
    await expect(page.getByRole('button', { name: 'Connected' })).toBeVisible({ timeout: 5000 });
  });

  test('Upload button enabled after connect', async ({ page }) => {
    await mockConnected(page);
    await page.goto('/');
    await page.getByRole('button', { name: 'Connect BLE' }).click();
    await expect(page.getByRole('button', { name: 'Connected' })).toBeVisible({ timeout: 5000 });
    await expect(page.getByRole('button', { name: 'Upload' })).toBeEnabled();
  });

  test('Dashboard shows action tiles when actions exist', async ({ page }) => {
    await mockConnected(page, ['blip_red', 'blip_green', 'blip_blue']);
    await page.goto('/');
    await page.getByRole('button', { name: 'Connect BLE' }).click();
    await expect(page.getByRole('button', { name: 'Connected' })).toBeVisible({ timeout: 5000 });
    await page.getByRole('button', { name: 'Dashboard' }).click();
    await expect(page.getByRole('button', { name: 'blip_red' })).toBeVisible({ timeout: 3000 });
    await expect(page.getByRole('button', { name: 'blip_green' })).toBeVisible();
    await expect(page.getByRole('button', { name: 'blip_blue' })).toBeVisible();
  });

  test('Dashboard shows empty-actions hint when none registered', async ({ page }) => {
    await mockConnected(page, []);
    await page.goto('/');
    await page.getByRole('button', { name: 'Connect BLE' }).click();
    await expect(page.getByRole('button', { name: 'Connected' })).toBeVisible({ timeout: 5000 });
    await page.getByRole('button', { name: 'Dashboard' }).click();
    await expect(page.getByText('action_register')).toBeVisible({ timeout: 3000 });
  });

  test('Action tile invoke calls action.invoke op', async ({ page }) => {
    await mockConnected(page, ['short_honk']);
    await page.goto('/');
    await page.getByRole('button', { name: 'Connect BLE' }).click();
    await expect(page.getByRole('button', { name: 'Connected' })).toBeVisible({ timeout: 5000 });
    await page.getByRole('button', { name: 'Dashboard' }).click();
    await expect(page.getByRole('button', { name: 'short_honk' })).toBeVisible({ timeout: 3000 });
    // Click should not throw (mock responds with ok:true)
    await page.getByRole('button', { name: 'short_honk' }).click();
    // Tile should not show an error
    await expect(page.locator('.err')).not.toBeVisible();
  });
});
