export type Cond = '>' | '<' | '>=' | '<=' | '==' | '!=';
export const CONDS: Cond[] = ['>', '<', '>=', '<=', '==', '!='];

export function evalCond(value: number, cond: Cond, threshold: number): boolean {
  switch (cond) {
    case '>':  return value > threshold;
    case '<':  return value < threshold;
    case '>=': return value >= threshold;
    case '<=': return value <= threshold;
    case '==': return Math.abs(value - threshold) < 0.0001;
    case '!=': return Math.abs(value - threshold) >= 0.0001;
  }
}

export type Snapshot = { ts: string; values: Map<string, number | null> };

export type DiffRow = { name: string; a: number; b: number; delta: number };

export function computeDiff(snapA: Snapshot, snapB: Snapshot): DiffRow[] {
  const rows: DiffRow[] = [];
  for (const [name, aVal] of snapA.values) {
    const bVal = snapB.values.get(name) ?? null;
    if (aVal === null || bVal === null) continue;
    const delta = bVal - aVal;
    if (Math.abs(delta) > 0.0001) rows.push({ name, a: aVal, b: bVal, delta });
  }
  return rows.sort((a, b) => Math.abs(b.delta) - Math.abs(a.delta));
}
