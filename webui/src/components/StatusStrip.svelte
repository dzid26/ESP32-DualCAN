<script lang="ts">
  import { app } from '../lib/store.svelte';
  import { isMobile, modKey } from '../lib/platform';
  import Icon from './Icon.svelte';
  import FilledIcon from './FilledIcon.svelte';
  import VersionPip from './VersionPip.svelte';
  import BusPip from './BusPip.svelte';
  import CarPip from './CarPip.svelte';
  import CpuPip from './CpuPip.svelte';
  import { onMount } from 'svelte';

  let { onPalette }: { onPalette: () => void } = $props();

  let latestStableVersion: string | null = $state(null);
  let confirmDisconn = $state(false);

  let statusEl: HTMLDivElement | null = $state(null);
  let flowEl: HTMLDivElement | null = $state(null);
  let githubEl: HTMLAnchorElement | null = $state(null);

  const GAP = 6;
  // Right-aligned items; the first one on a row is pushed to the right edge and
  // the rest hug it. Everything else stays left-aligned.
  const RIGHT_ALIGNED = new Set(['version', 'simkill', 'palette']);
  // How strongly an item claims row 0; each item declares it in the markup and
  // the lowest scores are the first to be demoted to a lower row.
  const row0Claim = (el: HTMLElement) => Number(el.dataset.row0 ?? 0);
  // Once the strip folds into three or more rows, items that declare a fold order
  // leave row 0 and take the overflow rows in ascending order (ties keep DOM
  // order). Unmarked items default to the middle. This lets the wide bus group
  // claim the first overflow row while the narrow car picker drops below it.
  const MID_FOLD = 50;
  const foldOrder = (el: HTMLElement) =>
    el.dataset.foldOrder ? Number(el.dataset.foldOrder) : MID_FOLD;

  function handleConnClick() {
    if (app.connected) { confirmDisconn = true; return; }
    if (app.connecting || app.reconnecting) return;
    app.toggleConnect();
  }

  const connState = $derived(
    app.connecting || app.reconnecting ? 'connecting' :
    app.connected ? 'ok' : 'err'
  );
  const stateLabel = $derived(
    app.reconnecting ? 'Reconnecting…' :
    app.connecting ? 'Connecting…' :
    app.connected
      ? (app.transport === 'ws' ? 'WiFi · dorky.local' : `BLE · ${app.deviceName ?? 'Dorky'}`)
      : 'Not connected'
  );
  const dotClass = $derived(
    connState === 'ok' ? 'pip__dot--ok' :
    connState === 'err' ? 'pip__dot--err' : 'pip__dot--warn'
  );

  const fwLatestStatus = $derived(
    app.protoMismatch ? 'warning' :
    !latestStableVersion || !app.fwVersion ? true :
    compareVersions(app.fwVersion, latestStableVersion) >= 0
  );

  function compareVersions(current: string, latest: string): number {
    const parse = (v: string) => {
      const match = v.match(/v?(\d+)\.(\d+)\.(\d+)/);
      return match ? [parseInt(match[1]), parseInt(match[2]), parseInt(match[3])] : [0, 0, 0];
    };
    const [cm, cn, cp] = parse(current);
    const [lm, ln, lp] = parse(latest);
    return cm !== lm ? cm - lm : cn !== ln ? cn - ln : cp - lp;
  }

  async function checkLatestStable() {
    try {
      const resp = await fetch('https://api.github.com/repos/dzid26/ESP32-DualCAN/releases');
      if (!resp.ok) return;
      const releases: any[] = await resp.json();
      const stable = releases.find((r: any) => !r.prerelease);
      if (stable) latestStableVersion = stable.tag_name;
    } catch {
      /* ignore */
    }
  }

  function itemOf(el: Element): string {
    return (el as HTMLElement).dataset.item ?? '';
  }

  // Reorder the flow so it packs: row 0 must start with the connection item and
  // end with Ctrl+K (next to GitHub), filled with whatever else fits. Remaining
  // items are greedy-packed into full-width rows below.
  function pack() {
    if (!flowEl) return;
    const children = Array.from(flowEl.children) as HTMLElement[];
    if (children.length === 0) return;
    const W = flowEl.clientWidth;
    if (W <= 0) return;

    const widths = new Map<HTMLElement, number>();
    for (const el of children) widths.set(el, Math.ceil(el.getBoundingClientRect().width));
    const w = (el: HTMLElement) => widths.get(el) ?? 0;

    const conn = children.find((el) => itemOf(el) === 'conn');
    const palette = children.find((el) => itemOf(el) === 'palette');
    if (!conn) return;

    // GitHub is pinned out of flow, so only row 0 has to keep its column clear;
    // rows below can run the full width underneath it.
    const reserve = githubEl ? Math.ceil(githubEl.getBoundingClientRect().width) + GAP : 0;

    const others = children.filter((el) => el !== conn && el !== palette);

    const buildRows = (fold: boolean): HTMLElement[][] => {
      const row0: HTMLElement[] = [conn];
      let used = w(conn);

      if (palette) {
        const budget = W - reserve - w(palette) - GAP;
        const chosen = new Set<HTMLElement>();
        const claimants = others
          .filter((el) => !fold || foldOrder(el) === MID_FOLD)
          .sort((a, b) => row0Claim(b) - row0Claim(a));
        for (const el of claimants) {
          if (used + GAP + w(el) <= budget) { chosen.add(el); used += GAP + w(el); }
        }
        row0.push(...others.filter((el) => chosen.has(el)));
        // Ctrl+K only joins row 0 if it fits next to the connection item.
        if (w(conn) + GAP + w(palette) <= W) row0.push(palette);
      }

      const rest = others.filter((el) => !row0.includes(el) && el !== palette);
      if (palette && !row0.includes(palette)) rest.push(palette);
      if (fold) rest.sort((a, b) => foldOrder(a) - foldOrder(b));

      const rows: HTMLElement[][] = [row0];
      let cur: HTMLElement[] = [];
      let curW = 0;
      for (const el of rest) {
        if (cur.length > 0 && curW + GAP + w(el) > W) { rows.push(cur); cur = []; curW = 0; }
        cur.push(el);
        curW = cur.length === 1 ? w(el) : curW + GAP + w(el);
      }
      if (cur.length > 0) rows.push(cur);
      return rows;
    };

    const rowWidth = (row: HTMLElement[]) =>
      row.reduce((sum, el) => sum + w(el), 0) + GAP * (row.length - 1);
    // The browser wraps greedily, so a planned row that still leaves room for the
    // next row's first item would be filled up (and overflow the GitHub column).
    const packsExactly = (candidate: HTMLElement[][]) =>
      candidate.every((row, i) => {
        const next = candidate[i + 1];
        if (!next) return true;
        return rowWidth(row) + (i === 0 ? reserve : 0) + GAP + w(next[0]) > W;
      });

    let rows = buildRows(false);
    // A three-row fold lifts the wide bus group above the car picker, but only
    // when its wrap points survive that greedy line breaking.
    if (rows.length >= 3) {
      const folded = buildRows(true);
      if (packsExactly(folded)) rows = folded;
    }

    const simkill = children.find((el) => itemOf(el) === 'simkill');
    let simkillRow = 0;

    let order = 0;
    for (let ri = 0; ri < rows.length; ri++) {
      // Left-aligned items lead the row so they stay pinned left; the first
      // right-aligned item is the one pushed to the right edge.
      const row = [...rows[ri]].sort(
        (a, b) => Number(RIGHT_ALIGNED.has(itemOf(a))) - Number(RIGHT_ALIGNED.has(itemOf(b)))
      );
      const anchor = row.find((el) => RIGHT_ALIGNED.has(itemOf(el)));
      const last = row[row.length - 1];
      for (const el of row) {
        el.style.order = String(order++);
        el.style.marginLeft = el === anchor ? 'auto' : '';
        el.style.marginRight = ri === 0 && el === last && reserve > 0 ? reserve + 'px' : '';
        if (el === simkill) simkillRow = ri;
      }
    }

    // Grow Sim/Kill straight from the computed plan (no measure/re-apply dance,
    // which caused an extra reflow on every layout pass).
    flowEl.querySelector<HTMLElement>('.sim-kill-group')
      ?.classList.toggle('sim-kill-group--big', simkillRow > 0);
  }

  function layout() {
    pack();
  }

  onMount(() => {
    checkLatestStable();
    layout();

    const ro = new ResizeObserver(() => layout());
    if (statusEl) ro.observe(statusEl);

    const mo = new MutationObserver(() => layout());
    if (statusEl) mo.observe(statusEl, { childList: true, subtree: true, characterData: true });

    window.addEventListener('resize', layout);
    document.fonts?.ready.then(() => layout());

    return () => {
      ro.disconnect();
      mo.disconnect();
      window.removeEventListener('resize', layout);
    };
  });

  $effect(() => {
    if (app.connected) checkLatestStable();
  });
