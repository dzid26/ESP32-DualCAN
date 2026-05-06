// Mock data — mirrors the design bundle's seeds so the prototype renders the
// same screens. Replace with real protocol calls when wiring to firmware.

export type EventLed = 'off' | 'on' | 'warn' | 'err';

export type EventTile = {
  id: string;
  name: string;
  icon: string;
  desc: string;
  danger: boolean;
  lastRun: string | null;
  led?: EventLed;
};

export const SAMPLE_EVENTS: EventTile[] = [
  { id: 'horn',       name: 'Honk horn',       icon: '📯', desc: 'Single short pulse',         danger: false, lastRun: '2m ago' },
  { id: 'hl-on',      name: 'Headlights ON',   icon: '💡', desc: 'CAN1 0x3E9 byte4 bit0=1',    danger: true,  lastRun: null,        led: 'off' },
  { id: 'hl-off',     name: 'Headlights OFF',  icon: '🌙', desc: 'CAN1 0x3E9 byte4 bit0=0',    danger: true,  lastRun: '14m ago',   led: 'on'  },
  { id: 'beep',       name: 'Cabin beep',      icon: '🔔', desc: '500 Hz · 200 ms',            danger: false, lastRun: 'never' },
  { id: 'lock',       name: 'Lock doors',      icon: '🔒', desc: 'Body bus · BCM cmd',         danger: true,  lastRun: null,        led: 'on'  },
  { id: 'unlock',     name: 'Unlock doors',    icon: '🔓', desc: 'Body bus · BCM cmd',         danger: true,  lastRun: null,        led: 'off' },
  { id: 'fan-3',      name: 'HVAC fan → 3',    icon: '🌬', desc: 'Climate bus',                danger: false, lastRun: '1h ago',    led: 'warn'},
  { id: 'reset-trip', name: 'Reset trip A',    icon: '↺',  desc: 'IC dashboard',               danger: false, lastRun: null },
  { id: 'reboot',     name: 'Reboot device',   icon: '⏻',  desc: 'Soft reset · keeps NVS',     danger: true,  lastRun: '3d ago' },
];

export type Script = {
  id: string;
  name: string;
  bus: number;
  enabled: boolean;
  err: string | null;
  code: string;
};

export const SAMPLE_SCRIPTS: Script[] = [
  {
    id: 'win', name: 'Window drop on long handle pull', bus: 0, enabled: true, err: null,
    code: `# @name Window drop on long handle pull
# @bus 0

def setup()
  can.on("DriverDoorHandle", on_handle)
end

def on_handle(sig)
  if sig.value == 1 && sig.age_ms > 2000
    var msg = can.message("VCLEFT_windowSwitch")
    msg.set("window_cmd", "down")
    msg.send()
  end
end`
  },
  {
    id: 'tm', name: 'Track mode on full throttle', bus: 0, enabled: false, err: null,
    code: `# @bus 0
def setup()
  # …
end`
  },
  {
    id: 'br', name: 'Bridge CAN0 → CAN1', bus: 0, enabled: true, err: 'checksum mismatch at 0x229',
    code: `# @bus 0
def setup()
  can.raw.on_frame(0, fn f ->
    can.raw.send(1, f.id, f.data)
  end)
end`
  },
];

export type DbcMsg = { id: number; name: string; sigs: number; age: number };

export const DBC_MSGS: DbcMsg[] = [
  { id: 0x118, name: 'DI_systemStatus',          sigs: 7, age: 12  },
  { id: 0x229, name: 'SCCM_steeringAngleSensor', sigs: 4, age: 40  },
  { id: 0x3D8, name: 'ID3D8GPSLatLong',          sigs: 2, age: 210 },
  { id: 0x257, name: 'DI_speed',                 sigs: 3, age: 20  },
  { id: 0x39D, name: 'IBST_status',              sigs: 6, age: 55  },
];

export type GalleryScript = {
  n: string; bus: number; desc: string;
  author: string; stars: number; brands: string[];
};

export const GALLERY_SCRIPTS: GalleryScript[] = [
  { n: 'Window drop on long handle pull', bus: 0, desc: 'Lower driver window when door is open and handle held 2s+.', author: 'dzid26',    stars: 42, brands: ['Tesla'] },
  { n: 'Track mode on full throttle',     bus: 0, desc: 'Arms track mode when throttle pedal pinned for 3s.',          author: 'dzid26',    stars: 31, brands: ['Tesla'] },
  { n: 'Lock confirm honk',               bus: 0, desc: 'Short horn tap on double-lock.',                                author: 'community', stars: 18, brands: ['*'] },
  { n: 'Charge port LED mirror',          bus: 0, desc: 'Mirror charge state to the onboard RGB LED.',                  author: 'community', stars: 12, brands: ['Tesla', 'Hyundai', 'Kia'] },
  { n: 'Wiper auto-park on shutdown',     bus: 0, desc: 'Force wipers to park before sleep — fixes random mid-windshield stop.', author: 'community', stars: 24, brands: ['Toyota', 'Honda'] },
  { n: 'TPMS warning chime mute',         bus: 0, desc: "Suppress the cabin chime once you've acknowledged a TPMS warning.", author: 'dzid26', stars: 9, brands: ['Subaru', 'Mazda'] },
];

