/**
 * Reactive store for the currently parsed DBC, shared between the DBC view
 * (which loads it) and the Dashboard view (which autocompletes signals from
 * it). Holds the parsed signal list along with the bus the user last
 * uploaded it to, so Signal Watch can default to the right bus.
 */

interface DbcSignalRef {
  name: string;
  message: string;     // owning message name, used for grouping
  msgId: number;
}

class DbcStore {
  signals = $state<DbcSignalRef[]>([]);
  /** Bus the most recent compile-and-upload targeted (-1 if never uploaded). */
  lastUploadedBus = $state<number>(-1);

  setSignals(signals: DbcSignalRef[]) { this.signals = signals; }
  setLastBus(bus: number)             { this.lastUploadedBus = bus; }
  clear() { this.signals = []; this.lastUploadedBus = -1; }
}

export const dbcStore = new DbcStore();
export type { DbcSignalRef };
