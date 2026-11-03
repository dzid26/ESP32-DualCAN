import test from 'node:test';
import assert from 'node:assert/strict';
import { preprocessScript, buildVarToMsgMap, trackVarToMsgs } from '../src/lib/preprocessor.ts';

const mockMessages = [
  {
    id: 0x3e9,
    name: 'DAS_bodyControls',
    dlc: 8,
    signals: [
      { name: 'DAS_hazardLightRequest', startBit: 0, bitLength: 8, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
      { name: 'DAS_wipersControl', startBit: 8, bitLength: 4, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
      { name: 'AMBIG_dup', startBit: 12, bitLength: 1, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
    ]
  },
  {
    id: 0x102,
    name: 'VCLEFT_doorStatus',
    dlc: 8,
    signals: [
      { name: 'VCLEFT_frontLatchStatus', startBit: 0, bitLength: 2, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
      { name: 'AMBIG_dup', startBit: 4, bitLength: 1, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 },
    ]
  },
  {
    id: 0x103,
    name: 'VCRIGHT_doorStatus',
    dlc: 8,
    signals: [
      { name: 'VCRIGHT_frontHandlePulled', startBit: 10, bitLength: 1, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 }
    ]
  },
  {
    id: 0x334,
    name: 'UI_powertrainControl',
    dlc: 8,
    signals: [
      { name: 'UI_powertrainControlCounter', startBit: 52, bitLength: 4, isBigEndian: false, isSigned: false, scale: 1, offset: 0, muxType: 0, muxValue: 0 }
    ]
  }
];

test('preprocesses can_msg_get with bus-first order', () => {
  const code = `can_msg_get(0, "DAS_bodyControls")`;
  const result = preprocessScript(code, mockMessages);
  assert(result.code.includes('can_msg_get(0, 0x3e9'));
  assert.equal(result.errors.length, 0);
});

test('errors on name-first can_msg_get', () => {
  const code = `can_msg_get("DAS_bodyControls", 0)`;
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('bus first')), `got: ${JSON.stringify(result.errors)}`);
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

/* ---- buildVarToMsgMap / trackVarToMsgs ---- */

test('buildVarToMsgMap tracks a simple assignment', () => {
  const code = 'var msg_r = can_msg_get(0, "VCRIGHT_doorStatus")';
  const map = buildVarToMsgMap(code);
  assert.equal(map.get('msg_r'), 'VCRIGHT_doorStatus');
  assert.equal(map.size, 1);
});

test('buildVarToMsgMap excludes variables with multiple message types', () => {
  const code = [
    'var msg = can_msg_get(0, "DAS_bodyControls")',
    'msg = can_msg_get(0, "VCLEFT_doorStatus")',
  ].join('\n');
  const map = buildVarToMsgMap(code);
  assert(!map.has('msg'), 'msg should be excluded (assigned to 2 messages)');
});

test('buildVarToMsgMap tracks can_msg_new assignment', () => {
  const code = 'var m = can_msg_new("UI_vehicleControl3")';
  const map = buildVarToMsgMap(code);
  assert.equal(map.get('m'), 'UI_vehicleControl3');
});

test('buildVarToMsgMap tracks across multiple lines', () => {
  const code = [
    'var msg_l = can_msg_get(0, "VCLEFT_doorStatus")',
    'var msg_r = can_msg_get(0, "VCRIGHT_doorStatus")',
    'var m = can_msg_new("UI_vehicleControl3")',
  ].join('\n');
  const map = buildVarToMsgMap(code);
  assert.equal(map.get('msg_l'), 'VCLEFT_doorStatus');
  assert.equal(map.get('msg_r'), 'VCRIGHT_doorStatus');
  assert.equal(map.get('m'), 'UI_vehicleControl3');
  assert.equal(map.size, 3);
});

test('buildVarToMsgMap does not false-match msg_sig_get', () => {
  const code = 'msg_sig_get(msg_r, "VCLEFT_frontLatchStatus")';
  const map = buildVarToMsgMap(code);
  assert.equal(map.size, 0);
});

test('buildVarToMsgMap does not false-match == comparison', () => {
  const code = 'if msg == nil msg = can_msg_new("DAS_bodyControls", 0) end';
  const map = buildVarToMsgMap(code);
  assert.equal(map.get('msg'), 'DAS_bodyControls', 'should match actual assignment but not ==');
});

/* ---- Nested / balanced DBC-aware calls ---- */

test('nested msg_sig_get resolves via the tracked draft message', () => {
  const code = [
    'var msg = can_msg_new("UI_powertrainControl")',
    'msg_sig_set(msg, "UI_powertrainControlCounter", (int(msg_sig_get(msg, "UI_powertrainControlCounter")) + 1) % 16)',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0, result.errors.join('; '));
  assert(
    result.code.includes('msg_sig_set(msg, 52, 4, false, false, 1, 0, (int(msg_sig_get(msg, 52, 4, false, false, 1, 0)) + 1) % 16)'),
    `got: ${result.code}`,
  );
});

test('balanced value matching keeps commas inside a nested call', () => {
  const code = 'msg_sig_set(msg, "UI_powertrainControlCounter", max(1, 2) % 16)';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0, result.errors.join('; '));
  assert.equal(result.code, 'msg_sig_set(msg, 52, 4, false, false, 1, 0, max(1, 2) % 16)');
});

test('reports an unresolved nested msg_sig_get instead of emitting broken code', () => {
  const code = 'msg_sig_set(msg, "UI_powertrainControlCounter", int(msg_sig_get(msg, "GHOST_SIG")) + 1)';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('GHOST_SIG')), `got: ${JSON.stringify(result.errors)}`);
  assert(result.code.includes('raise("key_error"'), `got: ${result.code}`);
});

test('reports unbalanced parentheses rather than silently mangling the call', () => {
  const code = 'msg_sig_set(msg, "UI_powertrainControlCounter", (1 + 2';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('unbalanced')), `got: ${JSON.stringify(result.errors)}`);
  assert(!result.code.includes('52, 4'), 'must not half-rewrite an unparseable call');
});

