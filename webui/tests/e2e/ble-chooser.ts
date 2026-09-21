/**
 * ble-chooser.ts — reusable helpers for Playwright + Chrome DevTools Protocol
 * (CDP) tests that drive the native Web Bluetooth chooser
 * (`navigator.bluetooth.requestDevice()`).
 *
 * Real-device tier only. The Chrome chooser emits MANY
 * `DeviceAccess.deviceRequestPrompted` events that share the same prompt `id`;
 * each event appends newly discovered devices. `DevicePromptAccumulator` owns
 * the accumulation/dedup so callers do not re-implement it.
 *
 * Main helpers:
 *   - enableDeviceAccess / installRequestDeviceHarness / triggerRequestDevice:
 *     attach the CDP DeviceAccess domain, inject the page-side requestDevice()
 *     button, and click it.
 *   - BleChooser.waitForDevice / selectDevice / cancelPrompt: wait for a unique
 *     match and drive the native chooser via CDP (never guesses on ambiguity).
 *   - connectGatt / disconnectGatt / reconnectSavedDevice: GATT lifecycle plus
 *     the chooser-less silent-reconnect primitive (getDevices() + gatt.connect()).
 *
 * `playwright` is a type-only import (erased at runtime); the helpers accept the
 * caller's `Page`/`BrowserContext`, so no runtime discovery happens here.
 */

import type { BrowserContext, CDPSession, Page } from 'playwright';

/** Default time to wait for a matching device to be accumulated (ms). */
export const DEFAULT_SCAN_TIMEOUT_MS = 30_000;
/** Default time to wait for the chooser prompt event to fire (ms). */
export const DEFAULT_PROMPT_TIMEOUT_MS = 15_000;
/** Default time to poll between checks (ms). */
export const DEFAULT_POLL_INTERVAL_MS = 200;
/** Default bound for `device.gatt.connect()` (ms). */
export const DEFAULT_GATT_TIMEOUT_MS = 15_000;
/** Default id of the injected page-side request button. */
export const DEFAULT_BUTTON_ID = 'ble-go';
/** Default name of the injected page-side state global. */
export const DEFAULT_GLOBAL_NAME = '__ble';

/** Machine-readable error codes. */
export const BleChooserErrorCode = Object.freeze({
  NOT_FOUND: 'NOT_FOUND',
  AMBIGUOUS: 'AMBIGUOUS',
  MATCHER_INVALID: 'MATCHER_INVALID',
  TIMEOUT: 'TIMEOUT',
  STATE: 'STATE',
} as const);
export type BleChooserErrorCode = (typeof BleChooserErrorCode)[keyof typeof BleChooserErrorCode];

/** A device as reported by the CDP `DeviceAccess` chooser prompt. */
export interface BleDevice {
  /** Real CDP device id — the peripheral BLE MAC address. Not the page's opaque handle. */
  id: string;
  /** Advertised name as reported by the chooser (may be `""`). */
  name: string;
  /** CDP prompt id under which it was first/seen latest. */
  promptId: string;
}

export type DeviceMatcherObject =
  | { exact: string }
  | { prefix: string }
  | { regex: RegExp }
  | { predicate: (device: BleDevice) => boolean }
  | { id: string };

export type DeviceMatcher = string | RegExp | ((device: BleDevice) => boolean) | DeviceMatcherObject;

export interface NormalizedMatcher {
  description: string;
  test: (device: BleDevice) => boolean;
}

export interface HarnessError { name: string; message: string }
export interface HarnessResult { name: string; id: string }

export interface HarnessState {
  started: boolean;
  device: BluetoothDevice | null;
  resolved: HarnessResult | null;
  error: HarnessError | null;
  gattConnected: boolean | null;
  gattError: HarnessError | null;
}

export interface HarnessSnapshot {
  started: boolean;
  device: boolean;
  resolved: HarnessResult | null;
  error: HarnessError | null;
  gattConnected: boolean | null;
  gattError: HarnessError | null;
}

export interface GattConnectResult { connected: boolean; error: HarnessError | null }

export interface BleChooserErrorDetails {
  candidates?: BleDevice[];
  matches?: BleDevice[];
  matcher?: string;
  timeoutMs?: number;
  error?: HarnessError;
}

export class BleChooserError extends Error {
  readonly code: BleChooserErrorCode;
  readonly candidates?: BleDevice[];
  readonly matches?: BleDevice[];
  readonly matcher?: string;
  readonly timeoutMs?: number;
  readonly error?: HarnessError;

