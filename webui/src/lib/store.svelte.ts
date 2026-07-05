// Global app state — Svelte 5 runes shared across components.
// Wires the real BleTransport + Protocol from ../transport/* through to the
// status strip, banners, and views. Per-view local state stays in the views.

import { BleTransport } from '../transport/ble';
import { Protocol } from '../transport/protocol';
import type { BusStatus, LogLevel } from '../transport/protocol';

const LOG_LEVELS: readonly LogLevel[] = ['none', 'error', 'warn', 'info', 'debug', 'verbose'];
function loadLogLevel(): LogLevel {
  try {
    const v = localStorage.getItem('dc-log-level');
    if (v && (LOG_LEVELS as readonly string[]).includes(v)) return v as LogLevel;
  } catch { /* ignore */ }
  return 'info';
}
function loadLogHeight(): number {
  try {
    const v = localStorage.getItem('dc-log-height');
    if (v) { const n = parseInt(v, 10); if (n >= 80 && n <= 800) return n; }
  } catch { /* ignore */ }
  return 180;
}
import type { LogLine } from './sampleData';
import { type Car, loadCar, saveCar } from './cars';
import { toast } from './toast.svelte';

// Bump in lockstep with firmware proto_version when ops change.
const UI_PROTO_VERSION = 6;

interface BeforeInstallPromptEvent extends Event {
  prompt(): Promise<void>;
  readonly userChoice: Promise<{ outcome: 'accepted' | 'dismissed'; platform: string }>;
}

export type Transport = 'ble' | 'ws';

export type ViewId =
  | 'events' | 'scripts' | 'ai' | 'gallery'
  | 'signals' | 'dbc'
  | 'trace' | 'capture'
  | 'settings' | 'tesla'
  | 'engine' | 'radio';

const VALID_VIEWS = new Set<string>([
  'events','scripts','ai','gallery','signals','dbc','trace','capture','settings','tesla','engine','radio'
]);

function loadView(): ViewId {
  try {
    const hash = location.hash.slice(1);
    if (hash && VALID_VIEWS.has(hash)) return hash as ViewId;
    const v = localStorage.getItem('dc-view');
    if (v === 'dashboard') return 'events'; // renamed
    if (v && VALID_VIEWS.has(v)) return v as ViewId;
    return 'events';
  } catch { return 'events'; }
}

function saveView(v: ViewId): void {
  try {
    localStorage.setItem('dc-view', v);
    if (location.hash !== '#' + v) history.replaceState(null, '', '#' + v);
  } catch { /* ignore */ }
}

function loadLastScriptFilename(): string | null {
  try { return localStorage.getItem('dc-last-script'); } catch { return null; }
}
function saveLastScriptFilename(fn: string | null): void {
  try {
    if (fn) localStorage.setItem('dc-last-script', fn);
    else localStorage.removeItem('dc-last-script');
  } catch { /* ignore */ }
}

function nowTs(): string {
  const d = new Date();
  const p2 = (n: number) => String(n).padStart(2, '0');
  const p3 = (n: number) => String(n).padStart(3, '0');
  return `${p2(d.getHours())}:${p2(d.getMinutes())}:${p2(d.getSeconds())}.${p3(d.getMilliseconds())}`;
}

class AppState {
  // Real transport + protocol — singletons for the whole app.
  readonly ble = new BleTransport();
  readonly proto = new Protocol(this.ble);

  // ---- Reactive UI state ----
  view = $state<ViewId>(loadView());

  /** Persisted last-opened script filename so ScriptsView restores it across tab switches. */
  lastScriptFilename = $state<string | null>(loadLastScriptFilename());

  connected = $state(false);
  connecting = $state(false);
  /** Surfaced as the transport pip subtitle. */
  transport: Transport = 'ble';
  connectError = $state<string | null>(null);

  fwVersion = $state<string | null>(null);
  protoMismatch = $state<string | null>(null);
  /** Firmware-declared scripting API version (undefined = firmware too old to report it). */
  scriptingApiVersion = $state<number | undefined>(undefined);
  /** Advertised BLE name of the currently-connected device (e.g. "Dorky-A3F1"). */
  deviceName = $state<string | null>(null);