/* ---- Ambiguous name resolution ---- */

test('warns (does not fail) when an untracked name matches signals in multiple messages', () => {
  const code = 'msg_sig_get(someParam, "AMBIG_dup")';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.warnings.length, 1, `got: ${JSON.stringify(result.warnings)}`);
  assert(result.warnings[0].includes('AMBIG_dup'));
  assert(result.warnings[0].includes('DAS_bodyControls'));
  assert(result.warnings[0].includes('VCLEFT_doorStatus'));
  // still rewrites using the first match so the script remains usable
  assert(result.code.includes('msg_sig_get(someParam, 12, 1, false, false, 1, 0)'));
});

test('does not warn for an unambiguously tracked draft', () => {
  const code = [
    'var msg = can_msg_get(0, "VCLEFT_doorStatus")',
    'msg_sig_get(msg, "VCLEFT_frontLatchStatus")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.warnings.length, 0);
});

test('errors when a tracked draft lacks the signal but several messages have it', () => {
  const code = 'var msg = can_msg_get(0, "VCRIGHT_doorStatus")\nmsg_sig_get(msg, "AMBIG_dup")';
  const result = preprocessScript(code, mockMessages);
  const err = result.errors.find(e => e.includes('AMBIG_dup'));
  assert(err, `expected an error, got: ${JSON.stringify(result.errors)}`);
  assert(err.includes('multiple messages'), `got: ${err}`);
  assert(err.includes('DAS_bodyControls'));
  assert(err.includes('VCLEFT_doorStatus'));
});

/* ---- Comment / string awareness ---- */

test('keeps the closing paren after a trailing comment', () => {
  const code = [
    'msg_sig_set(msg, "UI_powertrainControlCounter",',
    '  1',
    '  # note',
    '  )',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0, result.errors.join('; '));
  assert(result.code.endsWith('# note\n  )'), `got: ${JSON.stringify(result.code)}`);
});

test('ignores Berry block comments inside a call', () => {
  const code = 'msg_sig_set(msg, "UI_powertrainControlCounter", 1 #- note ) -# + 1)';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0, result.errors.join('; '));
  assert.equal(result.code, 'msg_sig_set(msg, 52, 4, false, false, 1, 0, 1 #- note ) -# + 1)');
});

test('does not rewrite call-shaped text inside strings', () => {
  const code = `print("use msg_sig_get(d, 'DAS_hazardLightRequest') to read")`;
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.code, code);
  assert.equal(result.errors.length, 0);
});

