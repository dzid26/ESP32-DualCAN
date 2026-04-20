<script lang="ts">
  import type { Transport } from '../transport/types';
  import type { Protocol } from '../transport/protocol';
  import { parseDbc, compileDbc, type Message } from '../dbc/parser';

  let { transport, proto: _proto }: { transport: Transport; proto: Protocol } = $props();
  let messages = $state<Message[]>([]);
  let status = $state('No DBC loaded');

  function loadText(text: string, label: string) {
    messages = parseDbc(text);
    const totalSigs = messages.reduce((n, m) => n + m.signals.length, 0);
    status = `${label}: ${messages.length} messages, ${totalSigs} signals`;
  }

  function handleFile(event: Event) {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => loadText(reader.result as string, file.name);
    reader.readAsText(file);
  }

  async function loadTeslaDefaults() {
    status = 'Loading Tesla Model 3 vehicle DBC...';
    const resp = await fetch('/dbc/tesla_model3_vehicle.dbc');
    const text = await resp.text();
    loadText(text, 'Tesla Model 3 Vehicle');
  }

  async function upload(busId: number) {
    if (messages.length === 0) return;
    const binary = compileDbc(messages, busId);
    status = `Compiled ${binary.length} bytes for bus ${busId}`;
    if (transport.connected) {
      await transport.send(binary);
      status += ' — uploaded';
    } else {
      status += ' — not connected (compile-only)';
    }
  }
</script>

<div class="dbc">
  <h2>DBC</h2>
  <p>Load a DBC to define CAN signal definitions for a bus.</p>

  <div class="load-options">
    <button class="preset" onclick={loadTeslaDefaults}>Load Tesla Model 3 defaults</button>
    <span class="or">or</span>
    <input type="file" accept=".dbc" onchange={handleFile} />
  </div>

  <p class="status">{status}</p>

  {#if messages.length > 0}
    <div class="actions">
      <button onclick={() => upload(0)}>Compile &amp; Upload to Bus 0</button>
      <button onclick={() => upload(1)}>Compile &amp; Upload to Bus 1</button>
    </div>

    <div class="message-list">
      {#each messages as msg}
        <details>
          <summary>0x{msg.id.toString(16).toUpperCase().padStart(3, '0')} — {msg.name} ({msg.signals.length} signals)</summary>
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
  .load-options {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin: 0.75rem 0;
  }
  .or {
    color: #666;
    font-size: 0.85rem;
  }
  .preset {
    background: #2a3a4a;
    border: 1px solid #4a6a8a;
    color: #acd;
    padding: 0.4rem 1rem;
    border-radius: 4px;
    cursor: pointer;
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
  .message-list {
    max-height: 60vh;
    overflow-y: auto;
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