  constructor(code: BleChooserErrorCode, message: string, details: BleChooserErrorDetails = {}) {
    super(message);
    this.name = 'BleChooserError';
    this.code = code;
    if (details.candidates !== undefined) this.candidates = details.candidates;
    if (details.matches !== undefined) this.matches = details.matches;
    if (details.matcher !== undefined) this.matcher = details.matcher;
    if (details.timeoutMs !== undefined) this.timeoutMs = details.timeoutMs;
    if (details.error !== undefined) this.error = details.error;
  }
}

export interface WaitOptions { timeoutMs?: number; pollIntervalMs?: number }
export interface GlobalNameOptions { globalName?: string }
export interface HarnessOptions extends GlobalNameOptions { buttonId?: string }

export interface DevicePromptEvent {
  id: string;
  devices?: Array<{ id: string; name?: string }>;
}

const delay = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms));

function normalizeName(value: unknown): string {
  return String(value == null ? '' : value).trim().toLowerCase();
}

/** Normalize the supported matcher forms into a predicate over {@link BleDevice}. */
export function normalizeMatcher(matcher: DeviceMatcher): NormalizedMatcher {
  if (typeof matcher === 'string') {
    return {
      description: `name === ${JSON.stringify(matcher)} (trimmed, case-insensitive)`,
      test: (d) => normalizeName(d.name) === normalizeName(matcher),
    };
  }
  if (matcher instanceof RegExp) {
    return { description: `name matches ${String(matcher)}`, test: (d) => matcher.test(String(d.name ?? '')) };
  }
  if (typeof matcher === 'function') {
    return { description: 'predicate(device)', test: (d) => !!matcher(d) };
  }
  if (matcher && typeof matcher === 'object') {
    if ('exact' in matcher && typeof matcher.exact === 'string') {
      const exact = matcher.exact;
      return {
        description: `name === ${JSON.stringify(exact)} (trimmed, case-insensitive)`,
        test: (d) => normalizeName(d.name) === normalizeName(exact),
      };
    }
    if ('prefix' in matcher && typeof matcher.prefix === 'string') {
      const prefix = matcher.prefix;
      return {
        description: `name startsWith ${JSON.stringify(prefix)} (trimmed, case-insensitive)`,
        test: (d) => normalizeName(d.name).startsWith(normalizeName(prefix)),
      };
    }
    if ('regex' in matcher && matcher.regex instanceof RegExp) {
      const regex = matcher.regex;
      return { description: `name matches ${String(regex)}`, test: (d) => regex.test(String(d.name ?? '')) };
    }
    if ('predicate' in matcher && typeof matcher.predicate === 'function') {
      const predicate = matcher.predicate;
      return { description: 'predicate(device)', test: (d) => !!predicate(d) };
    }
    if ('id' in matcher && typeof matcher.id === 'string') {
      const id = matcher.id;
      return { description: `id === ${JSON.stringify(id)}`, test: (d) => String(d.id) === id };
    }
  }
  throw new BleChooserError(
    BleChooserErrorCode.MATCHER_INVALID,
    'Unsupported device matcher. Use a string, RegExp, predicate function, or one of ' +
      '{ exact, prefix, regex, predicate, id }.',
  );
}

/**
 * Accumulates devices across the repeated `DeviceAccess.deviceRequestPrompted`
 * events Chrome emits for a single chooser interaction. Devices are deduped by
 * `device.id` per prompt; a later non-empty name overwrites an earlier empty one.
 */
export class DevicePromptAccumulator {
  readonly byPrompt = new Map<string, Map<string, BleDevice>>();
  promptOrder: string[] = [];
  lastPromptId: string | null = null;

  ingest(event: DevicePromptEvent): number {
    const promptId = event.id;
    this.lastPromptId = promptId;
    let devices = this.byPrompt.get(promptId);
    if (!devices) {
      devices = new Map();
      this.byPrompt.set(promptId, devices);
      this.promptOrder.push(promptId);
    }
    for (const raw of event.devices ?? []) {
      const prev = devices.get(raw.id);
      const hasName = raw.name != null && String(raw.name).length > 0;
      const name = hasName ? String(raw.name) : prev ? prev.name : '';
      devices.set(raw.id, { id: raw.id, name, promptId });
    }
    return devices.size;
  }

  get devices(): BleDevice[] {
    const out = new Map<string, BleDevice>();
    for (const promptId of this.promptOrder) {
      const devices = this.byPrompt.get(promptId);
      if (!devices) continue;
      for (const [deviceId, device] of devices) out.set(deviceId, device);
    }
    return [...out.values()];
  }

