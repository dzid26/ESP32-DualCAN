<script lang="ts">
  import SectionHead from '../SectionHead.svelte';
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1">
  <SectionHead title="Tesla-BLE" sub="Optional secondary channel — works when the car is asleep">
    {#snippet actions()}
      <span class="pill pill--warn">not yet implemented</span>
    {/snippet}
  </SectionHead>

  <div class="empty" style="margin-bottom: 10px">
    Tesla-BLE pairing isn't wired in firmware yet. The form below is a wireframe for the planned flow (Phase 3 in <span class="mono">firmware/ROADMAP.md</span>).
  </div>

  <div class="frame" style="opacity: 0.6; pointer-events: none">
    <div class="frame__head">Pairing</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
      <label class="field"><span>Keypair</span><span class="mono ghost">not generated</span></label>
      <label class="field"><span>VIN</span><input class="inp" placeholder="5YJ3E…" disabled /></label>
      <button class="btn btn--primary" style="align-self: flex-start" disabled>Request "Add key"</button>
      <p class="muted" style="font-size: 11px; margin: 0">
        Approve with an existing phone key or keycard inside the car.
      </p>
    </div>
  </div>
</div>

<style>
  .field {
    display: grid; grid-template-columns: 90px 1fr; gap: 8px;
    align-items: center; font-size: 12px; color: var(--dc-text-dim);
  }
</style>