test('does not rewrite call-shaped text inside comments', () => {
  const code = '# msg_sig_get(d, "DAS_hazardLightRequest")\nx = 1  # msg_sig_set(d, "DAS_hazardLightRequest", 0)\nprint("ok")';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.code, code);
  assert.equal(result.errors.length, 0);
});

test('continues rewriting after an unbalanced call', () => {
  const code = [
    'msg_sig_set(msg, "UI_powertrainControlCounter", (1 + 2',
    'msg_sig_set(msg, "UI_powertrainControlCounter", 3)',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('unbalanced')), `got: ${JSON.stringify(result.errors)}`);
  assert(result.code.includes('msg_sig_set(msg, 52, 4, false, false, 1, 0, 3)'), `got: ${result.code}`);
});

/* ---- Unsupported shapes are reported, not passed through ---- */

test('errors on msg_sig_set with the wrong argument count', () => {
  const code = 'msg_sig_set(msg, "UI_powertrainControlCounter", 1, 2)';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('expected 3 arguments')), `got: ${JSON.stringify(result.errors)}`);
  assert(result.code.includes('raise("key_error"'));
});

test('errors on msg_sig_get with the wrong argument count', () => {
  const code = 'msg_sig_get(msg, "DAS_hazardLightRequest", 1)';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('expected 2 arguments')), `got: ${JSON.stringify(result.errors)}`);
});

test('errors when a signal name is not a string literal', () => {
  const code = 'msg_sig_get(msg, sigVar)';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('string literal')), `got: ${JSON.stringify(result.errors)}`);
});

test('handles escaped quotes in a signal literal', () => {
  const code = 'msg_sig_get(msg, "OG\\"SIG")';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('OG"SIG')), `got: ${JSON.stringify(result.errors)}`);
});

test('errors on a computed signal name', () => {
  const code = 'msg_sig_get(msg, "UI_" .. "counter")';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('compile-time string literal')), `got: ${JSON.stringify(result.errors)}`);
});

test('errors on a computed message name', () => {
  const code = 'can_msg_get(0, "DAS" .. "_bodyControls")';
  const result = preprocessScript(code, mockMessages);
  assert(result.errors.some(e => e.includes('compile-time string literal')), `got: ${JSON.stringify(result.errors)}`);
});

test('leaves numeric-id can_msg_get unchanged', () => {
  const code = 'can_msg_get(0, msgIdVar)';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.code, code);
  assert.equal(result.errors.length, 0);
});

test('leaves already-inlined low-level calls unchanged', () => {
  const code = [
    'msg_sig_get(msg, 0, 2, false, false, 1, 0)',
    'msg_sig_set(msg, 0, 2, false, false, 1, 0, 1)',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.code, code);
  assert.equal(result.errors.length, 0);
});

/* ---- Message resolution forms ---- */

test('resolves can_msg_get with a non-literal bus', () => {
  const code = 'can_msg_get(bus, "DAS_bodyControls")';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.code, 'can_msg_get(bus, 0x3e9)');
});

test('resolves bus-first can_msg_new', () => {
  const code = 'can_msg_new(0, "DAS_bodyControls")';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.code, 'can_msg_new(0x3e9, 8)');
});

test('still rewrites a nested call inside an unrecognised call', () => {
  const code = 'var a = can_msg_get(0, can_msg_new("DAS_bodyControls"))';
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert(result.code.includes('can_msg_new(0x3e9, 8)'), `got: ${result.code}`);
});

test('errors when an inline draft lacks the signal', () => {
  const code = 'msg_sig_get(can_msg_get(0, "VCRIGHT_doorStatus"), "DAS_hazardLightRequest")';
  const result = preprocessScript(code, mockMessages);
  const err = result.errors.find(e => e.includes('DAS_hazardLightRequest'));
  assert(err, `expected an error, got: ${JSON.stringify(result.errors)}`);
  assert(err.includes("belongs to 'DAS_bodyControls', not 'VCRIGHT_doorStatus'"), `got: ${err}`);
});

