<script lang="ts">
  import type { Transport } from '../transport/types';
  import type { Protocol } from '../transport/protocol';

  let { transport, proto: _proto }: { transport: Transport; proto: Protocol } = $props();
</script>

<div class="dashboard">
  <h2>Dashboard</h2>
  <p>Live signal values and graphs will appear here once connected.</p>

  {#if !transport.connected}
    <div class="placeholder">
      Connect to the device via BLE to see live data.
    </div>
  {:else}
    <div class="placeholder">
      Signal watch list — coming soon.
    </div>
  {/if}
</div>

<style>
  .placeholder {
    background: #0d0d1a;
    border: 1px dashed #333;
    border-radius: 4px;
    padding: 2rem;
    text-align: center;
    color: #666;
    margin-top: 1rem;
  }
</style>
