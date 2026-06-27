import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseDbc, type Message } from '../src/dbc/parser.ts';
import { preprocessScript } from '../src/lib/preprocessor.ts';

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO = join(__dirname, '../..');

/* ---- Parse all 3 Tesla DBC files into one message list ---- */

function loadDbcMessages(): Message[] {
  const dbcDir = join(REPO, 'dbc');
  const all: Message[] = [];
  for (const f of ['Model3_VEH.dbc', 'Model3_CH.dbc', 'Model3_PARTY.dbc']) {
    const text = readFileSync(join(dbcDir, f), 'latin1');
    all.push(...parseDbc(text));
  }
  return all;
}

const messages = loadDbcMessages();

/* ---- Helper: find a message by name ---- */

function findMsg(name: string): Message | undefined {
  return messages.find(m => m.name === name);
}

/* ---- Helper: spot-check a signal's metadata ---- */

function assertSignal(
  msgName: string,
  sigName: string,
  expected: { startBit: number; bitLength: number; isBigEndian: boolean; isSigned: boolean; scale: number; offset: number },
) {
  const msg = findMsg(msgName);
  assert.ok(msg, `Message '${msgName}' not found in DBC`);
  const sig = msg.signals.find(s => s.name === sigName);
  assert.ok(sig, `Signal '${sigName}' not found in message '${msgName}'`);
  assert.equal(sig.startBit, expected.startBit, `${msgName}.${sigName} startBit`);
  assert.equal(sig.bitLength, expected.bitLength, `${msgName}.${sigName} bitLength`);
  assert.equal(sig.isBigEndian, expected.isBigEndian, `${msgName}.${sigName} isBigEndian`);
  assert.equal(sig.isSigned, expected.isSigned, `${msgName}.${sigName} isSigned`);
  assert.equal(sig.scale, expected.scale, `${msgName}.${sigName} scale`);
  assert.equal(sig.offset, expected.offset, `${msgName}.${sigName} offset`);
}

/* ================================================================
 *  DBC parse sanity — make sure the DBC files parse correctly
 * ================================================================ */

test('DBC parses all files without throwing', () => {
  assert.ok(messages.length > 0, 'no messages parsed');
  assert.ok(messages.every(m => m.id > 0 && m.name && m.signals));
});

