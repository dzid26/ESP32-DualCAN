// Global app state — Svelte 5 runes shared across components.
// Wires the real BleTransport + Protocol from ../transport/* through to the
// status strip, banners, and views. Per-view local state stays in the views.

import { BleTransport } from '../transport/ble';
import { Protocol } from '../transport/protocol';
import type { BusStatus } from '../transport/protocol';
import type { LogLine } from './sampleData';
import { type Car, loadCar, saveCar } from './cars';

// Bump in lockstep with firmware proto_version when ops change.
const UI_PROTO_VERSION = 2;

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
  | 'engine';

const VALID_VIEWS = new Set<string>([
  'events','scripts','ai','gallery','signals','dbc','trace','capture','settings','tesla','engine'
]);

function loadView(): ViewId {
  try {
    const v = localStorage.getItem('dc-view');
    if (v === 'dashboard') return 'events'; // renamed
    if (v && VALID_VIEWS.has(v)) return v as ViewId;
    return 'events';
  } catch { return 'events'; }
}

function saveView(v: ViewId): void {
  try { localStorage.setItem('dc-view', v); } catch { /* ignore */ }
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

  connected = $state(false);
  connecting = $state(false);
  /** Surfaced as the transport pip subtitle. */
  transport: Transport = 'ble';
  connectError = $state<string | null>(null);

  fwVersion = $state<string | null>(null);
  protoMismatch = $state<string | null>(null);

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

  car = $state<Car | null>(loadCar());
  carPickerOpen = $state(false);
  paletteOpen = $state(false);
  logOpen = $state(false);
  logs = $state<LogLine[]>([]);

  /** Cross-view hand-off: another view (Events, Gallery) drops a script
   * filename here, then setView('scripts'). ScriptsView consumes + clears. */
  pendingExample = $state<string | null>(null);

  /** Cross-view hand-off for Gallery → DBC. Picks the bus tab and the
   * URL to fetch+parse+upload. DbcView consumes + clears. */
  pendingDbc = $state<{ url: string; busId: number; name: string } | null>(null);

  /** Cross-view hand-off: Scripts → AI. Carries the current editor content
   * so AIView can pre-attach it as context. AIView consumes + clears. */
  pendingAiScript = $state<{ filename: string; code: string } | null>(null);

  /** Last-loaded DBC name per bus, indexed by bus id. Status pips link
   * to the DBC view + bus tab when a slot is populated. */
  loadedDbc = $state<Record<number, string | null>>({ 0: null, 1: null });

  /** Pure navigation hand-off: bus-pip "(dbc)" link writes the bus id
   * here, then setView('dbc'). DbcView consumes + clears. */
  dbcViewBus = $state<number | null>(null);

  // ---- OTA (Over-the-Air firmware update) ----
  otaBusy = $state(false);
  otaProgress = $state(0);   // 0–100
  otaTotal = $state(0);      // total bytes
  otaStatus = $state('');    // human-readable status line
  otaError = $state<string | null>(null);
  otaDone = $state(false);
  rebootAfterUpdate = $state(true);

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
    this.proto.onLog((msg) => this.pushLog(msg, 'info', 'device'));
    this.proto.onBusStatus((u) => this.noteBusStatus(u.bus, u.status));

    if (typeof window !== 'undefined') {
      window.addEventListener('beforeinstallprompt', (e) => {
        e.preventDefault();
        this.installPrompt = e as BeforeInstallPromptEvent;
      });
      window.addEventListener('appinstalled', () => {
        this.installPrompt = null;
        this.isInstalled = true;
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

  pushLog(msg: string, level: LogLine['level'] = 'info', src = 'system'): void {
    this.logs = [...this.logs, { ts: nowTs(), level, src, msg }];
    if (this.logs.length > 500) this.logs = this.logs.slice(-500);
  }

  clearLogs(): void { this.logs = []; }

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
      this.connectError = msg;
      this.pushLog(`connect failed: ${msg}`, 'error', 'ble');
    } finally {
      this.connecting = false;
    }
  }

  private async onConnChange(c: boolean): Promise<void> {
    this.connected = c;
    this.connecting = false;
    if (!c) {
      this.fwVersion = null;
      this.protoMismatch = null;
      this.simulation = false;
      this.killed = false;
      this.bus0Status = 'idle';
      this.bus1Status = 'idle';
      this.resetOtaState();
      this.pushLog('disconnected', 'warn', 'ble');
      return;
    }
    this.pushLog('connected', 'info', 'ble');
    try {
      const info = await this.proto.systemInfo();
      this.fwVersion = info.fw_version;
      this.protoMismatch = info.proto_version !== UI_PROTO_VERSION
        ? `firmware proto v${info.proto_version}, UI expects v${UI_PROTO_VERSION} — some features may not work`
        : null;
    } catch {
      this.protoMismatch = 'firmware too old (no system.info) — please update';
    }
    try {
      const r = await this.proto.getSecret('anthropic');
      if (r.value) this.setAiKey(r.value);
    } catch { /* key not set or firmware too old */ }
  }

  // ---- OTA Actions ----

  resetOtaState(): void {
    this.otaBusy = false;
    this.otaProgress = 0;
    this.otaTotal = 0;
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

    try {
      await this.proto.streamFirmware(bin, (sent, total) => {
        this.otaProgress = Math.round((sent / total) * 100);
        this.otaStatus = `Flashing… ${this.otaProgress}% (${(sent / 1024).toFixed(0)} / ${(total / 1024).toFixed(0)} KB)`;
      });

      // Fetch the LATEST value of rebootAfterUpdate just before ending
      const reboot = this.rebootAfterUpdate;
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
      this.otaBusy = false;
      this.pushLog('OTA aborted by user', 'warn', 'ota');
    } catch {
      /* ignore */
    }
  }

  async rebootDevice(): Promise<void> {
    try {
      await this.proto.systemReboot();
      this.pushLog('Device reboot command sent', 'info', 'system');
      this.otaStatus = 'Rebooting…';
    } catch (e) {
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
    this.pushLog(`Vehicle profile set: ${c.brand} ${c.model} (${c.dbc})`, 'info', 'system');
  }

  clearCar(): void {
    this.car = null;
    saveCar(null);
    this.carPickerOpen = false;
    this.pushLog('Vehicle profile cleared', 'info', 'system');
  }

  noteBusStatus(bus: number, status: BusStatus): void {
    if (bus === 0) this.bus0Status = status;
    else if (bus === 1) this.bus1Status = status;
  }
}

export const app = new AppState();
