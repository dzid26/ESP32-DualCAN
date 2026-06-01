/**
 * Reactive store for the currently parsed DBC, shared between the DBC view
 * (which loads it) and the Dashboard / Scripts views that use it for
 * autocomplete and preprocessing.
 */

import type { Message } from './dbc/parser';

interface DbcSignalRef {
  name: string;
  message: string;     // owning message name, used for grouping
  msgId: number;
  sigIndex: number;    // index within the owning message
}

interface DbcMessageRef {
  name: string;
  id: number;
}

class DbcStore {
  signals = $state.raw<DbcSignalRef[]>([]);
  messages = $state.raw<DbcMessageRef[]>([]);
  /** Full parsed messages per bus, including signal metadata (startBit, bitLength, etc.). */
  fullMessages = $state.raw<Record<number, Message[]>>({});
  setSignals(signals: DbcSignalRef[]) { this.signals = signals; }
  setMessages(messages: DbcMessageRef[]) { this.messages = messages; }
  setFullMessages(busMessages: Record<number, Message[]>) { this.fullMessages = busMessages; }
  clear() { this.signals = []; this.messages = []; this.fullMessages = {}; }
}

export const dbcStore = new DbcStore();
export type { DbcMessageRef, DbcSignalRef };
