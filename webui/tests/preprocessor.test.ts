import test from 'node:test';
import assert from 'node:assert/strict';
import { preprocessScript } from '../src/lib/preprocessor.ts';

const mockMessages = [
  {
    id: 0x3e9,
    name: 'DAS_bodyControls',
    dlc: 8,
    signals: [
      { name: 'DAS_hazardLightRequest', startBit: 0, bitLength: 8, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
      { name: 'DAS_wipersControl', startBit: 8, bitLength: 4, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
    ]
  },
  {
    id: 0x102,
    name: 'VCLEFT_doorStatus',
    dlc: 8,
    signals: [
      { name: 'VCLEFT_frontLatchStatus', startBit: 0, bitLength: 2, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 }
    ]
  }
];

test('preprocesses can_msg_get with string message name', () => {
  const code = `can_msg_get("DAS_bodyControls", 0)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_get(0x3e9'));
  assert.equal(result.errors.length, 0);
});

test('preprocesses can_signal_get into __sig_get call', () => {
  const code = `can_signal_get("DAS_bodyControls", "DAS_hazardLightRequest")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('__sig_get'));
  assert(result.code.includes('0x3e9'));
  assert(result.code.includes('0, 8, false, false, 1, 0'));
  assert.equal(result.errors.length, 0);
});

test('preprocesses on_can_signal into __watch_sig call', () => {
  const code = `on_can_signal("DAS_bodyControls", "DAS_hazardLightRequest", my_fn)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('__watch_sig'));
  assert(result.code.includes('0x3e9'));
  assert(result.code.includes('my_fn'));
  assert.equal(result.errors.length, 0);
});

test('reports error for unknown message', () => {
  const code = `can_msg_get("UNKNOWN_MSG", 0)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('UNKNOWN_MSG'));
});

test('reports error for unknown signal', () => {
  const code = `can_signal_get("DAS_bodyControls", "UNKNOWN_SIG")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('UNKNOWN_SIG'));
});

test('replaces can_msg_set with signal_encode inline', () => {
  const code = `can_msg_set(msg, "DAS_hazardLightRequest", 1)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('signal_encode'));
  assert(result.code.includes('msg["data"] ='));
  assert.equal(result.errors.length, 0);
});

test('reports error for unknown signal in can_msg_set', () => {
  const code = `can_msg_set(msg, "GHOST_SIG", 1)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('GHOST_SIG'));
});

test('handles multiple replacements in one script', () => {
  const code = [
    'on_can_signal("DAS_bodyControls", "DAS_hazardLightRequest", fn)',
    'can_signal_get("DAS_bodyControls", "DAS_hazardLightRequest")',
    'can_msg_get("VCLEFT_doorStatus", 0)',
    'can_msg_set(msg, "DAS_hazardLightRequest", 42)',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('__watch_sig'));
  assert(result.code.includes('__sig_get'));
  assert(result.code.includes('can_msg_get(0x102'));
  assert(result.code.includes('signal_encode'));
  assert.equal(result.errors.length, 0);
});