  findById(deviceId: string): BleDevice | undefined {
    const all = this.devices;
    for (let i = all.length - 1; i >= 0; i -= 1) {
      const device = all[i];
      if (device && device.id === deviceId) return device;
    }
    return undefined;
  }

  clear(): void {
    this.byPrompt.clear();
    this.promptOrder = [];
    this.lastPromptId = null;
  }
}

/** A DeviceAccess-backed chooser controller for one {@link Page}. */
export class BleChooser {
  readonly cdp: CDPSession;
  readonly page: Page;
  readonly accumulator: DevicePromptAccumulator;
  enabled = false;

  constructor(cdp: CDPSession, page: Page) {
    this.cdp = cdp;
    this.page = page;
    this.accumulator = new DevicePromptAccumulator();
  }

  /** Attach the accumulation listener and enable the `DeviceAccess` domain. */
  async enable(): Promise<this> {
    if (this.enabled) return this;
    this.cdp.on('DeviceAccess.deviceRequestPrompted', (event) => {
      this.accumulator.ingest(event as DevicePromptEvent);
    });
    await this.cdp.send('DeviceAccess.enable');
    this.enabled = true;
    return this;
  }

  get devices(): BleDevice[] { return this.accumulator.devices; }

  /** Wait until at least one chooser prompt has been observed. */
  async waitForPrompt(options: WaitOptions = {}): Promise<string> {
    const timeoutMs = options.timeoutMs ?? DEFAULT_PROMPT_TIMEOUT_MS;
    const pollIntervalMs = options.pollIntervalMs ?? DEFAULT_POLL_INTERVAL_MS;
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      if (this.accumulator.lastPromptId) return this.accumulator.lastPromptId;
      if (Date.now() >= deadline) {
        throw new BleChooserError(
          BleChooserErrorCode.TIMEOUT,
          `No DeviceAccess.deviceRequestPrompted event within ${timeoutMs}ms. ` +
            'Check that the browser is headful and the page triggered navigator.bluetooth.requestDevice().',
          { timeoutMs },
        );
      }
      await delay(pollIntervalMs);
    }
  }

  /**
   * Wait for exactly one accumulated device to match.
   * Throws `AMBIGUOUS` immediately when two or more match (never guesses), and
   * `NOT_FOUND` on timeout (listing every candidate seen).
   */
  async waitForDevice(matcher: DeviceMatcher, options: WaitOptions = {}): Promise<BleDevice> {
    const spec = normalizeMatcher(matcher);
    const timeoutMs = options.timeoutMs ?? DEFAULT_SCAN_TIMEOUT_MS;
    const pollIntervalMs = options.pollIntervalMs ?? DEFAULT_POLL_INTERVAL_MS;
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const candidates = this.accumulator.devices;
      const matches = candidates.filter(spec.test);
      const [only] = matches;
      if (matches.length === 1 && only) return only;
      if (matches.length > 1) {
        throw new BleChooserError(
          BleChooserErrorCode.AMBIGUOUS,
          `${matches.length} devices match ${spec.description}: ${JSON.stringify(matches)}. Refusing to guess.`,
          { matches, candidates, matcher: spec.description },
        );
      }
      if (Date.now() >= deadline) {
        throw new BleChooserError(
          BleChooserErrorCode.NOT_FOUND,
          `No device matched ${spec.description} within ${timeoutMs}ms. Candidates seen: ${JSON.stringify(candidates)}.`,
          { candidates, matcher: spec.description, timeoutMs },
        );
      }
      await delay(pollIntervalMs);
    }
  }

  /** Select a previously discovered device in the native chooser via CDP. */
  async selectDevice(device: BleDevice | string, options: { promptId?: string } = {}): Promise<{ promptId: string; deviceId: string }> {
    const isObject = typeof device === 'object' && device !== null;
    const deviceId = isObject ? device.id : device;
    const promptId =
      options.promptId ??
      (isObject ? device.promptId : undefined) ??
      this.accumulator.findById(deviceId)?.promptId ??
      this.accumulator.lastPromptId ??
      undefined;
    if (!promptId) {
      throw new BleChooserError(BleChooserErrorCode.STATE, 'selectDevice(): no prompt id available.');
    }
    await this.cdp.send('DeviceAccess.selectPrompt', { id: promptId, deviceId });
    return { promptId, deviceId };
  }

  /** Cancel/dismiss the chooser prompt via CDP. */
  async cancelPrompt(promptId?: string): Promise<boolean> {
    const id = promptId ?? this.accumulator.lastPromptId;
    if (!id) return false;
    await this.cdp.send('DeviceAccess.cancelPrompt', { id });
    return true;
  }
}

