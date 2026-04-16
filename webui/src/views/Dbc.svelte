<script lang="ts">
  import type { Transport } from '../transport/types';
  import { parseDbc, compileDbc, type Message } from '../dbc/parser';

  let { transport }: { transport: Transport } = $props();
  let messages = $state<Message[]>([]);
  let status = $state('No DBC loaded');

  function handleFile(event: Event) {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = () => {
      const text = reader.result as string;
      messages = parseDbc(text);
      const totalSigs = messages.reduce((n, m) => n + m.signals.length, 0);
      status = `Parsed ${messages.length} messages, ${totalSigs} signals`;
    };
    reader.readAsText(file);
  }

  async function upload(busId: number) {
    if (messages.length === 0) return;
    const binary = compileDbc(messages, busId);
    status = `Compiled ${binary.length} bytes for bus ${busId}`;
    if (transport.connected) {
      // TODO: send via CBOR dbc.upload topic
      await transport.send(binary);
      status += ' — uploaded';
    } else {
      status += ' — not connected';
    }
  }
</script>

<div class="dbc">
  <h2>DBC</h2>
  <p>Upload a .dbc file to define CAN signals for a bus.</p>

  <input type="file" accept=".dbc" onchange={handleFile} />
  <p class="status">{status}</p>

  {#if messages.length > 0}
    <div class="actions">
      <button onclick={() => upload(0)}>Upload to Bus 0</button>
      <button onclick={() => upload(1)}>Upload to Bus 1</button>
    </div>

    <div class="message-list">
      {#each messages as msg}
        <details>
          <summary>0x{msg.id.toString(16).toUpperCase()} — {msg.name} ({msg.signals.length} signals)</summary>
          <ul>
            {#each msg.signals as sig}
              <li>{sig.name} [{sig.startBit}|{sig.bitLength}] scale={sig.scale} offset={sig.offset}</li>
            {/each}
          </ul>
        </details>
      {/each}
    </div>
  {/if}
</div>

<style>
  input[type="file"] {
    margin: 0.5rem 0;
  }
  .status {
    color: #888;
    font-size: 0.85rem;
  }
  .actions {
    display: flex;
    gap: 0.5rem;
    margin: 0.5rem 0;
  }
  .actions button {
    background: #2a2a4a;
    border: 1px solid #444;
    color: #ccc;
    padding: 0.4rem 1rem;
    border-radius: 4px;
    cursor: pointer;
  }
  details {
    background: #0d0d1a;
    border: 1px solid #333;
    border-radius: 4px;
    margin: 0.25rem 0;
    padding: 0.5rem;
  }
  summary {
    cursor: pointer;
    font-family: monospace;
  }
  ul {
    font-size: 0.85rem;
    font-family: monospace;
    color: #aaa;
  }
</style>