  /** Mirrors the device's sim mode toggle. */
  simulation = $state(false);
  simBusy = $state(false);

  /** "Killed" = every enabled script disabled. Local UI flag — firmware has no
   * persistent kill switch yet, so this is just whether the disable-all sweep
   * has been run since last connect. */
  killed = $state(false);
  killBusy = $state(false);

  /** Bus status driven by firmware bus_status push frames. */
  bus0Status = $state<BusStatus>('idle');
  bus1Status = $state<BusStatus>('idle');
  /** RX frames-per-second per bus, pushed alongside bus_status. */
  bus0Rate = $state(0);
  bus1Rate = $state(0);

  /** Total FreeRTOS CPU load percentage pushed from firmware. */
  cpuLoad = $state(0);

  car = $state<Car | null>(loadCar());
  carPickerOpen = $state(false);
  paletteOpen = $state(false);
  logOpen = $state(false);
  logAttention = $state(false);
  logs = $state<LogLine[]>([]);
  logLevel = $state<LogLevel>(loadLogLevel());
  logHeight = $state<number>(loadLogHeight());

  /** Cross-view hand-off: another view (Events, Gallery) drops a script
   * filename here, then setView('scripts'). ScriptsView consumes + clears. */
  pendingExample = $state<string | null>(null);

  /** Cross-view hand-off for Gallery → DBC. Picks the bus tab and the
   * URL to fetch+parse+upload. DbcView consumes + clears. */
  pendingDbc = $state<{ url: string; busId: number; name: string } | null>(null);

  /** Cross-view hand-off: BusPip → Trace. Pre-filters the Trace view to
   * this bus id. TraceView consumes + clears on mount. */
  tracePendingBus = $state<number | null>(null);

  /** Cross-view hand-off: LogPane → Scripts. Carries a script filename + line
   * number so ScriptsView loads the script and scrolls the editor to that line.
   * Consumed + cleared by ScriptsView. */
  gotoEditorLine = $state<{ filename: string; line: number } | null>(null);

  /** Cross-view hand-off: Scripts → AI. Carries the current editor content
   * so AIView can pre-attach it as context. AIView consumes + clears. */
  pendingAiScript = $state<{ filename: string; code: string } | null>(null);

  /** Last-loaded DBC name per bus, indexed by bus id. Status pips link
   * to the DBC view + bus tab when a slot is populated. */


  /** WiFi IP address (or null if not connected). Read by any view that needs
   * to construct the file server URL. Updated by refreshWifiIp(). */
  wifiIp = $state<string | null>(null);

  /** Pure navigation hand-off: bus-pip "(dbc)" link writes the bus id
   * here, then setView('dbc'). DbcView consumes + clears. */
  dbcViewBus = $state<number | null>(null);

  // ---- OTA (Over-the-Air firmware update) ----
  otaBusy = $state(false);
  otaProgress = $state(0);   // 0–100
  otaTotal = $state(0);      // total bytes
  otaSpeed = $state('');     // throughput during transfer, e.g. "12 KB/s"
  otaStatus = $state('');    // human-readable status line
  otaError = $state<string | null>(null);
  otaDone = $state(false);
  rebootAfterUpdate = $state(true);
  suppressUnexpectedDisconnect = false;
  /** Guards against concurrent transport recovery attempts. */
  private recovering = false;

  /** Attempt to recover a stalled BLE notification stream.
   *  Tries restarting notifications first (gentle), then full reconnect.
   *  Fires at most once per stall episode. */
  private async recoverStalledTransport(): Promise<void> {
    if (this.recovering || !this.connected) return;
    this.recovering = true;
    this.pushLog('BLE notification stream stalled — recovering', 'warn', 'ble');

    // Level 1: re-subscribe to TX notifications without disconnecting.
    try {
      await this.ble.restartNotifications();
      this.pushLog('restarted BLE notifications', 'info', 'ble');
      // Verify with a ping. If it succeeds we're done.
      try {
        await this.proto.ping();
        this.pushLog('BLE transport recovered', 'info', 'ble');
        return;
      } catch {
        // ping still failed — notifications not actually flowing
      }
    } catch (e) {
      const m = e instanceof Error ? e.message : String(e);
      this.pushLog(`restartNotifications failed: ${m}`, 'warn', 'ble');
    }

    // Level 2: full reconnect.
    this.pushLog('reconnecting BLE transport', 'warn', 'ble');
    this.proto.reset();
    try {
      await this.ble.reconnect();
      // onConnChange(true) handles re-initialising protocol state
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.pushLog(`BLE reconnect failed: ${msg}`, 'error', 'ble');
    } finally {
      this.recovering = false;
    }
  }

