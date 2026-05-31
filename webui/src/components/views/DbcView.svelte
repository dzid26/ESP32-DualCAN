<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import { parseDbcInWorker, compileDbc, type Message } from '../../dbc/parser';
  import { dbcStore } from '../../dbcStore.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';

  let bus = $state(0);
  let messages = $state.raw<Message[]>([]);
  let loadedFrom = $state<string | null>(null);
  let status = $state('No DBC loaded');
  let busy = $state(false);
  let openMessages = $state(new Set<number>());
  let busDbc = $state.raw<Record<number, { messages: Message[]; loadedFrom: string }>>({});

  function publishSignals(): void {
    const flat: { name: string; message: string; msgId: number; sigIndex: number }[] = [];
    const msgList: { name: string; id: number }[] = [];
    for (const d of Object.values(busDbc)) {
      for (const m of d.messages) {
        msgList.push({ name: m.name, id: m.id });
        const sigs = m.signals;
        for (let si = 0; si < sigs.length; si++)
          flat.push({ name: sigs[si].name, message: m.name, msgId: m.id, sigIndex: si });
      }
    }
    dbcStore.setSignals(flat);
    dbcStore.setMessages(msgList);
    dbcStore.setFullMessages(Object.fromEntries(
      Object.entries(busDbc).map(([bus, d]) => [Number(bus), d.messages])
    ));
  }

  $effect(() => {
    const d = busDbc[bus];
    if (d) {
      messages = d.messages;
      loadedFrom = d.loadedFrom;
      status = `Bus ${bus}: ${d.messages.length} messages, ${d.messages.reduce((n, m) => n + m.signals.length, 0)} signals — ${d.loadedFrom}`;
    } else {
      messages = [];
      loadedFrom = null;
      status = `Bus ${bus}: not loaded`;
    }
  });

  function toggleMessage(e: Event, id: number): void {
    const s = new Set(openMessages);
    (e.currentTarget as HTMLDetailsElement).open ? s.add(id) : s.delete(id);
    openMessages = s;
  }

  async function loadText(text: string, label: string, targetBus: number, cacheKey?: string): Promise<void> {
    status = `Parsing ${label}…`;
    await new Promise(r => setTimeout(r, 0));
    try {
      const parsed = await parseDbcInWorker(text, cacheKey);
      openMessages = new Set();
      busDbc = { ...busDbc, [targetBus]: { messages: parsed, loadedFrom: label } };
      publishSignals();
    } catch (e) {
      status = `parse failed: ${e instanceof Error ? e.message : e}`;
    }
  }

  function handleFile(e: Event): void {
    const input = e.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => { void loadText(reader.result as string, file.name, bus, file.name); };
    reader.readAsText(file);
  }

  async function loadTeslaDefaults(): Promise<void> {
    if (busy) return;
    busy = true;
    status = 'Loading…';
    try {
      const urlVeh = `${import.meta.env.BASE_URL}dbc/Model3_VEH.dbc`;
      const urlCh = `${import.meta.env.BASE_URL}dbc/Model3_CH.dbc`;
      const [vehResp, chResp] = await Promise.all([fetch(urlVeh), fetch(urlCh)]);
      if (!vehResp.ok) throw new Error(`VEH HTTP ${vehResp.status}`);
      if (!chResp.ok) throw new Error(`CH HTTP ${chResp.status}`);
      const [vehText, chText] = await Promise.all([vehResp.text(), chResp.text()]);
      status = 'Parsing…';
      const [veh, ch] = await Promise.all([
        parseDbcInWorker(vehText, urlVeh),
        parseDbcInWorker(chText, urlCh),
      ]);
      if (busDbc[0]?.messages === veh && busDbc[1]?.messages === ch) {
        status = `Cached — Bus 0: ${veh.length} msgs · Bus 1: ${ch.length} msgs`;
        return;
      }
      openMessages = new Set();
      busDbc = {
        [0]: { messages: veh, loadedFrom: 'VehicleCAN' },
        [1]: { messages: ch, loadedFrom: 'ChassisCAN' },
      };
      publishSignals();
      bus = 0;
    } catch (e) {
      status = `load failed: ${e instanceof Error ? e.message : e}`;
    } finally {
      busy = false;
    }
  }

  async function upload(busId: number): Promise<void> {
    const d = busDbc[busId];
    if (!d || d.messages.length === 0) return;
    busy = true;
    try {
      const binary = compileDbc(d.messages, busId);
      status = `Compiled ${binary.length} bytes for bus ${busId} (${d.loadedFrom})`;
      if (app.ble.connected) {
        await app.ble.send(binary);
        status += ' — uploaded';
        dbcStore.setLastBus(busId);
        app.loadedDbc = { ...app.loadedDbc, [busId]: d.loadedFrom };
        app.pushLog(`DBC uploaded to bus ${busId} (${binary.length} bytes)`, 'info', 'dbc');
      } else {
        status += ' — not connected (compile-only)';
      }
    } catch (e) {
      status = `upload failed: ${e instanceof Error ? e.message : e}`;
    } finally {
      busy = false;
    }
  }

  /** Cross-view hand-off from Gallery: fetch URL, parse, switch to that
   * bus tab, upload if connected. Clears the flag so a re-mount doesn't
   * replay it. */
  $effect(() => {
    const p = app.pendingDbc;
    if (!p) return;
    app.pendingDbc = null;
    bus = p.busId;
    void (async () => {
      status = `fetching ${p.name}…`;
      try {
        const resp = await fetch(p.url);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const text = await resp.text();
        await loadText(text, p.name, p.busId, p.url);
        if (app.ble.connected) await upload(p.busId);
      } catch (e) {
        status = `gallery load failed: ${e instanceof Error ? e.message : e}`;
      }
    })();
  });

  /** Pure navigation: bus-pip "(dbc)" link sets dbcViewBus + setView('dbc'). */
  $effect(() => {
    const b = app.dbcViewBus;
    if (b === null) return;
    bus = b;
    app.dbcViewBus = null;
  });

  function hex3(n: number): string {
    return '0x' + n.toString(16).toUpperCase().padStart(3, '0');
  }
