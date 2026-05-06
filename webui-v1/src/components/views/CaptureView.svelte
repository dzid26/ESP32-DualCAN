<script lang="ts">
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1">
  <SectionHead title="Capture & diff" sub="Event-triggered captures, baseline diffs, replay">
    {#snippet actions()}
      <span class="pill pill--warn">not yet implemented</span>
    {/snippet}
  </SectionHead>

  <div class="empty" style="margin-bottom: 10px">
    The firmware doesn't expose capture / diff / replay ops yet. Wireframe below shows the planned UI; the controls are inert.
    Use <a href="#" onclick={(e) => { e.preventDefault(); }} style="color: var(--dc-accent)">Trace</a> for raw frame logging in the meantime.
  </div>

  <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 10px; opacity: 0.6; pointer-events: none">
    <div class="frame">
      <div class="frame__head">Event capture</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
        <label class="field"><span>Trigger</span><input class="inp" placeholder="DriverDoorHandle" disabled /></label>
        <label class="field"><span>Condition</span><input class="inp" value="== 1" readonly /></label>
        <label class="field"><span>Duration</span><input class="inp" value="3000 ms" readonly /></label>
        <button class="btn btn--primary" style="align-self: flex-start" disabled><Icon name="capture" size={13} />Arm</button>
      </div>
    </div>

    <div class="frame">
      <div class="frame__head">Baseline diff</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
        <div class="row-flex">
          <span class="mono" style="color: var(--dc-info-text); font-weight: 700">A</span>
          <span class="ghost" style="flex: 1">—</span>
          <button class="btn btn--sm" disabled>Snap</button>
        </div>
        <div class="row-flex">
          <span class="mono" style="color: var(--dc-info-text); font-weight: 700">B</span>
          <span class="ghost" style="flex: 1">—</span>
          <button class="btn btn--sm" disabled>Snap</button>
        </div>
        <button class="btn btn--primary" style="align-self: flex-start" disabled>Show diff</button>
      </div>
    </div>

    <div class="frame">
      <div class="frame__head">Replay</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 6px">
        <span class="muted mono" style="font-size: 11px">no captures saved</span>
        <div class="row-flex" style="margin-top: 4px">
          <button class="btn btn--sm" disabled><Icon name="play" size={13} />Sim</button>
          <button class="btn btn--sm btn--danger" disabled><Icon name="play" size={13} />Live</button>
        </div>
      </div>
    </div>
  </div>
</div>

<style>
  .field {
    display: grid; grid-template-columns: 90px 1fr; gap: 8px;
    align-items: center; font-size: 12px; color: var(--dc-text-dim);
  }
</style>