  // ---- AI assistant ----
  aiKey = $state<string>((() => { try { return localStorage.getItem('dc-ai-key') ?? ''; } catch { return ''; } })());
  aiModel = $state<string>((() => { try { return localStorage.getItem('dc-ai-model') ?? 'claude-haiku-4-5-20251001'; } catch { return 'claude-haiku-4-5-20251001'; } })());

  setAiKey(k: string): void {
    this.aiKey = k;
    try { localStorage.setItem('dc-ai-key', k); } catch { /* ignore */ }
  }
  setAiModel(m: string): void {
    this.aiModel = m;
    try { localStorage.setItem('dc-ai-model', m); } catch { /* ignore */ }
  }

  // ---- PWA install ----
  /** Non-null when the browser has emitted a deferrable install prompt. Cleared on install or dismiss. */
  installPrompt = $state<BeforeInstallPromptEvent | null>(null);
  /** True when already running as an installed standalone app. */
  isInstalled = $state(
    typeof window !== 'undefined' &&
    (window.matchMedia('(display-mode: standalone)').matches ||
     (navigator as any).standalone === true),
  );

  constructor() {
    this.ble.onConnectionChange((c) => this.onConnChange(c));
    this.ble.onDisconnect((kind) => {
      if (kind === 'auth_fail') {
        toast.show({
          severity: 'error',
          message: 'Dorky rejected the connection. Maybe you reconnected too quickly. Or the OS has a stale bond — remove "Dorky" from your Bluetooth settings, then reconnect.',
          link: { href: 'https://github.com/dzid26/ESP32-DualCAN/blob/main/docs/ble.md', text: 'More info' },
          duration: 12000,
        });
      } else if (kind === 'replaced') {
        toast.show({
          severity: 'warn',
          message: 'Dorky taken over by another client.',
          duration: 6000,
        });
      } else if (kind === 'unexpected') {
        if (this.suppressUnexpectedDisconnect) {
          this.suppressUnexpectedDisconnect = false;
          return;
        }
        toast.show({
          severity: 'warn',
          message: 'Dorky disconnected unexpectedly. Out of range or powered off.',
          duration: 6000,
        });
      }
    });
    const IDF_LEVEL: Record<string, LogLine['level']> = {
      I: 'info', W: 'warn', E: 'error', D: 'debug', V: 'debug',
    };
    this.proto.onLog((e) => {
      if ('raw' in e) {
        const m = e.raw.match(/^([IEWDV]) \(\d+\) ([^:]+): (.+)/);
        if (m) this.pushLog(m[3], IDF_LEVEL[m[1]] ?? 'info', m[2]);
      } else {
        this.pushLog(e.msg, e.level as LogLine['level'], e.src);
      }
    });
    this.proto.onBusStatus((u) => this.noteBusStatus(u.bus, u.status, u.rate));
    this.proto.onScriptUpdate(() => { this.scriptsVersion++; });
    this.proto.onCpuStatus((u) => { this.cpuLoad = u.load; });
    this.proto.onStall(() => this.recoverStalledTransport());

    if (typeof window !== 'undefined') {
      window.addEventListener('beforeinstallprompt', (e) => {
        e.preventDefault();
        this.installPrompt = e as BeforeInstallPromptEvent;
      });
      window.addEventListener('appinstalled', () => {
        this.installPrompt = null;
        this.isInstalled = true;
      });
      window.addEventListener('hashchange', () => {
        const hash = location.hash.slice(1);
        if (hash && VALID_VIEWS.has(hash)) this.view = hash as ViewId;
      });
    }
  }

  async installApp(): Promise<void> {
    const prompt = this.installPrompt;
    if (!prompt) return;
    await prompt.prompt();
    const { outcome } = await prompt.userChoice;
    if (outcome === 'accepted') {
      this.installPrompt = null;
      this.isInstalled = true;
    }
  }

