<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { GALLERY_SCRIPTS, GALLERY_DBCS, DBC_SOURCE, type GalleryDbc } from '../../lib/sampleData';

  type Tab = 'scripts' | 'dbcs';
  let tab = $state<Tab>('scripts');

  const carBrand = $derived(app.car?.brand);
  const matches = (item: { brands?: string[] }) =>
    !carBrand || item.brands?.includes('*') || item.brands?.includes(carBrand);

  const scripts = $derived(GALLERY_SCRIPTS.filter(matches));
  const dbcs = $derived(GALLERY_DBCS.filter(matches));
  const opendbcDbcs  = $derived(dbcs.filter(d => d.source === 'opendbc'));
  const communityDbcs = $derived(dbcs.filter(d => d.source === 'community'));

  // Per-card chosen bus (Tesla VehicleCAN defaults to bus 0, ChassisCAN to bus 1).
  let pickedBus = $state<Record<string, number>>({});
  function busFor(d: { n: string; bus: number }): number {
    return pickedBus[d.n] ?? d.bus;
  }

  function loadDbc(d: { n: string; file: string; url?: string }, busId: number): void {
    if (!d.url) {
      app.pushLog(`Gallery DBC "${d.n}" has no bundled URL — fetch from opendbc by hand`, 'warn', 'gallery');
      return;
    }
    app.pendingDbc = { url: d.url, busId, name: d.file };
    app.setView('dbc');
  }
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px">
  <SectionHead title="Gallery" sub="Curated scripts and DBCs from GitHub">
    {#snippet actions()}
      <button class="btn btn--sm btn--ghost"><Icon name="search" size={13} />Search github</button>
    {/snippet}
  </SectionHead>

  <div class="frame" style:border-color={app.car ? 'var(--dc-border)' : 'var(--dc-warn-tint)'}>
    <div class="frame__body row-flex" style="padding: 8px 12px; gap: 10px; flex-wrap: wrap">
      <span style="font-size: 14px">{app.car?.icon ?? '🚗'}</span>
      {#if app.car}
        <span style="font-size: 12px; color: var(--dc-text-dim)">
          Compatible with <strong style="color: var(--dc-text)">{app.car.brand} {app.car.model}</strong>
          <span class="muted mono" style="margin-left: 6px; font-size: 11px">{app.car.years}</span>
        </span>
        <span class="spacer"></span>
        <span class="muted" style="font-size: 11px">Clear vehicle in status bar to see everything.</span>
      {:else}
        <span style="font-size: 12px; color: var(--dc-warn)">
          No vehicle profile selected — showing everything.
        </span>
        <span class="spacer"></span>
        <span class="muted" style="font-size: 11px">Pick a car in the status bar to filter.</span>
      {/if}
    </div>
  </div>

  <div
    class="row-flex"
    role="tablist"
    style="gap: 2px; background: var(--dc-surface); border: 1px solid var(--dc-border); border-radius: 4px; padding: 3px; align-self: flex-start"
  >
    {#each [
      { id: 'scripts' as Tab, label: 'Scripts',  icon: 'scripts' as const, shown: scripts.length, total: GALLERY_SCRIPTS.length },
      { id: 'dbcs'    as Tab, label: 'DBCs',     icon: 'dbc'     as const, shown: dbcs.length,    total: GALLERY_DBCS.length },
    ] as t}
      <button
        class={'btn btn--sm ' + (tab === t.id ? '' : 'btn--ghost')}
        style={tab === t.id ? 'background: var(--dc-surface-3); border-color: var(--dc-border-hi); color: var(--dc-text)' : ''}
        onclick={() => (tab = t.id)}
      >
        <Icon name={t.icon} size={13} />{t.label}
        <span class="ghost mono" style="font-size: 10px; margin-left: 2px">
          {app.car && t.shown !== t.total ? `${t.shown}/${t.total}` : t.total}
        </span>
      </button>
    {/each}
  </div>

  {#if tab === 'scripts'}
    <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 10px">
      {#each scripts as ex}
        <div class="frame gal-card">
          <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
            <div class="row-flex" style="justify-content: space-between; align-items: flex-start; gap: 8px">
              <div style="font-size: 13px; color: var(--dc-text); font-weight: 600; line-height: 1.3; flex: 1">{ex.n}</div>
              <span class={'gal-bus gal-bus--' + ex.bus} title={`Targets bus ${ex.bus}`}>bus {ex.bus}</span>
            </div>
            <div style="font-size: 12px; color: var(--dc-text-fade); line-height: 1.45; flex: 1">{ex.desc}</div>
            <div class="row-flex gal-meta" style="flex-wrap: wrap; gap: 6px">
              {#if ex.brands?.includes('*')}
                <span class="gal-brand gal-brand--any mono">universal</span>
              {:else}
                {#each ex.brands as b}
                  <span class="gal-brand mono">{b}</span>
                {/each}
              {/if}
              <span class="spacer"></span>
              <span class="muted mono" style="font-size: 11px">@{ex.author}</span>
              <span class="ghost mono" style="font-size: 11px">★ {ex.stars}</span>
            </div>
            <div class="row-flex gal-actions" style="justify-content: flex-end">
              <button class="btn btn--sm btn--ghost">Preview</button>
              <button class="btn btn--sm btn--primary"><Icon name="down" size={13} />Install</button>
            </div>
          </div>
        </div>
      {/each}
      {#if scripts.length === 0}
        <div class="muted" style="grid-column: 1 / -1; text-align: center; padding: 24px; font-size: 12px">
          No scripts tagged for {app.car?.brand}. Clear the vehicle in the status bar to see everything.
        </div>
      {/if}
    </div>
  {/if}

  {#if tab === 'dbcs'}
    {#if dbcs.length === 0}
      <div class="muted" style="text-align: center; padding: 24px; font-size: 12px">
        No DBCs tagged for {app.car?.brand}. Clear the vehicle in the status bar to see everything.
      </div>
    {/if}

    {#snippet dbcCard(d: GalleryDbc)}
      <div class="frame gal-card">
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
          <div class="row-flex" style="justify-content: space-between; align-items: flex-start; gap: 8px">
            <div style="font-size: 13px; color: var(--dc-text); font-weight: 600; line-height: 1.3; flex: 1">{d.n}</div>
            <span class={'gal-bus gal-bus--' + d.bus} title={`Default bus ${d.bus}`}>bus {d.bus}</span>
          </div>
          <div style="font-size: 12px; color: var(--dc-text-fade); line-height: 1.45; flex: 1">{d.desc}</div>
          {#if d.source === 'opendbc'}
            <div class="gal-spec mono">
              <span class="gal-spec__label">file</span>
              <a
                href={`${DBC_SOURCE.url}/blob/master/opendbc/dbc/${d.file}`}
                target="_blank"
                rel="noopener"
                class="gal-spec__link"
                title={`Source: ${DBC_SOURCE.repo}`}
              >
                opendbc/{d.file} <span class="ghost">↗</span>
              </a>
            </div>
          {:else}
            <div class="gal-spec mono">
              <span class="gal-spec__label">file</span>
              <span class="gal-spec__link" style="color: var(--dc-text-fade)">{d.file}</span>
            </div>
          {/if}
          <div class="gal-stats mono">
            <div><span class="gal-stats__n">{d.msgs}</span><span class="gal-stats__l">msgs</span></div>
            <div><span class="gal-stats__n">{d.sigs}</span><span class="gal-stats__l">sigs</span></div>
            <div><span class="gal-stats__n">{d.ver}</span><span class="gal-stats__l">version</span></div>
            <div><span class="gal-stats__n">{d.size}</span><span class="gal-stats__l">size</span></div>
          </div>
          <div class="row-flex gal-meta" style="flex-wrap: wrap; gap: 6px">
            {#each d.brands as b}
              <span class="gal-brand mono">{b}</span>
            {/each}
          </div>
          <div class="row-flex gal-actions" style="justify-content: flex-end; gap: 6px">
            <span class="row-flex" title="Pick which bus to load this onto" style="gap: 4px">
              <span class="muted mono" style="font-size: 10px">to</span>
              <select
                class="sel"
                value={busFor(d)}
                onchange={(e) => (pickedBus = { ...pickedBus, [d.n]: Number((e.currentTarget as HTMLSelectElement).value) })}
                style="width: auto; height: 22px; padding: 0 6px; font-size: 11px"
              >
                <option value={0}>bus 0</option>
                <option value={1}>bus 1</option>
              </select>
            </span>
            <button class="btn btn--sm btn--primary" onclick={() => loadDbc(d, busFor(d))} disabled={!d.url} title={d.url ? 'Open in DBC view and load' : 'No bundled URL — fetch by hand'}>
              <Icon name="down" size={13} />Load
            </button>
          </div>
        </div>
      </div>
    {/snippet}

    {#if opendbcDbcs.length > 0}
      <div class="frame" style="background: var(--dc-info-bg); border-color: var(--dc-info-border)">
        <div class="frame__body" style="display: flex; align-items: center; gap: 10px; padding: 8px 12px">
          <Icon name="dbc" size={16} />
          <div style="flex: 1; font-size: 12px; color: var(--dc-info-text); line-height: 1.45">
            Definitions mirrored from
            <a href={DBC_SOURCE.url} target="_blank" rel="noopener" class="mono" style="color: var(--dc-info-text); text-decoration: underline; font-weight: 600">
              {DBC_SOURCE.repo}
            </a>
            — the de-facto open source for vehicle CAN definitions. Tracking <span class="mono">master</span>; updated nightly.
          </div>
          <a href={DBC_SOURCE.url} target="_blank" rel="noopener" class="btn btn--sm btn--ghost" style="color: var(--dc-info-text)">
            View repo ↗
          </a>
        </div>
      </div>
      <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 10px">
        {#each opendbcDbcs as d (d.n)}{@render dbcCard(d)}{/each}
      </div>
    {/if}

    {#if communityDbcs.length > 0}
      <h3 style="margin: 8px 0 0; font-size: 12px; letter-spacing: .12em; text-transform: uppercase; color: var(--dc-text-fade)">
        Community DBCs
      </h3>
      <p class="muted" style="margin: 0; font-size: 11px">
        Reverse-engineered by the dorky-commander community or third parties — not in upstream opendbc.
      </p>
      <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 10px">
        {#each communityDbcs as d (d.n)}{@render dbcCard(d)}{/each}
      </div>
    {/if}
  {/if}
</div>
