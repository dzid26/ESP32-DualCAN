<script lang="ts">
  import type { Transport } from '../transport/types';

  let { transport }: { transport: Transport } = $props();
  let code = $state('# Write your Berry script here\ndef setup()\n  print("hello")\nend\n');
</script>

<div class="scripts">
  <h2>Scripts</h2>
  <p>Edit and deploy Berry automation scripts to the device.</p>

  <textarea bind:value={code} rows="16" spellcheck="false"></textarea>

  <div class="actions">
    <button disabled={!transport.connected}>Upload & Enable</button>
    <button disabled={!transport.connected}>Disable</button>
  </div>
</div>

<style>
  textarea {
    width: 100%;
    font-family: 'Fira Code', 'Cascadia Code', monospace;
    font-size: 0.9rem;
    background: #0d0d1a;
    color: #e0e0e0;
    border: 1px solid #333;
    border-radius: 4px;
    padding: 0.75rem;
    resize: vertical;
  }
  .actions {
    display: flex;
    gap: 0.5rem;
    margin-top: 0.5rem;
  }
  .actions button {
    background: #2a4a2a;
    border: 1px solid #4a4;
    color: #ccc;
    padding: 0.4rem 1rem;
    border-radius: 4px;
    cursor: pointer;
  }
  .actions button:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
</style>
