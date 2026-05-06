<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import Icon from '../Icon.svelte';

  type ToolStep = { kind: 'tool'; name: string; args: Record<string, unknown>; result: string };
  type ReasonStep = { kind: 'reason'; text: string };
  type ProseStep = { kind: 'prose'; text: string };
  type ProposeEventStep = { kind: 'propose-event'; evId: string; name: string; desc: string };
  type ProposeScriptStep = { kind: 'propose-script'; name: string; code: string };
  type Step = ToolStep | ReasonStep | ProseStep | ProposeEventStep | ProposeScriptStep;

  type UserTurn = { role: 'user'; text: string };
  type AgentTurn = { role: 'agent'; steps: Step[] };
  type Turn = UserTurn | AgentTurn;

  let { hostingHttp = false }: { hostingHttp?: boolean } = $props();

  const SAMPLE_TURNS: Turn[] = [
    { role: 'user', text: 'Turn on the headlights' },
    { role: 'agent', steps: [
      { kind: 'reason', text: 'User wants headlights on. Check available events first — safer than crafting a raw CAN frame.' },
      { kind: 'tool', name: 'list_events', args: {}, result: '9 events · 6 vehicle, 3 safe' },
      { kind: 'tool', name: 'lookup_event', args: { q: 'headlights' }, result: '"Headlights ON" · CAN1 0x3E9 byte4 bit0=1 · vehicle write' },
      { kind: 'propose-event', evId: 'hl-on', name: 'Headlights ON', desc: 'CAN1 0x3E9 byte4 bit0=1' },
      { kind: 'prose', text: 'I can fire the **Headlights ON** event. It writes to the body bus. Confirm below to send.' },
    ]},
    { role: 'user', text: 'Make a script that beeps when the battery drops below 20%' },
    { role: 'agent', steps: [
      { kind: 'reason', text: 'Need to find SOC signal in the loaded DBC, then build an event-handler script that triggers a beep.' },
      { kind: 'tool', name: 'lookup_signal', args: { q: 'battery state of charge' }, result: 'BMS_socUI · message 0x292 · scale 0.5%/bit · CAN0' },
      { kind: 'tool', name: 'list_events', args: { tag: 'beep' }, result: 'Cabin beep · safe' },
      { kind: 'propose-script', name: 'Low SOC alert',
        code: `# meta: automation
# Beep once when SOC dips below 20% — re-arms above 25%.
import dbc
import events

var armed = true

dbc.on('BMS_socUI', def(soc)
  if soc < 20 && armed
    events.fire('beep')
    armed = false
  elif soc > 25
    armed = true
  end
end)` },
      { kind: 'prose', text: 'Save this to a slot? It uses **events.fire("beep")** so you can keep tweaking the beep tile separately.' },
    ]},
  ];

  let turns = $state<Turn[]>(SAMPLE_TURNS);
  let draft = $state('');
  let busy = $state(false);
  let endEl: HTMLDivElement | undefined = $state();

  $effect(() => {
    void turns.length;
    endEl?.scrollIntoView({ block: 'end' });
  });

  function send(): void {
    const text = draft.trim();
    if (!text) return;
    turns = [...turns, { role: 'user', text }];
    draft = '';
    busy = true;
    setTimeout(() => {
      turns = [...turns, { role: 'agent', steps: [
        { kind: 'reason', text: '(mock) I would call window.claude.complete() with the tool catalog and your message here.' },
        { kind: 'prose',  text: 'In a wired build this is where streaming output appears. Tools render as cards as they fire.' },
      ]}];
      busy = false;
    }, 700);
  }

  function onTextareaKey(e: KeyboardEvent): void {
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      send();
    }
  }

  // very light markdown — bold + inline code
  type ProsePart = { kind: 'b' | 'c' | 't'; v: string };
  function splitProse(text: string): ProsePart[] {
    const parts = text.split(/(\*\*[^*]+\*\*|`[^`]+`)/g);
    return parts.map(p => {
      if (p.startsWith('**') && p.endsWith('**')) return { kind: 'b', v: p.slice(2, -2) };
      if (p.startsWith('`')  && p.endsWith('`'))  return { kind: 'c', v: p.slice(1, -1) };
      return { kind: 't', v: p };
    });
  }

  const disabled = $derived(hostingHttp || !app.connected || app.killed);
</script>

<div class="view view--ai">
  <header class="view__head">
    <div>
      <h2 class="view__title row-flex"><Icon name="sparkle" size={18} /> AI assistant</h2>
      <p class="view__sub">Describe what you want. The agent reads signals, fires events, and proposes scripts — vehicle writes always confirm.</p>
    </div>
    <div class="row-flex">
      <span class={'pill ' + (hostingHttp ? 'pill--warn' : 'pill--ok')}>
        {hostingHttp ? 'AI disabled · open hosted UI' : 'Hosted · BYOK ready'}
      </span>
    </div>
  </header>

  <div class="ai-stage">
    {#if hostingHttp}
      <div class="ai-banner">
        <Icon name="sparkle" size={14} />
        <span>
          This page is served from the device over HTTP, so the browser blocks calls to api.anthropic.com.
          Open <a href="https://dzid26.github.io/dorky-commander" target="_blank" rel="noopener">the hosted UI</a> to enable AI.
          The hosted page still talks to your device over Web Bluetooth.
        </span>
      </div>
    {/if}

    <div class="frame ai-frame">
      <div class="ai-chat">
        {#each turns as t, i (i)}
          {#if t.role === 'user'}
            <div class="ai-msg ai-msg--user">
              <div class="ai-msg__bubble">{t.text}</div>
            </div>
          {:else}
            <div class="ai-msg ai-msg--agent">
              {#each t.steps as s, j (j)}
                {#if s.kind === 'reason'}
                  <details class="ai-reason">
                    <summary class="mono">↳ reasoning</summary>
                    <div>{s.text}</div>
                  </details>
                {:else if s.kind === 'tool'}
                  <div class="ai-tool mono">
                    <span class="ai-tool__name"><Icon name="tool" size={12} /> {s.name}</span>
                    <span class="ai-tool__args">{JSON.stringify(s.args)}</span>
                    <span class="ai-tool__sep">→</span>
                    <span class="ai-tool__result">{s.result}</span>
                  </div>
                {:else if s.kind === 'propose-event'}
                  <div class="ai-card ai-card--event">
                    <div class="ai-card__head">
                      <Icon name="bolt" size={14} /> Proposed action — fire event
                    </div>
                    <div class="ai-card__body">
                      <div class="ai-card__title">{s.name}</div>
                      <div class="mono ai-card__sub">{s.desc}</div>
                      <div class="row-flex" style="justify-content: flex-end; margin-top: 10px">
                        <button class="btn btn--sm btn--ghost">Cancel</button>
                        <button class="btn btn--sm btn--primary" {disabled}>
                          <Icon name="bolt" size={13} />Fire
                        </button>
                      </div>
                    </div>
                  </div>
                {:else if s.kind === 'propose-script'}
                  <div class="ai-card ai-card--script">
                    <div class="ai-card__head">
                      <Icon name="scripts" size={14} /> Proposed script
                    </div>
                    <div class="ai-card__body">
                      <input class="inp" value={s.name} style="margin-bottom: 6px; width: 100%" />
                      <div class="ai-card__editor">
                        <pre class="mono">{s.code}</pre>
                      </div>
                      <div class="row-flex" style="justify-content: space-between; margin-top: 10px">
                        <span class="mono ghost">type: automation · {s.code.split('\n').length} lines</span>
                        <span class="row-flex">
                          <button class="btn btn--sm btn--ghost">Discard</button>
                          <button class="btn btn--sm">Install only</button>
                          <button class="btn btn--sm btn--primary"><Icon name="check" size={13} />Install &amp; enable</button>
                        </span>
                      </div>
                    </div>
                  </div>
                {:else if s.kind === 'prose'}
                  <div class="ai-prose">
                    {#each splitProse(s.text) as p}
                      {#if p.kind === 'b'}<strong>{p.v}</strong>
                      {:else if p.kind === 'c'}<code class="mono">{p.v}</code>
                      {:else}{p.v}{/if}
                    {/each}
                  </div>
                {/if}
              {/each}
            </div>
          {/if}
        {/each}
        {#if busy}
          <div class="ai-typing mono">agent is thinking<span class="ai-typing__dots"></span></div>
        {/if}
        <div bind:this={endEl}></div>
      </div>

      <footer class="ai-input">
        <div class="ai-suggest mono">
          <button class="chip" onclick={() => (draft = 'Lock the doors')}>🔒 Lock the doors</button>
          <button class="chip" onclick={() => (draft = 'What does message 0x292 contain?')}>🔍 Decode 0x292</button>
          <button class="chip" onclick={() => (draft = 'Beep when reverse gear is engaged')}>🔔 Beep on reverse</button>
        </div>
        <div class="ai-inputrow">
          <textarea
            class="inp ai-textarea"
            placeholder="Describe what you want… ('turn on headlights', 'log every CAN1 frame with id 0x3E9 for 30s')"
            bind:value={draft}
            onkeydown={onTextareaKey}
            rows="2"
            disabled={hostingHttp}
          ></textarea>
          <button class="btn btn--primary" onclick={send} disabled={busy || !draft.trim() || hostingHttp}>
            <Icon name="sparkle" size={13} />Send
          </button>
        </div>
        <div class="ai-foot mono">
          <span>{hostingHttp ? '⚠ AI disabled in device-hosted mode' : 'haiku-4.5 · BYOK · responses streamed'}</span>
          <span>⌘↵ to send</span>
        </div>
      </footer>
    </div>
  </div>
</div>
