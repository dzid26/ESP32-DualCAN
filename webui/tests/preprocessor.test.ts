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
  },
  {
    id: 0x103,
    name: 'VCRIGHT_doorStatus',
    dlc: 8,
    signals: [
      { name: 'VCRIGHT_frontHandlePulled', startBit: 10, bitLength: 1, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 }
    ]
  }
];

test('preprocesses can_msg_get with bus-first order', () => {
  const code = `can_msg_get(0, "DAS_bodyControls")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_get(0, 0x3e9'));
  assert.equal(result.errors.length, 0);
});

test('name-first can_msg_get passes through unchanged', () => {
  const code = `can_msg_get("DAS_bodyControls", 0)`;
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.code, code);
  assert.equal(result.errors.length, 0);
});

test('inlines msg_sig_get into msg_sig_get with inline metadata', () => {
  const code = `var sig = msg_sig_get(msg, "DAS_hazardLightRequest")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('msg_sig_get(msg, 0, 8, false, false, 1, 0)'));
  assert.equal(result.errors.length, 0);
});

test('reports error for unknown message in can_msg_get', () => {
  const code = `can_msg_get(0, "UNKNOWN_MSG")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('UNKNOWN_MSG'));
});

test('reports error for unknown signal in msg_sig_get', () => {
  const code = `var x = msg_sig_get(msg, "UNKNOWN_SIG")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('UNKNOWN_SIG'));
});

test('reports error for unknown signal in msg_sig_set', () => {
  const code = `msg_sig_set(msg, "GHOST_SIG", 1)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('GHOST_SIG'));
});

test('replaces msg_sig_set with msg_sig_set with inline metadata', () => {
  const code = `msg_sig_set(msg, "DAS_hazardLightRequest", 1)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('msg_sig_set(msg, 0, 8, false, false, 1, 0, 1)'));
  assert.equal(result.errors.length, 0);
});

test('preprocesses can_msg_new with string message name', () => {
  const code = `var msg = can_msg_new("DAS_bodyControls", 0)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_new(0x3e9, 8)'));
  assert.equal(result.errors.length, 0);
});

test('preprocesses can_msg_new without bus', () => {
  const code = `var msg = can_msg_new("VCLEFT_doorStatus")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_new(0x102, 8)'));
  assert.equal(result.errors.length, 0);
});

test('passes can_msg_new with numeric id through unchanged', () => {
  const code = `var msg = can_msg_new(0x3E9, 8)`;
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.code, code);
  assert.equal(result.errors.length, 0);
});

test('reports error for unknown message in can_msg_new', () => {
  const code = `can_msg_new("UNKNOWN_MSG")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes('UNKNOWN_MSG'));
});

test('can_msg_send passes through to native binding', () => {
  const code = [
    'var msg = can_msg_get(0, "DAS_bodyControls")',
    'msg_sig_set(msg, "DAS_hazardLightRequest", 1)',
    'can_msg_send(0, msg)',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_send(0, msg)'), 'can_msg_send should pass through unchanged');
  assert(!result.code.includes('can_send_raw'), 'should NOT rewrite to can_send_raw');
  assert(result.code.includes('msg_sig_set('));
  assert.equal(result.errors.length, 0);
});

test('handles multiple replacements in one script', () => {
  const code = [
    'var msg = can_msg_get(0, "DAS_bodyControls")',
    'msg_sig_set(msg, "DAS_hazardLightRequest", 42)',
    'can_msg_send(0, msg)',
    'var sig = msg_sig_get(msg, "DAS_hazardLightRequest")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_get(0, 0x3e9'));
  assert(result.code.includes('can_msg_send(0, msg)'), 'can_msg_send passes through');
  assert(!result.code.includes('can_send_raw'), 'should NOT rewrite to can_send_raw');
  assert(result.code.includes('msg_sig_set(msg, 0, 8, false, false, 1, 0, 42)'));
  assert(result.code.includes('msg_sig_get(msg, 0, 8, false, false, 1, 0)'));
  assert.equal(result.errors.length, 0);
});

test('errors when signal does not belong to the tracked message (msg_sig_get)', () => {
  const code = `var msg_r = can_msg_get(0, "VCRIGHT_doorStatus")\nmsg_sig_get(msg_r, "VCLEFT_frontLatchStatus")`;
  const result = preprocessScript(code, mockMessages);
  const crossErr = result.errors.find(e => e.includes("belongs to"));
  assert(crossErr, `expected a cross-message error, got: ${JSON.stringify(result.errors)}`);
  assert(crossErr.includes("VCLEFT_doorStatus"));
  assert(crossErr.includes("VCRIGHT_doorStatus"));
});

test('errors when signal does not belong to the tracked message (msg_sig_set)', () => {
  const code = `var msg = can_msg_get(0, "VCLEFT_doorStatus")\nmsg_sig_set(msg, "DAS_hazardLightRequest", 1)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.length > 0);
  assert(result.errors[0].includes("belongs to 'DAS_bodyControls'"));
  assert(result.errors[0].includes("not 'VCLEFT_doorStatus'"));
});

test('allows signal that belongs to the tracked message', () => {
  const code = `var msg = can_msg_get(0, "VCLEFT_doorStatus")\nmsg_sig_get(msg, "VCLEFT_frontLatchStatus")`;
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert(result.code.includes('msg_sig_get(msg, 0, 2, false, false, 1, 0)'));
});

test('allows signal from untracked variable (no assignment seen) — falls back to global search', () => {
  const code = `msg_sig_get(someParam, "DAS_hazardLightRequest")`;
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert(result.code.includes('msg_sig_get(someParam, 0, 8, false, false, 1, 0)'));
});

test('allows signal from variable assigned to multiple message types — falls back to global search', () => {
  const code = [
    'var msg = can_msg_get(0, "DAS_bodyControls")',
    'msg = can_msg_get(0, "VCLEFT_doorStatus")',
    'msg_sig_get(msg, "DAS_hazardLightRequest")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert(result.code.includes('msg_sig_get(msg, 0, 8, false, false, 1, 0)'));
});