  // ---- Actions ----

  setView(v: ViewId): void {
    this.view = v;
    saveView(v);
  }

  setLastScriptFilename(fn: string | null): void {
    this.lastScriptFilename = fn;
    saveLastScriptFilename(fn);
  }

  pushLog(msg: string, level: LogLine['level'] = 'info', src = 'system'): void {
    this.logs = [...this.logs, { ts: nowTs(), level, src, msg }];
    if (this.logs.length > 500) this.logs = this.logs.slice(-500);
    if (level === 'error' && !this.logOpen) this.logOpen = true;
    if (level === 'warn' && !this.logOpen) this.logAttention = true;
  }

  clearLogs(): void { this.logs = []; }

  setLogLevel(level: LogLevel): void {
    this.logLevel = level;
    try { localStorage.setItem('dc-log-level', level); } catch { /* ignore */ }
    if (this.connected) this.proto.setLogLevel(level).catch(() => { /* best effort */ });
  }

  setLogHeight(h: number): void {
    this.logHeight = Math.round(h);
    try { localStorage.setItem('dc-log-height', String(this.logHeight)); } catch { /* ignore */ }
  }

  async toggleConnect(): Promise<void> {
    if (this.connecting) return;
    this.connectError = null;
    try {
      if (this.connected) {
        await this.ble.disconnect();
      } else {
        this.connecting = true;
        await this.ble.connect();
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      /* Match both the explicit cancel string and DOMException name="NotFoundError"
       * which Chrome throws when the user dismisses the BLE picker without picking. */
      const isNotFound =
        (typeof DOMException !== 'undefined' && e instanceof DOMException && e.name === 'NotFoundError') ||
        /cancell?ed|dismiss|NotFoundError|User cancelled/i.test(msg);
      if (isNotFound) {
        this.connectError = 'Connect cancelled';
        // toast.show({
        //   severity: 'info',
        //   message: 'Connect cancelled. Click Connect again and pick a "Dorky-XXXX" device from the browser\'s Bluetooth picker.',
        //   duration: 7000,
        // });
      } else {
        const message = msg.includes('Web Bluetooth not available')
          ? msg
          : 'Connect failed. Check the board is powered, in range, and or un-pair in your OS Bluetooth settings.';
        this.connectError = message;
        toast.show({
          severity: 'error',
          message,
          link: { href: 'https://github.com/dzid26/ESP32-DualCAN/blob/main/docs/ble.md', text: 'More info' },
          duration: 90000,
        });
      }
      this.pushLog(this.connectError, isNotFound ? 'warn' : 'error', 'ble');
    } finally {
      this.connecting = false;
    }
  }

  private async onConnChange(c: boolean): Promise<void> {
    this.connected = c;
    this.connecting = false;
    this.deviceName = c ? this.ble.deviceName : null;
    if (!c) {
      this.proto.reset();
      this.fwVersion = null;
      this.protoMismatch = null;
      this.scriptingApiVersion = undefined;
      this.simulation = false;
      this.killed = false;
      this.bus0Status = 'idle';
      this.bus1Status = 'idle';
      this.wifiIp = null;
      this.bus0Rate = 0;
      this.bus1Rate = 0;
      this.cpuLoad = 0;
      this.resetOtaState();
      this.pushLog('disconnected', 'warn', 'ble');
      return;
    }
    this.pushLog('connected', 'info', 'ble');
    try {
      const info = await this.proto.systemInfo();
      this.fwVersion = info.fw_version;
      this.scriptingApiVersion = info.scripting_api_version;
      this.protoMismatch = info.proto_version !== UI_PROTO_VERSION
        ? `firmware proto v${info.proto_version}, UI expects v${UI_PROTO_VERSION} — some features may not work`
        : null;
    } catch (e) {
      if (!(e instanceof Error && /timeout/i.test(e.message))) {
        this.protoMismatch = 'firmware too old (no system.info) — please update';
      }
    }
    /* Query the negotiated ATT MTU so OTA writes use the largest possible
     * chunk size.  Best-effort; fallback is the safe 180-byte default. */
    this.proto.adjustChunkFromMtu().catch(() => {});
    try {
      const r = await this.proto.getSecret('anthropic');
      if (r.value) this.setAiKey(r.value);
    } catch { /* key not set or firmware too old */ }
    /* Sync the log level dropdown to the firmware's actual level, then
     * re-apply any stored preference from a previous session. */
    const storedLevel = this.logLevel;
    try { const r = await this.proto.getLogLevel(); this.logLevel = r.level; }
    catch { /* firmware too old — keep stored default */ }
    // this.setLogLevel(storedLevel);
    this.refreshWifiIp();
  }

  async refreshWifiIp(): Promise<void> {
    if (!this.connected) return;
    try {
      const s = await this.proto.wifiStatus();
      this.wifiIp = s.connected && s.ip ? s.ip : null;
    } catch { /* keep stale value */ }
  }

  // ---- OTA Actions ----

  resetOtaState(): void {
    this.otaBusy = false;
    this.otaProgress = 0;
    this.otaTotal = 0;
    this.otaSpeed = '';
    this.otaStatus = '';
    this.otaError = null;
    this.otaDone = false;
  }

  async doOTA(bin: Uint8Array, label: string): Promise<void> {
    if (!this.connected) {
      this.otaError = 'Not connected';
      return;
    }
    this.otaBusy = true;
    this.otaError = null;
    this.otaDone = false;
    this.otaProgress = 0;
    this.otaTotal = bin.length;
    this.otaStatus = `Preparing flash for ${label} (${(bin.length / 1024).toFixed(0)} KB)…`;
    this.pushLog(`OTA: starting upload of ${label} (${bin.length} bytes)`, 'info', 'ota');

    const t0 = performance.now();
    try {
      await this.proto.streamFirmware(bin, (sent, total) => {
        this.otaProgress = Math.round((sent / total) * 100);
        const elapsed = (performance.now() - t0) / 1000;
        if (elapsed > 0.5) {
          const bps = sent / elapsed;
          this.otaSpeed = bps >= 1024 ? `${(bps / 1024).toFixed(1)} KB/s` : `${bps.toFixed(0)} B/s`;
        }
        this.otaStatus = `Flashing… ${this.otaProgress}% (${(sent / 1024).toFixed(0)} / ${(total / 1024).toFixed(0)} KB)`;
      });

      // Fetch the LATEST value of rebootAfterUpdate just before ending
      const reboot = this.rebootAfterUpdate;
      if (reboot) {
        this.suppressUnexpectedDisconnect = true;
        toast.show({
          severity: 'info',
          message: 'OTA complete — device rebooting now. Disconnect is expected.',
          duration: 7000,
        });
      }
      await this.proto.otaEnd(reboot, bin.length);

      this.otaDone = true;
      if (reboot) {
        this.otaStatus = 'Update complete — device is rebooting…';
        this.pushLog('OTA: firmware installed, device rebooting', 'info', 'ota');
      } else {
        this.otaStatus = 'Update complete — manual reboot required';
        this.pushLog('OTA: firmware installed, manual reboot required', 'info', 'ota');
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.otaError = msg;
      this.otaStatus = '';
      this.pushLog(`OTA failed: ${msg}`, 'error', 'ota');
      this.suppressUnexpectedDisconnect = false;
      try {
        await this.proto.otaAbort();
      } catch {
        /* best-effort */
      }
    } finally {
      this.otaBusy = false;
    }
  }

  async abortOTA(): Promise<void> {
    try {
      await this.proto.otaAbort();
      this.otaStatus = 'Aborted';
      this.otaSpeed = '';
      this.otaBusy = false;
      this.pushLog('OTA aborted by user', 'warn', 'ota');
    } catch {
      /* ignore */
    }
  }

  async rebootDevice(): Promise<void> {
    try {
      this.suppressUnexpectedDisconnect = true;
      await this.proto.systemReboot();
      toast.show({
        severity: 'info',
        message: 'Device rebooting now. Disconnect is expected.',
        duration: 7000,
      });
      this.pushLog('Device reboot command sent', 'info', 'system');
      this.otaStatus = 'Rebooting…';
    } catch (e) {
      this.suppressUnexpectedDisconnect = false;
      const msg = e instanceof Error ? e.message : String(e);
      this.pushLog(`Reboot failed: ${msg}`, 'error', 'system');
    }
  }

  async toggleSim(): Promise<void> {
    if (this.simBusy) return;
    const next = !this.simulation;
    this.simBusy = true;
    try {
      if (this.connected) await this.proto.setSimMode(next);
      this.simulation = next;
      this.pushLog(next
        ? 'Simulation mode ENABLED — sends routed to log'
        : 'Simulation mode disabled', 'info', 'system');
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.pushLog(`sim toggle failed: ${msg}`, 'error', 'system');
    } finally {
      this.simBusy = false;
    }
  }

  /** Filenames that were enabled at the moment Kill engaged — re-enabled on release. */
  private restoreOnRelease: string[] = [];

  /** Bumped on every kill / release so view-side $effects re-fetch their lists. */
  scriptsVersion = $state(0);
  /** Last known script enable-state change, used for live cross-view updates
   * while a kill / release sweep is still running. */
  scriptStatusPatch = $state<{ seq: number; filename: string; enabled: boolean; clearError: boolean } | null>(null);
  private scriptStatusSeq = 0;

  private publishScriptStatus(filename: string, enabled: boolean, clearError = false): void {
    this.scriptStatusPatch = {
      seq: ++this.scriptStatusSeq,
      filename,
      enabled,
      clearError,
    };
  }

  /** Kill switch: disable every enabled script and remember which they were.
   * Release: re-enable that remembered set. Both modes refresh ScriptsView via
   * scriptsVersion. */
  async toggleKill(): Promise<void> {
    if (this.killBusy) return;
    if (!this.connected) {
      this.pushLog('Kill switch needs an active connection', 'warn', 'system');
      return;
    }
    this.killBusy = true;
    try {
      if (this.killed) {
        // Release — re-enable everything we disabled.
        let restored = 0;
        for (const fn of this.restoreOnRelease) {
          try {
            await this.proto.enableScript(fn);
            this.publishScriptStatus(fn, true, true);
            restored++;
          } catch (e) {
            const msg = e instanceof Error ? e.message : String(e);
            this.pushLog(`re-enable ${fn} failed: ${msg}`, 'error', 'system');
          }
        }
        this.restoreOnRelease = [];
        this.killed = false;
        this.pushLog(`Kill switch released — re-enabled ${restored} script(s)`, 'info', 'system');
      } else {
        // Engage — list, disable each enabled, remember them.
        const r = await this.proto.listScripts();
        const enabled = r.scripts.filter(s => s.enabled);
        const remember: string[] = [];
        for (const s of enabled) {
          try {
            await this.proto.disableScript(s.filename);
            this.publishScriptStatus(s.filename, false);
            remember.push(s.filename);
          } catch (e) {
            const msg = e instanceof Error ? e.message : String(e);
            this.pushLog(`disable ${s.filename} failed: ${msg}`, 'error', 'system');
          }
        }
        this.restoreOnRelease = remember;
        this.killed = true;
        this.pushLog(`KILL SWITCH engaged — disabled ${remember.length} script(s)`, 'warn', 'system');
      }
      this.scriptsVersion++;
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.pushLog(`kill failed: ${msg}`, 'error', 'system');
    } finally {
      this.killBusy = false;
    }
  }

  pickCar(c: Car): void {
    this.car = c;
    saveCar(c.id);
    this.carPickerOpen = false;
    const suffix = c.customDbc ? '' : ` (opendbc: ${c.dbc})`;
    this.pushLog(`Vehicle profile set: ${c.brand} ${c.model}${suffix}`, 'info', 'system');
    if (this.view === 'tesla' && c.brand !== 'Tesla') this.setView('events');
  }

  clearCar(): void {
    this.car = null;
    saveCar(null);
    this.carPickerOpen = false;
    this.pushLog('Vehicle profile cleared', 'info', 'system');
    if (this.view === 'tesla') this.setView('events');
  }

  noteBusStatus(bus: number, status: BusStatus, rate: number): void {
    if (bus === 0) { this.bus0Status = status; this.bus0Rate = rate; }
    else if (bus === 1) { this.bus1Status = status; this.bus1Rate = rate; }
  }
}

export const app = new AppState();