test('DBC parsed DAS_bodyControls signal metadata', () => {
  assertSignal('DAS_bodyControls', 'DAS_hazardLightRequest', {
    startBit: 2, bitLength: 2, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
  assertSignal('DAS_bodyControls', 'DAS_headlightRequest', {
    startBit: 0, bitLength: 2, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
  assertSignal('DAS_bodyControls', 'DAS_bodyControlsChecksum', {
    startBit: 56, bitLength: 8, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
  assertSignal('DAS_bodyControls', 'DAS_bodyControlsCounter', {
    startBit: 52, bitLength: 4, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
});

test('DBC parsed DI_systemStatus signal metadata', () => {
  assertSignal('DI_systemStatus', 'DI_gear', {
    startBit: 21, bitLength: 3, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
  assertSignal('DI_systemStatus', 'DI_accelPedalPos', {
    startBit: 32, bitLength: 8, isBigEndian: false, isSigned: false, scale: 0.4, offset: 0,
  });
});

test('DBC parsed UI_vehicleControl signal metadata', () => {
  assertSignal('UI_vehicleControl', 'UI_mirrorFoldRequest', {
    startBit: 24, bitLength: 2, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
});

test('DBC parsed VCRIGHT_doorStatus signal metadata', () => {
  assertSignal('VCRIGHT_doorStatus', 'VCRIGHT_frontHandlePulled', {
    startBit: 10, bitLength: 1, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
  assertSignal('VCRIGHT_doorStatus', 'VCRIGHT_frontLatchStatus', {
    startBit: 14, bitLength: 4, isBigEndian: false, isSigned: false, scale: 1, offset: 0,
  });
});

/* ================================================================
 *  Preprocess every example .be script and verify the output
 * ================================================================ */

const examplesDir = join(REPO, 'firmware/scripts_examples');
const scriptFiles = readdirSync(examplesDir).filter(f => f.endsWith('.be'));

// Scripts that reference DBC messages/signals and should preprocess without errors
const dbcScripts = new Set([
  'tesla_blink_hazards_tile.be',
  'tesla_das_bodycontrols_loopback.be',
  'tesla_fold_mirror_tile.be',
  'tesla_gear_led_demo.be',
  'track_mode_on_full_throttle.be',
  'window_drop_on_handle_pull.be',
]);

// Scripts that use only raw CAN (no DBC refs) — should pass through unchanged
const rawScripts = new Set([
  'bench_test.be',
  'hello_log.be',
  'loopback_led.be',
  'tesla_doors_sim.be',
  'tiles_demo.be',
]);

for (const fn of scriptFiles) {
  test(`preprocesses ${fn}`, () => {
    const code = readFileSync(join(examplesDir, fn), 'utf-8');
    const result = preprocessScript(code, messages);

    assert.ok(typeof result.code === 'string', `${fn}: code should be a string`);

    if (dbcScripts.has(fn)) {
      assert.equal(result.errors.length, 0,
        `${fn}: DBC-referencing script should have 0 errors, got: ${result.errors.join('; ')}`);
      assert.notEqual(result.code, code, `${fn}: DBC-referencing script should be transformed`);
    }

    if (rawScripts.has(fn)) {
      assert.equal(result.code, code, `${fn}: raw-CAN script should pass through unchanged`);
      assert.equal(result.errors.length, 0, `${fn}: should have 0 errors`);
    }
  });
}

/* ---- Spot-check specific preprocessor output ---- */

test('tesla_blink_hazards_tile .bep uses msg_sig_set', () => {
  const code = readFileSync(join(examplesDir, 'tesla_blink_hazards_tile.be'), 'utf-8');
  const result = preprocessScript(code, messages);
  assert(result.code.includes('0x3e9'), 'should contain hex message ID');
  assert(result.code.includes('msg_sig_set('), 'should contain msg_sig_set() calls');
  assert(!result.code.includes('"DAS_bodyControls"'), 'message name should be replaced');
  assert(result.code.includes('can_msg_send('), 'can_msg_send should pass through');
});

test('tesla_gear_led_demo .bep uses msg_sig_get', () => {
  const code = readFileSync(join(examplesDir, 'tesla_gear_led_demo.be'), 'utf-8');
  const result = preprocessScript(code, messages);
  assert(result.code.includes('0x118'), 'should contain DI_systemStatus hex ID');
  assert(result.code.includes('can_msg_get(0, 0x118)'), 'should contain can_msg_get for DI_systemStatus');
  assert(result.code.includes('msg_sig_get('), 'should contain msg_sig_get() calls');
});

test('window_drop_on_handle_pull .bep uses msg_sig_get and msg_sig_set', () => {
  const code = readFileSync(join(examplesDir, 'window_drop_on_handle_pull.be'), 'utf-8');
  const result = preprocessScript(code, messages);
  assert(result.code.includes('can_msg_send('), 'can_msg_send should pass through');
  assert(result.code.includes('0x294'), 'should contain UI_vehicleControl3 hex ID');
  assert(result.code.includes('0x102'), 'should contain VCLEFT_doorStatus hex ID');
  assert(result.code.includes('0x103'), 'should contain VCRIGHT_doorStatus hex ID');
  assert(result.code.includes('can_msg_get(0, 0x102)'), 'should contain can_msg_get for VCLEFT');
  assert(result.code.includes('can_msg_get(0, 0x103)'), 'should contain can_msg_get for VCRIGHT');
  assert(result.code.includes('msg_sig_get('), 'should contain msg_sig_get() calls');
  assert(result.code.includes('msg_sig_set('), 'should contain msg_sig_set() calls');
});

test('tesla_das_bodycontrols_loopback .bep has all msg_sig_set calls', () => {
  const code = readFileSync(join(examplesDir, 'tesla_das_bodycontrols_loopback.be'), 'utf-8');
  const result = preprocessScript(code, messages);
  const matches = result.code.match(/msg_sig_set\(/g);
  assert.ok(matches, 'should contain msg_sig_set() calls');
  assert.equal(matches.length, 8, 'tesla_das_bodycontrols_loopback has 8 msg_sig_set calls');
});

test('raw CAN scripts pass through unchanged', () => {
  for (const fn of rawScripts) {
    const code = readFileSync(join(examplesDir, fn), 'utf-8');
    const result = preprocessScript(code, messages);
    assert.equal(result.code, code, `${fn} should be identical`);
    assert.equal(result.errors.length, 0, `${fn} should have no errors`);
  }
});
