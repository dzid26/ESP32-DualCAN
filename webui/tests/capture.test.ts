import test from 'node:test';
import assert from 'node:assert/strict';
import { evalCond, computeDiff } from '../src/lib/capture.ts';
import type { Snapshot } from '../src/lib/capture.ts';

// ---- evalCond ----

test('evalCond > passes when value is strictly greater', () => {
  assert.equal(evalCond(5, '>', 4), true);
  assert.equal(evalCond(4, '>', 4), false);
  assert.equal(evalCond(3, '>', 4), false);
});

test('evalCond < passes when value is strictly less', () => {
  assert.equal(evalCond(3, '<', 4), true);
  assert.equal(evalCond(4, '<', 4), false);
  assert.equal(evalCond(5, '<', 4), false);
});

test('evalCond >= passes when value is greater or equal', () => {
  assert.equal(evalCond(5, '>=', 4), true);
  assert.equal(evalCond(4, '>=', 4), true);
  assert.equal(evalCond(3, '>=', 4), false);
});

test('evalCond <= passes when value is less or equal', () => {
  assert.equal(evalCond(3, '<=', 4), true);
  assert.equal(evalCond(4, '<=', 4), true);
  assert.equal(evalCond(5, '<=', 4), false);
});

test('evalCond == passes within epsilon, rejects outside', () => {
  assert.equal(evalCond(4.0,      '==', 4),     true);
  assert.equal(evalCond(4.00005,  '==', 4),     true);  // within 0.0001
  assert.equal(evalCond(4.001,    '==', 4),     false);
  assert.equal(evalCond(0,        '==', 1),     false);
});

test('evalCond != rejects within epsilon, passes outside', () => {
  assert.equal(evalCond(4.0,   '!=', 4),  false);
  assert.equal(evalCond(4.001, '!=', 4),  true);
  assert.equal(evalCond(0,     '!=', 4),  true);
});

test('evalCond works with negative thresholds', () => {
  assert.equal(evalCond(-5, '<', -3), true);
  assert.equal(evalCond(-2, '>', -3), true);
  assert.equal(evalCond(-3, '==', -3), true);
});

// ---- computeDiff ----

function snap(vals: Record<string, number | null>): Snapshot {
  return { ts: '00:00:00', values: new Map(Object.entries(vals)) };
}

test('computeDiff returns empty when nothing changed', () => {
  const a = snap({ speed: 10, rpm: 1000 });
  const b = snap({ speed: 10, rpm: 1000 });
  assert.deepEqual(computeDiff(a, b), []);
});

test('computeDiff excludes changes below epsilon', () => {
  const a = snap({ speed: 10.00000 });
  const b = snap({ speed: 10.00005 }); // 0.00005 < 0.0001
  assert.deepEqual(computeDiff(a, b), []);
});

test('computeDiff returns changed signals with correct delta', () => {
  const a = snap({ speed: 10, rpm: 1000, steer: 0 });
  const b = snap({ speed: 15, rpm: 1000, steer: -5 });
  const rows = computeDiff(a, b);
  assert.equal(rows.length, 2);
  const names = rows.map(r => r.name);
  assert.ok(names.includes('speed'));
  assert.ok(names.includes('steer'));
  const speedRow = rows.find(r => r.name === 'speed')!;
  assert.equal(speedRow.a, 10);
  assert.equal(speedRow.b, 15);
  assert.equal(speedRow.delta, 5);
});

test('computeDiff sorts by |delta| descending', () => {
  const a = snap({ a: 0, b: 0, c: 0 });
  const b = snap({ a: 1, b: 10, c: 5 });
  const rows = computeDiff(a, b);
  assert.deepEqual(rows.map(r => r.name), ['b', 'c', 'a']);
});

test('computeDiff skips signals where either snapshot has null', () => {
  const a = snap({ good: 10, missing: null });
  const b = snap({ good: 20, missing: 5 });
  const rows = computeDiff(a, b);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].name, 'good');
});

test('computeDiff skips signals not present in snapshot B', () => {
  const a = snap({ x: 1, y: 2 });
  const b = snap({ x: 3 }); // y absent
  const rows = computeDiff(a, b);
  assert.equal(rows.length, 1);
  assert.equal(rows[0].name, 'x');
});