</script>

<div style="padding: 12px; display: flex; flex-direction: column; flex: 1; min-height: 0; gap: 10px">
  <SectionHead title="DBC" sub="Per-bus signal definitions">
    {#snippet actions()}
      <button class="btn btn--sm btn--info" onclick={loadTeslaDefaults} disabled={busy}>
        Load Tesla Model 3 defaults
      </button>
      <label class="btn btn--sm">
        <Icon name="up" size={13} /> Upload .dbc
        <input type="file" accept=".dbc" style="display: none" onchange={handleFile} />
      </label>
    {/snippet}
  </SectionHead>

  <div class="row-flex" style="flex-wrap: wrap; gap: 10px">
    <div
      class="row-flex"
      role="tablist"
      style="gap: 2px; background: var(--dc-surface); border: 1px solid var(--dc-border); border-radius: 4px; padding: 3px"
    >
      {#each [0, 1] as b}
        <button
          class={'btn btn--sm ' + (bus === b ? '' : 'btn--ghost')}
          style={bus === b ? 'background: var(--dc-surface-3); border-color: var(--dc-border-hi); color: var(--dc-text)' : ''}
          onclick={() => (bus = b)}
        >
          Bus {b}
        </button>
      {/each}
    </div>

    {#if messages.length > 0}
      <button class="btn btn--sm btn--primary" onclick={() => upload(bus)} disabled={busy}>
        <Icon name="up" size={13} /> Compile & upload to bus {bus}
      </button>
    {/if}

    <span class="spacer"></span>
    <span class="mono ghost" style="font-size: 11px">{status}</span>
  </div>

  {#if messages.length === 0}
    <div class="empty">
      No DBC parsed. Use <strong>Load Tesla defaults</strong> or <strong>Upload .dbc</strong> above.
    </div>
  {:else}
    <div class="dtable" style="flex: 1">
      <div class="dtable__head" style="grid-template-columns: 80px 1fr 90px">
        <span>ID</span><span>Message</span><span>Signals</span>
      </div>
      <div class="dtable__body">
        {#each messages as m (m.id)}
          <details style="border-bottom: 1px solid var(--dc-border)" ontoggle={(e) => toggleMessage(e, m.id)}>
            <summary class="mono" style="display: grid; grid-template-columns: 80px 1fr 90px; gap: 8px; padding: 4px 10px; cursor: pointer; font-size: 12px; align-items: center">
              <span>{hex3(m.id)}</span>
              <span style="font-family: var(--dc-font-sans); color: var(--dc-text)">{m.name}</span>
              <span class="muted">{m.signals.length} signals</span>
            </summary>
            {#if openMessages.has(m.id)}
              <ul class="mono" style="margin: 0; padding: 4px 10px 8px 28px; font-size: 11px; color: var(--dc-text-fade); list-style: square">
                {#each m.signals as sig}
                  <li>{sig.name} <span class="ghost">[{sig.startBit}|{sig.bitLength}] scale={sig.scale} offset={sig.offset}</span></li>
                {/each}
              </ul>
            {/if}
          </details>
        {/each}
      </div>
      <div style="padding: 4px 10px; border-top: 1px solid var(--dc-border); font-size: 11px; color: var(--dc-text-fade); display: flex; justify-content: space-between">
        <span>{messages.length} messages · {messages.reduce((n, m) => n + m.signals.length, 0)} signals</span>
        <span class="mono">{loadedFrom ?? ''}</span>
      </div>
    </div>
  {/if}
</div>