/**
 * Create a CDP session for `page`, attach the device accumulator, and enable the
 * `DeviceAccess` domain. Must be called BEFORE the page triggers
 * `navigator.bluetooth.requestDevice()`.
 */
export async function enableDeviceAccess(page: Page): Promise<BleChooser> {
  const cdp = await page.context().newCDPSession(page);
  const chooser = new BleChooser(cdp, page);
  await chooser.enable();
  return chooser;
}

interface InstallHarnessArgs {
  requestOptions: RequestDeviceOptions;
  buttonId: string;
  globalName: string;
}

/**
 * Inject a page-side harness that calls `navigator.bluetooth.requestDevice()`
 * when its button is clicked, stashing the outcome in a page global.
 */
export async function installRequestDeviceHarness(
  page: Page,
  requestOptions: RequestDeviceOptions,
  options: HarnessOptions = {},
): Promise<void> {
  const buttonId = options.buttonId ?? DEFAULT_BUTTON_ID;
  const globalName = options.globalName ?? DEFAULT_GLOBAL_NAME;
  await page.evaluate<void, InstallHarnessArgs>(
    ({ requestOptions: ro, buttonId: bid, globalName: g }) => {
      const globalRef = window as unknown as Record<string, HarnessState>;
      globalRef[g] = {
        started: false,
        device: null,
        resolved: null,
        error: null,
        gattConnected: null,
        gattError: null,
      };
      document.getElementById(bid)?.remove();
      const button = document.createElement('button');
      button.id = bid;
      button.textContent = 'requestDevice';
      button.style.cssText = 'font-size:40px;padding:20px';
      button.addEventListener('click', async () => {
        const state = globalRef[g];
        if (!state) return;
        state.started = true;
        try {
          const device = await navigator.bluetooth.requestDevice(ro);
          state.device = device;
          state.resolved = { name: device.name ?? '', id: device.id };
          document.title = `OK:${device.name ?? ''}:${device.id}`;
        } catch (error) {
          const detail = error instanceof Error
            ? { name: error.name, message: error.message }
            : { name: 'Error', message: String(error) };
          state.error = detail;
          document.title = `ERR:${detail.name}:${detail.message}`;
        }
      });
      document.body.appendChild(button);
    },
    { requestOptions, buttonId, globalName },
  );
}

/** Click the button injected by {@link installRequestDeviceHarness}. */
export async function triggerRequestDevice(page: Page, options: HarnessOptions = {}): Promise<void> {
  await page.click(`#${options.buttonId ?? DEFAULT_BUTTON_ID}`);
}

/** Structured-clone-safe snapshot of the page-side harness state. */
export async function readHarnessState(page: Page, options: GlobalNameOptions = {}): Promise<HarnessSnapshot> {
  const globalName = options.globalName ?? DEFAULT_GLOBAL_NAME;
  return await page.evaluate<HarnessSnapshot, string>((g) => {
    const state = (window as unknown as Record<string, HarnessState | undefined>)[g];
    if (!state) {
      return { started: false, device: false, resolved: null, error: null, gattConnected: null, gattError: null };
    }
    return {
      started: !!state.started,
      device: !!state.device,
      resolved: state.resolved ? { name: state.resolved.name, id: state.resolved.id } : null,
      error: state.error ? { name: state.error.name, message: state.error.message } : null,
      gattConnected: state.gattConnected,
      gattError: state.gattError ? { name: state.gattError.name, message: state.gattError.message } : null,
    };
  }, globalName);
}

/** Wait until `requestDevice()` has resolved or rejected in the page. */
export async function waitForRequestDeviceResult(
  page: Page,
  options: WaitOptions & GlobalNameOptions = {},
): Promise<HarnessResult> {
  const timeoutMs = options.timeoutMs ?? DEFAULT_SCAN_TIMEOUT_MS;
  const pollIntervalMs = options.pollIntervalMs ?? DEFAULT_POLL_INTERVAL_MS;
  const globalName = options.globalName ?? DEFAULT_GLOBAL_NAME;
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const state = await readHarnessState(page, { globalName });
    if (state.resolved) return state.resolved;
    if (state.error) {
      throw new BleChooserError(
        BleChooserErrorCode.STATE,
        `requestDevice() rejected: ${state.error.name}: ${state.error.message}`,
        { error: state.error },
      );
    }
    if (Date.now() >= deadline) {
      throw new BleChooserError(BleChooserErrorCode.TIMEOUT, `requestDevice() did not settle within ${timeoutMs}ms.`, { timeoutMs });
    }
    await delay(pollIntervalMs);
  }
}