export const DBC_SOURCE = { repo: 'commaai/opendbc', url: 'https://github.com/commaai/opendbc' };

export type GalleryDbc = {
  n: string; bus: number; desc: string; file: string;
  sigs: number; msgs: number; ver: string; size: string; brands: string[];
};

export const GALLERY_DBCS: GalleryDbc[] = [
  { n: 'Tesla Model 3 / Y — VehicleCAN', bus: 0, desc: 'Reverse-engineered from the Model 3/Y party bus. 61 messages, 428 signals.', file: 'tesla_model3_party.dbc',     sigs: 428, msgs: 61, ver: '2026.03', size: '2.1 KB', brands: ['Tesla'] },
  { n: 'Tesla Model 3 / Y — ChassisCAN', bus: 1, desc: 'Brake, steering, IBST, EPAS frames on the chassis bus.',                    file: 'tesla_model3_chassis.dbc',   sigs: 187, msgs: 24, ver: '2026.03', size: '0.9 KB', brands: ['Tesla'] },
  { n: 'Tesla Model S / X — Pre-Raven',  bus: 0, desc: 'Legacy MS/MX vehicle bus through 2019 Raven refresh.',                       file: 'tesla_modelS_preraven.dbc',  sigs: 312, msgs: 51, ver: '2025.08', size: '1.6 KB', brands: ['Tesla'] },
  { n: 'Tesla Model S / X — Raven+',     bus: 0, desc: 'Post-Raven MS/MX. Newer steering rack and inverter messages.',               file: 'tesla_modelS_raven.dbc',     sigs: 298, msgs: 49, ver: '2025.11', size: '1.5 KB', brands: ['Tesla'] },
  { n: 'Cybertruck — VehicleCAN',        bus: 0, desc: 'Partial coverage. Steer-by-wire, tri-motor torque, air-suspension.',         file: 'tesla_cybertruck.dbc',       sigs:  92, msgs: 22, ver: '2026.01', size: '0.5 KB', brands: ['Tesla'] },
  { n: 'Toyota — RAV4 / Prius / Corolla', bus: 0, desc: 'Shared Toyota powertrain DBC. Brake, accel, steering torque, ACC.',         file: 'toyota_rav4_2019_pt.dbc',    sigs: 215, msgs: 38, ver: '2026.02', size: '1.2 KB', brands: ['Toyota'] },
  { n: 'Honda — Civic / Accord / CR-V',  bus: 0, desc: 'Bosch radar architecture Honda powertrain. Auto-generated.',                 file: 'honda_civic_2022_can_generated.dbc', sigs: 244, msgs: 41, ver: '2026.02', size: '1.4 KB', brands: ['Honda'] },
  { n: 'Hyundai EV — Ioniq 5 / EV6',     bus: 0, desc: 'E-GMP platform. BMS, charge, regen, vehicle dynamics.',                      file: 'hyundai_ev_2022.dbc',        sigs: 271, msgs: 44, ver: '2026.02', size: '1.5 KB', brands: ['Hyundai', 'Kia'] },
  { n: 'Powertrain debug overlay',       bus: 0, desc: 'Adds inverter currents and battery cell deltas on top of the M3 base DBC.',  file: 'tesla_m3_pt_overlay.dbc',    sigs:  38, msgs:  7, ver: '2026.02', size: '0.2 KB', brands: ['Tesla'] },
];

export type LogLine = { ts: string; level: 'info' | 'warn' | 'error' | 'debug'; src: string; msg: string };

export const SEED_LOGS: LogLine[] = [
  { ts: '00:00:00.012', level: 'info', src: 'system',  msg: 'Dorky Commander v0.3.1 booted' },
  { ts: '00:00:00.215', level: 'info', src: 'ble',     msg: 'GATT service advertising as DorkyCmdr-7F2A' },
  { ts: '00:00:01.088', level: 'info', src: 'dbc',     msg: 'Loaded /dbc/bus0.bin · 61 messages · 428 signals' },
  { ts: '00:00:01.121', level: 'info', src: 'scripts', msg: 'Loaded 3 scripts · 2 enabled' },
  { ts: '00:00:03.441', level: 'warn', src: 'scripts', msg: 'bridge.be: checksum mismatch at 0x229 — disabled' },
];