</script>

<div class="status" bind:this={statusEl}>
  <div class="status__flow" bind:this={flowEl}>
    <div class="status__item" data-item="conn">
      {#if confirmDisconn}
        <span class="pip">
          <Icon name="ble" size={14} />
          <span>Disconnect?</span>
        </span>
        <button class="btn btn--sm btn--danger" onclick={() => { confirmDisconn = false; app.toggleConnect(); }}>Yes</button>
        <button class="btn btn--sm btn--ghost" onclick={() => (confirmDisconn = false)}>No</button>
      {:else}
        <button class="pip pip--clickable" onclick={handleConnClick} title="Toggle connection">
          {#if app.transport === 'ws'}
            <Icon name="wifi" size={14} />
          {:else}
            <Icon name="ble" size={14} />
          {/if}
          <span class={'pip__dot ' + dotClass}></span>
          <span>{stateLabel}</span>
        </button>
      {/if}
    </div>

    <div class="status__item" data-item="car" data-row0="100" data-fold-order="3">
      <CarPip car={app.car} onOpen={() => (app.carPickerOpen = true)} />
    </div>

    <div class="status__item" data-item="bus" data-row0="10" data-fold-order="1">
      <div class="bus-group">
        <BusPip id={0} name="VehicleCAN" status={app.bus0Status} rate={app.bus0Rate} />
        <BusPip id={1} name="ChassisCAN" status={app.bus1Status} rate={app.bus1Rate} />
      </div>
    </div>

    {#if app.connected}
      <div class="status__item" data-item="cpu" data-row0="60" data-fold-order="4">
        <CpuPip load={app.cpuLoad} />
      </div>
      <div class="status__item" data-item="version" data-row0="40" data-fold-order="5">
        <VersionPip
          version={app.fwVersion ?? 'v?.?.?'}
          latest={fwLatestStatus}
          channel="stable"
          progress={app.otaBusy || app.otaDone ? app.otaProgress : null}
          onclick={() => { app.setView('settings'); setTimeout(() => document.getElementById('firmware-frame')?.scrollIntoView({ behavior: 'smooth', block: 'start' }), 50); }}
        />
      </div>
      <div class="status__item" data-item="simkill" data-row0="50" data-fold-order="2">
        <div class="sim-kill-group">
          <button
            class={'btn btn--sm' + (app.simulation ? ' btn--info' : '')}
            onclick={() => app.toggleSim()}
            title="Simulation mode — all sends routed to the log instead of CAN bus"
          >
            <Icon name="sim" size={14} /><span>Sim</span>
          </button>
          <button
            class={'btn btn--sm' + (app.killed ? ' btn--danger' : '')}
            onclick={() => app.toggleKill()}
            title="Disable ALL scripts — fail-safe"
          >
            <Icon name="power" size={14} /><span>{app.killed ? 'Release' : 'Kill'}</span>
          </button>
        </div>
      </div>
    {/if}

    <div class="status__item" data-item="palette">
      <button class="btn btn--sm btn--ghost" onclick={onPalette} title={isMobile ? 'Search' : modKey + 'K'}>
        <Icon name="search" size={14} /><span>{isMobile ? 'Search' : modKey + 'K'}</span>
      </button>
    </div>
  </div>

  <a
    class="btn btn--sm btn--ghost status__github"
    bind:this={githubEl}
    href="https://github.com/dzid26/ESP32-DualCAN"
    target="_blank"
    rel="noreferrer"
    title="View on GitHub"
    aria-label="GitHub repository"
  >
    <FilledIcon name="github" size={16} />
  </a>
</div>