test('tracks a draft created with a non-literal bus', () => {
  const code = [
    'var d = can_msg_get(bus, "VCLEFT_doorStatus")',
    'msg_sig_get(d, "AMBIG_dup")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.warnings.length, 0);
  assert(result.code.includes('msg_sig_get(d, 4, 1, false, false, 1, 0)'), `got: ${result.code}`);
});

test('tracks a bus-first can_msg_new draft', () => {
  const code = [
    'var d = can_msg_new(0, "VCLEFT_doorStatus")',
    'msg_sig_get(d, "AMBIG_dup")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.warnings.length, 0);
  assert(result.code.includes('msg_sig_get(d, 4, 1, false, false, 1, 0)'), `got: ${result.code}`);
});

test('tracks a draft whose bus is a call', () => {
  const code = [
    'var d = can_msg_get(getBus(), "VCLEFT_doorStatus")',
    'msg_sig_get(d, "AMBIG_dup")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.warnings.length, 0);
  assert(result.code.includes('msg_sig_get(d, 4, 1, false, false, 1, 0)'), `got: ${result.code}`);
});

test('tracks a can_msg_new draft whose bus is a call', () => {
  const code = [
    'var d = can_msg_new(getBus(), "VCLEFT_doorStatus")',
    'msg_sig_get(d, "AMBIG_dup")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.errors.length, 0);
  assert.equal(result.warnings.length, 0);
  assert(result.code.includes('msg_sig_get(d, 4, 1, false, false, 1, 0)'), `got: ${result.code}`);
});

/* ---- Warning dedupe ---- */

test('deduplicates identical ambiguity warnings', () => {
  const code = [
    'msg_sig_get(p, "AMBIG_dup")',
    'msg_sig_get(p, "AMBIG_dup")',
  ].join('\n');
  const result = preprocessScript(code, mockMessages);
  assert.equal(result.warnings.length, 1, `got: ${JSON.stringify(result.warnings)}`);
});

/* ---- Table-driven call matrix ---- */

const callMatrix: Array<{ name: string; source: string; code: string }> = [
  {
    name: 'can_msg_get with a literal bus',
    source: 'can_msg_get(0, "DAS_bodyControls")',
    code: 'can_msg_get(0, 0x3e9)',
  },
  {
    name: 'can_msg_get with a non-literal bus',
    source: 'can_msg_get(bus, "DAS_bodyControls")',
    code: 'can_msg_get(bus, 0x3e9)',
  },
  {
    name: 'can_msg_new bus-first',
    source: 'can_msg_new(0, "DAS_bodyControls")',
    code: 'can_msg_new(0x3e9, 8)',
  },
  {
    name: 'msg_sig_get from a tracked draft',
    source: 'var d = can_msg_get(0, "VCLEFT_doorStatus")\nmsg_sig_get(d, "VCLEFT_frontLatchStatus")',
    code: 'var d = can_msg_get(0, 0x102)\nmsg_sig_get(d, 0, 2, false, false, 1, 0)',
  },
  {
    name: 'msg_sig_get from an inline draft',
    source: 'msg_sig_get(can_msg_get(0, "VCLEFT_doorStatus"), "AMBIG_dup")',
    code: 'msg_sig_get(can_msg_get(0, 0x102), 4, 1, false, false, 1, 0)',
  },
  {
    name: 'msg_sig_set with a plain value',
    source: 'msg_sig_set(msg, "UI_powertrainControlCounter", 1)',
    code: 'msg_sig_set(msg, 52, 4, false, false, 1, 0, 1)',
  },
  {
    name: 'msg_sig_set with a nested get',
    source:
      'msg_sig_set(msg, "UI_powertrainControlCounter", (int(msg_sig_get(msg, "UI_powertrainControlCounter")) + 1) % 16)',
    code:
      'msg_sig_set(msg, 52, 4, false, false, 1, 0, (int(msg_sig_get(msg, 52, 4, false, false, 1, 0)) + 1) % 16)',
  },
  {
    name: 'comment inside a call',
    source: `msg_sig_set(msg, "UI_powertrainControlCounter", 1  # note (and don't\n)`,
    code: `msg_sig_set(msg, 52, 4, false, false, 1, 0, 1  # note (and don't\n)`,
  },
  {
    name: 'call-shaped text inside a string',
    source: `print("msg_sig_get(d, 'DAS_hazardLightRequest')")`,
    code: `print("msg_sig_get(d, 'DAS_hazardLightRequest')")`,
  },
];

for (const c of callMatrix) {
  test(`matrix: ${c.name}`, () => {
    const result = preprocessScript(c.source, mockMessages);
    assert.equal(result.errors.length, 0, `got: ${JSON.stringify(result.errors)}`);
    assert.equal(result.warnings.length, 0, `got: ${JSON.stringify(result.warnings)}`);
    assert.equal(result.code, c.code);
  });
}
