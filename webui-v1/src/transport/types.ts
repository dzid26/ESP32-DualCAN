export interface Transport {
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  send(data: Uint8Array): Promise<void>;
  onReceive(cb: (data: Uint8Array) => void): void;
  readonly connected: boolean;
}
