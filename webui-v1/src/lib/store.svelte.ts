// Global app state — Svelte 5 runes shared across components.
// Wires the real BleTransport + Protocol from ../transport/* through to the
// status strip, banners, and views. Per-view local state stays in the views.

import { BleTransport } from '../transport/ble';
import { Protocol } from '../transport/protocol';
import type { LogLine } from './sampleData';
import { type Car, loadCar, saveCar } from './cars';

// Bump in lockstep with firmware proto_version when ops change.
const UI_PROTO_VERSION = 1;

export type Transport = 'ble' | 'ws';

export type ViewId =
  | 'events' | 'scripts' | 'ai' | 'gallery'
  | 'dashboard' | 'dbc'
  | 'trace' | 'capture'
  | 'settings' | 'tesla'
  | 'engine';

function loadView(): ViewId {
  try {
    const v = localStorage.getItem('dc-view') as ViewId | null;
    return v ?? 'events';
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

  /** Bus activity dots — derived from trace frame stream when active.
   * When trace is off there's no signal so they stay idle. */
  bus0 = $state(false);
  bus1 = $state(false);
  private busDecay0: ReturnType<typeof setTimeout> | null = null;
  private busDecay1: ReturnType<typeof setTimeout> | null = null;

  car = $state<Car | null>(loadCar());
  carPickerOpen = $state(false);
  paletteOpen = $state(false);
  logOpen = $state(false);
  logs = $state<LogLine[]>([]);

  /** Cross-view hand-off: another view (Events, Gallery) drops a script
   * filename here, then setView('scripts'). ScriptsView consumes + clears. */
  pendingExample = $state<string | null>(null);

  constructor() {
    this.ble.onConnectionChange((c) => this.onConnChange(c));
    this.proto.onLog((msg) => this.pushLog(msg, 'info', 'device'));
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
      this.bus0 = false;
      this.bus1 = false;
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

  /** Kill switch = disable every enabled script. The "release" path can't
   * un-disable since we don't track which were enabled before; users re-enable
   * what they want from the Scripts view. */
  async toggleKill(): Promise<void> {
    if (this.killBusy) return;
    if (this.killed) {
      // "Release" — purely a UI state flag here. Tell the user how to recover.
      this.killed = false;
      this.pushLog('Kill switch released — re-enable scripts from Automations', 'info', 'system');
      return;
    }
    if (!this.connected) {
      this.pushLog('Kill switch needs an active connection', 'warn', 'system');
      return;
    }
    this.killBusy = true;
    try {
      const r = await this.proto.listScripts();
      const enabled = r.scripts.filter(s => s.enabled);
      let n = 0;
      for (const s of enabled) {
        try {
          await this.proto.disableScript(s.filename);
          n++;
        } catch (e) {
          const msg = e instanceof Error ? e.message : String(e);
          this.pushLog(`disable ${s.filename} failed: ${msg}`, 'error', 'system');
        }
      }
      this.killed = true;
      this.pushLog(`KILL SWITCH engaged — disabled ${n} script(s)`, 'warn', 'system');
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

  /** Trace view calls this for every received frame so the status pips can
   * show live bus activity. Not subscribed when trace is off. */
  noteBusActivity(bus: number): void {
    if (bus === 0) {
      this.bus0 = true;
      if (this.busDecay0) clearTimeout(this.busDecay0);
      this.busDecay0 = setTimeout(() => { this.bus0 = false; }, 600);
    } else if (bus === 1) {
      this.bus1 = true;
      if (this.busDecay1) clearTimeout(this.busDecay1);
      this.busDecay1 = setTimeout(() => { this.bus1 = false; }, 600);
    }
  }
}

export const app = new AppState();