interface ConnectGattArgs { g: string; timeoutMs: number }

/** Connect GATT for the device resolved in the page-side harness. Never throws on GATT failure. */
export async function connectGatt(page: Page, options: WaitOptions & GlobalNameOptions = {}): Promise<GattConnectResult> {
  const globalName = options.globalName ?? DEFAULT_GLOBAL_NAME;
  const timeoutMs = options.timeoutMs ?? DEFAULT_GATT_TIMEOUT_MS;
  return await page.evaluate<GattConnectResult, ConnectGattArgs>(
    async ({ g, timeoutMs: limit }) => {
      const state = (window as unknown as Record<string, HarnessState | undefined>)[g];
      if (!state || !state.device || !state.device.gatt) {
        return { connected: false, error: { name: 'StateError', message: 'No resolved device; select a device first.' } };
      }
      const gatt = state.device.gatt;
      try {
        const server = await Promise.race([
          gatt.connect(),
          new Promise<never>((_resolve, reject) =>
            setTimeout(() => reject(Object.assign(new Error(`gatt.connect() did not settle within ${limit}ms`), { name: 'TimeoutError' })), limit),
          ),
        ]);
        state.gattConnected = !!server.connected;
        return { connected: !!server.connected, error: null };
      } catch (error) {
        const detail = error instanceof Error
          ? { name: error.name, message: error.message }
          : { name: 'Error', message: String(error) };
        state.gattError = detail;
        return { connected: false, error: detail };
      }
    },
    { g: globalName, timeoutMs },
  );
}

/** Disconnect GATT for the device resolved in the page-side harness. */
export async function disconnectGatt(page: Page, options: GlobalNameOptions = {}): Promise<boolean> {
  const globalName = options.globalName ?? DEFAULT_GLOBAL_NAME;
  return await page.evaluate<boolean, string>((g) => {
    const state = (window as unknown as Record<string, HarnessState | undefined>)[g];
    if (!state || !state.device || !state.device.gatt) return false;
    try {
      state.device.gatt.disconnect();
      state.gattConnected = false;
      return true;
    } catch {
      return false;
    }
  }, globalName);
}

/** Outcome of {@link reconnectSavedDevice}. */
export interface SavedReconnectResult {
  /** `navigator.bluetooth.getDevices()` is available. */
  supported: boolean;
  /** A previously-permitted device matched the prefix. */
  found: boolean;
  /** `device.gatt.connect()` returned a connected server. */
  connected: boolean;
  /** Advertised name of the matched device. */
  name: string | null;
  /** Names of every previously-permitted device (for diagnostics). */
  candidates: string[];
}

interface ReconnectArgs { prefix: string; timeoutMs: number }

/**
 * Re-establish GATT with a previously-permitted device WITHOUT opening the
 * chooser — the same mechanism the app's silent auto-reconnect relies on
 * (`navigator.bluetooth.getDevices()` + `device.gatt.connect()`).
 */
export async function reconnectSavedDevice(page: Page, prefix: string, timeoutMs = DEFAULT_GATT_TIMEOUT_MS): Promise<SavedReconnectResult> {
  return await page.evaluate<SavedReconnectResult, ReconnectArgs>(async ({ prefix: namePrefix, timeoutMs: limit }) => {
    const result: SavedReconnectResult = { supported: false, found: false, connected: false, name: null, candidates: [] };
    const bt = (navigator as unknown as { bluetooth?: { getDevices?: () => Promise<BluetoothDevice[]> } }).bluetooth;
    if (!bt || typeof bt.getDevices !== 'function') return result;
    result.supported = true;
    const devices = await bt.getDevices();
    result.candidates = devices.map((d) => d.name ?? '');
    const wanted = String(namePrefix).trim().toLowerCase();
    const match = devices.find((d) => String(d.name ?? '').trim().toLowerCase().startsWith(wanted));
    if (!match || !match.gatt) return result;
    result.found = true;
    result.name = match.name ?? null;
    const server = await Promise.race([
      match.gatt.connect(),
      new Promise<never>((_resolve, reject) =>
        setTimeout(() => reject(Object.assign(new Error(`gatt.connect() did not settle within ${limit}ms`), { name: 'TimeoutError' })), limit),
      ),
    ]);
    result.connected = !!server.connected;
    return result;
  }, { prefix, timeoutMs });
}

/** Convenience export so callers can type a `BrowserContext` without importing playwright. */
export type { BrowserContext, CDPSession, Page };
