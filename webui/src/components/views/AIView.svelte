<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import { dbcStore } from '../../dbcStore.svelte';
  import { examples } from '../../examples';
  import Icon from '../Icon.svelte';

  // ---- types ----
  type UserTurn  = { role: 'user'; text: string; attached?: string };
  type AgentTurn = { role: 'assistant'; text: string; streaming: boolean };
  type Turn = UserTurn | AgentTurn;

  // ---- state ----
  let turns    = $state<Turn[]>([]);
  let draft    = $state('');
  let busy     = $state(false);
  let errorMsg = $state('');
  let endEl: HTMLDivElement | undefined = $state();

  /** Script attached for context — shown as a badge; prepended to the next send. */
  let attachedScript = $state<{ filename: string; code: string } | null>(null);

  /** Device script list for the "Attach from device" picker. */
  let deviceScripts = $state<string[]>([]);
  let attachPickerOpen = $state(false);
  let attachLoading = $state(false);

  /** API-side conversation history for multi-turn context. */
  let history: { role: 'user' | 'assistant'; content: string }[] = [];

  // Consume pendingAiScript hand-off from ScriptsView.
  $effect(() => {
    const p = app.pendingAiScript;
    if (!p) return;
    app.pendingAiScript = null;
    attachedScript = p;
  });

  $effect(() => {
    void turns.length;
    endEl?.scrollIntoView({ block: 'end' });
  });

  async function openAttachPicker(): Promise<void> {
    if (!app.connected) { errorMsg = 'Connect to device to list scripts'; return; }
    attachLoading = true;
    attachPickerOpen = false;
    try {
      const { scripts } = await app.proto.listScripts();
      deviceScripts = scripts.map(s => s.filename);
      attachPickerOpen = true;
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      attachLoading = false;
    }
  }

  async function attachFromDevice(filename: string): Promise<void> {
    attachPickerOpen = false;
    if (!filename) return;
    attachLoading = true;
    try {
      const { code } = await app.proto.readScript(filename);
      attachedScript = { filename, code };
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      attachLoading = false;
    }
  }

  // ---- system prompt ----
  function buildSystemPrompt(): string {
    // Format: "signal_name  (message: message_name)" so arg order is unambiguous.
    const signals = dbcStore.signals.slice(0, 80)
      .map(s => `${s.name}  (message: ${s.message})`)
      .join('\n');
    const exSnippets = examples.slice(0, 3)
      .map(e => `### ${e.name}\n\`\`\`berry\n${e.code}\n\`\`\``).join('\n\n');

    return `You are an AI assistant embedded in Dorky Commander — an open-source ESP32-C6 device for Tesla car automation using Berry scripting and CAN bus signals.

## Script structure
Every script starts with optional metadata comments, then must define \`setup()\`:

\`\`\`berry
# @name My script
# @description What it does
# @bus 0

def setup()
  # runs on enable
end

def teardown()
  # runs on disable (optional)
end
\`\`\`

## Berry Scripting API

### CAN signals (requires DBC loaded on device)
- \`on_can_signal(message_name, signal_name, fn)\` — subscribe to a signal; fn receives a map with \`fn['value']\` and \`fn['prev']\`. First arg is the DBC *message* name, second is the *signal* name within that message.
- \`can_signal_get(message_name, signal_name)\` — read current value (returns map or nil if DBC not loaded / signal not found)

### Raw CAN frames
- \`can_send_raw(bus, id, b)\` — send raw frame; \`bus\` = 0 or 1, \`id\` = integer, \`b\` = bytes object
- \`can_recv_raw(bus)\` — dequeue next received frame (returns bytes or nil)

### Encoded messages (requires DBC)
- \`can_msg_get(message_id)\` — get an encodable message object by numeric ID
- \`can_msg_set(msg, signal_name, value)\` — set a signal's value in the message
- \`can_msg_send(msg)\` — transmit with auto Tesla checksum/counter

### bytes objects
- \`var b = bytes()\` — create empty byte buffer
- \`b.add(v)\` — append byte value 0-255
- \`b.set(i, v)\` — set byte at index i
- \`b.item(i)\` — read byte at index i

### Timers
- \`timer_after(ms, fn)\` — one-shot timer
- \`timer_every(ms, fn)\` — repeating timer
- \`timer_cancel(fn)\` — cancel a timer

### Actions (Dashboard tiles)
- \`action_register(name, fn)\` — register a tile on the Events page
- \`action_invoke(name)\` — invoke an action from script code
- \`action_list()\` — returns list of registered action names

### LED
- \`led_set(r, g, b)\` — set onboard RGB LED (0–255 each channel)
- \`led_off()\` — turn off LED

### Persistent storage (NVS flash)
- \`state_set(key, value)\` — persist a value across reboots
- \`state_get(key)\` — read persisted value (nil if not set)
- \`state_remove(key)\` — delete a persisted key

### Utilities
- \`millis()\` — milliseconds since boot
- \`print(msg)\` — log to the web UI log panel
- \`str(v)\`, \`format(fmt, ...)\` — string conversion / formatting

Berry syntax: \`var\`, \`def\`, \`if/elif/else/end\`, \`while\`, \`for i:0..n\`, \`/-> expr\` (lambda).

## Device context
${app.connected ? '✓ Connected' : '⚠ Not connected'}
${Object.entries(app.loadedDbc).filter(([,v])=>v).map(([k,v])=>`Bus ${k}: ${v}`).join('\n') || 'No DBC loaded'}
${signals ? `\n## Available DBC signals\nFormat: signal_name  (message: message_name)\nUse message_name as first arg and signal_name as second arg to on_can_signal().\n\n${signals}` : ''}

## Example scripts
${exSnippets}

## Instructions
- Write complete Berry scripts in \`\`\`berry code blocks
- Always include \`def setup()\`; add \`# @name\` and \`# @bus\` metadata
- When using on_can_signal, use the exact message_name and signal_name from the DBC signal list above
- Keep scripts focused and well-commented
- Propose scripts rather than claiming to fire CAN frames directly`;
  }

  // ---- streaming fetch ----
  async function streamResponse(onChunk: (text: string) => void): Promise<void> {
    const resp = await fetch('https://api.anthropic.com/v1/messages', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'x-api-key': app.aiKey,
        'anthropic-version': '2023-06-01',
        'anthropic-dangerous-direct-browser-access': 'true',
      },
      body: JSON.stringify({
        model: app.aiModel,
        max_tokens: 2048,
        stream: true,
        system: buildSystemPrompt(),
        messages: history,
      }),
    });

    if (!resp.ok) {
      const body = await resp.json().catch(() => ({}));
      throw new Error((body as any)?.error?.message ?? `HTTP ${resp.status}`);
    }

    const reader = resp.body!.getReader();
    const dec = new TextDecoder();
    let buf = '';
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += dec.decode(value, { stream: true });
      const lines = buf.split('\n');
      buf = lines.pop() ?? '';
      for (const line of lines) {
        if (!line.startsWith('data: ')) continue;
        const data = line.slice(6).trim();
        if (data === '[DONE]') return;
        try {
          const evt = JSON.parse(data);
          if (evt.type === 'content_block_delta' && evt.delta?.type === 'text_delta') {
            onChunk(evt.delta.text as string);
          }
        } catch { /* skip malformed SSE */ }
      }
    }
  }

  // ---- send ----
  async function send(): Promise<void> {
    const text = draft.trim();
    if (!text || busy) return;
    errorMsg = '';
    draft = '';

    // If a script is attached, prepend it to the API content but display
    // the user's words only (with a small attachment indicator).
    const attached = attachedScript;
    attachedScript = null;
    const apiContent = attached
      ? `Current script "${attached.filename}":\n\`\`\`berry\n${attached.code}\n\`\`\`\n\n${text}`
      : text;

    turns = [...turns, { role: 'user', text, attached: attached?.filename } as UserTurn & { attached?: string }];
    history = [...history, { role: 'user', content: apiContent }];

    turns = [...turns, { role: 'assistant', text: '', streaming: true } as AgentTurn];
    const agentIdx = turns.length - 1;
    busy = true;
    try {
      if (!app.aiKey) throw new Error('No API key — add one in Settings → AI assistant');
      await streamResponse((chunk) => {
        // Access via reactive proxy (array index) so Svelte 5 detects the mutation.
        (turns[agentIdx] as AgentTurn).text += chunk;
      });
      history = [...history, { role: 'assistant', content: (turns[agentIdx] as AgentTurn).text }];
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
      turns = turns.filter((_, i) => i !== agentIdx);
      history = history.slice(0, -1);
    } finally {
      (turns[agentIdx] as AgentTurn | undefined) && ((turns[agentIdx] as AgentTurn).streaming = false);
      busy = false;
    }
  }

  function onKey(e: KeyboardEvent): void {
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) { e.preventDefault(); void send(); }
  }

  // ---- script install ----
  let installStatus = $state<Record<number, string>>({});

  async function installScript(turnIdx: number, code: string, enable: boolean): Promise<void> {
    if (!app.connected) {
      installStatus = { ...installStatus, [turnIdx]: 'connect to device first' };
      return;
    }
    const nameLine = code.match(/^#\s*@?name\s+(.+)$/m);
    const base = (nameLine?.[1] ?? `ai_script_${turnIdx}`)
      .toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '').slice(0, 32);
    const filename = base + '.be';
    try {
      installStatus = { ...installStatus, [turnIdx]: 'writing…' };
      await app.proto.writeScript(filename, code);
      if (enable) {
        installStatus = { ...installStatus, [turnIdx]: 'enabling…' };
        await app.proto.enableScript(filename);
      }
      installStatus = { ...installStatus, [turnIdx]: `✓ saved as ${filename}${enable ? ' (enabled)' : ''}` };
      app.pushLog(`AI script "${filename}" ${enable ? 'installed and enabled' : 'installed'}`, 'info', 'ai');
    } catch (err) {
      installStatus = { ...installStatus, [turnIdx]: `error: ${err instanceof Error ? err.message : err}` };
    }
  }

  // ---- response renderer ----
  type Seg = { kind: 'prose'; text: string } | { kind: 'code'; lang: string; code: string };

  function parseSegments(text: string): Seg[] {
    const segs: Seg[] = [];
    const re = /```(\w*)\n([\s\S]*?)```/g;
    let last = 0, m: RegExpExecArray | null;
    while ((m = re.exec(text)) !== null) {
      if (m.index > last) segs.push({ kind: 'prose', text: text.slice(last, m.index) });
      segs.push({ kind: 'code', lang: m[1] || 'text', code: m[2].trimEnd() });
      last = m.index + m[0].length;
    }
    if (last < text.length) segs.push({ kind: 'prose', text: text.slice(last) });
    return segs;
  }

  type Span = { t: 'b' | 'c' | 'text'; v: string };
  function parseSpans(text: string): Span[] {
    return text.split(/(\*\*[^*]+\*\*|`[^`]+`)/g).map(p => {
      if (p.startsWith('**') && p.endsWith('**')) return { t: 'b' as const, v: p.slice(2, -2) };
      if (p.startsWith('`')  && p.endsWith('`'))  return { t: 'c' as const, v: p.slice(1, -1) };
      return { t: 'text' as const, v: p };
    });
  }
</script>

<div class="view view--ai">
  <header class="view__head">
    <div>
      <h2 class="view__title row-flex"><Icon name="sparkle" size={18} />AI assistant</h2>
      <p class="view__sub">Describe what you want. Responses stream from Claude; Berry code blocks become installable scripts.</p>
    </div>
    {#if !app.aiKey}
      <button class="pill pill--warn" onclick={() => app.setView('settings')}>
        Set API key in Settings
      </button>
    {:else}
      <span class="pill pill--ok">BYOK · {app.aiModel.split('-').slice(1,3).join('-')}</span>
    {/if}
  </header>

  <div class="ai-stage">
    <div class="frame ai-frame">
      <div class="ai-chat">

        {#if turns.length === 0}
          <div class="ai-empty">
            <Icon name="sparkle" size={28} />
            <p>Ask anything about your car's CAN bus, or request a Berry script.</p>
            {#if !app.aiKey}
              <p class="ghost" style="font-size: 11px">Add your Anthropic API key in Settings first.</p>
            {/if}
          </div>
        {/if}

        {#each turns as turn, i (i)}
          {#if turn.role === 'user'}
            <div class="ai-msg ai-msg--user">
              {#if turn.attached}
                <div class="ai-attach-badge">📎 {turn.attached}</div>
              {/if}
              <div class="ai-msg__bubble">{turn.text}</div>
            </div>
          {:else}
            <div class="ai-msg ai-msg--agent">
              {#each parseSegments(turn.text) as seg}
                {#if seg.kind === 'prose'}
                  <div class="ai-prose">
                    {#each seg.text.split('\n') as line, li}
                      {#if li > 0}<br />{/if}
                      {#each parseSpans(line) as sp}
                        {#if sp.t === 'b'}<strong>{sp.v}</strong>
                        {:else if sp.t === 'c'}<code class="mono">{sp.v}</code>
                        {:else}{sp.v}{/if}
                      {/each}
                    {/each}
                  </div>
                {:else}
                  <div class="ai-card ai-card--script">
                    <div class="ai-card__head">
                      <Icon name="scripts" size={13} />
                      {seg.lang === 'berry' || seg.lang === 'be' ? 'Berry script' : seg.lang || 'code'}
                    </div>
                    <div class="ai-card__body">
                      <pre class="mono ai-code">{seg.code}</pre>
                      {#if seg.lang === 'berry' || seg.lang === 'be'}
                        <div class="row-flex" style="justify-content: space-between; margin-top: 8px; flex-wrap: wrap; gap: 6px">
                          <span class="mono ghost" style="font-size: 11px">{installStatus[i] ?? ''}</span>
                          <span class="row-flex" style="gap: 6px">
                            <button class="btn btn--sm"
                              onclick={() => installScript(i, seg.code, false)}
                              disabled={!app.connected}>
                              Install only
                            </button>
                            <button class="btn btn--sm btn--primary"
                              onclick={() => installScript(i, seg.code, true)}
                              disabled={!app.connected}>
                              <Icon name="check" size={13} />Install &amp; enable
                            </button>
                          </span>
                        </div>
                      {/if}
                    </div>
                  </div>
                {/if}
              {/each}
              {#if turn.streaming}
                <span class="ai-cursor">▊</span>
              {/if}
            </div>
          {/if}
        {/each}

        {#if errorMsg}
          <div class="ai-error mono">{errorMsg}</div>
        {/if}

        <div bind:this={endEl}></div>
      </div>

      <footer class="ai-input">
        <div class="ai-suggest mono">
          <button class="chip" onclick={() => (draft = 'What signals are in the loaded DBC?')}>
            🔍 List signals
          </button>
          <button class="chip" onclick={() => (draft = 'Write a script that blinks the LED when speed exceeds 100 km/h')}>
            💡 LED on speed
          </button>
          <button class="chip" onclick={() => (draft = 'Beep once when reverse gear is engaged')}>
            🔔 Beep on reverse
          </button>
        </div>

        <!-- Attachment bar: badge when script attached, picker when open -->
        {#if attachedScript}
          <div class="ai-attach-bar">
            <span class="mono" style="font-size: 11px">📎 {attachedScript.filename}</span>
            <button class="btn btn--sm btn--ghost" style="padding: 1px 6px; font-size: 11px"
              onclick={() => (attachedScript = null)}>✕</button>
          </div>
        {:else if attachPickerOpen}
          <div class="ai-attach-bar">
            <select class="sel" style="flex: 1; font-size: 11px"
              onchange={(e) => attachFromDevice((e.target as HTMLSelectElement).value)}>
              <option value="">— pick a script —</option>
              {#each deviceScripts as fn}
                <option value={fn}>{fn}</option>
              {/each}
            </select>
            <button class="btn btn--sm btn--ghost" style="padding: 1px 6px"
              onclick={() => (attachPickerOpen = false)}>✕</button>
          </div>
        {/if}

        <div class="ai-inputrow">
          <textarea
            class="inp ai-textarea"
            placeholder="Describe what you want… (⌘↵ or Ctrl↵ to send)"
            bind:value={draft}
            onkeydown={onKey}
            rows="2"
          ></textarea>
          <button class="btn btn--primary" onclick={send} disabled={busy || !draft.trim()}>
            <Icon name="sparkle" size={13} />Send
          </button>
        </div>
        <div class="ai-foot mono">
          <span class="row-flex" style="gap: 4px">
            <button class="btn btn--sm btn--ghost"
              onclick={() => { turns = []; history = []; errorMsg = ''; installStatus = {}; attachedScript = null; }}
              disabled={turns.length === 0}>Clear</button>
            <button class="btn btn--sm btn--ghost"
              onclick={openAttachPicker}
              disabled={!app.connected || attachLoading}
              title="Attach a script from the device as context">
              {attachLoading ? '…' : '📎 Attach script'}
            </button>
          </span>
          <span>⌘↵ to send</span>
        </div>
      </footer>
    </div>
  </div>
</div>

<style>
  .ai-empty {
    display: flex; flex-direction: column; align-items: center;
    gap: 10px; padding: 40px 20px; color: var(--dc-text-fade); text-align: center;
  }
  .ai-empty p { margin: 0; font-size: 13px; }

  .ai-code {
    font-size: 11px; line-height: 1.55; overflow-x: auto; white-space: pre;
    background: var(--dc-bg); border-radius: 4px; padding: 10px 12px; margin: 0;
    max-height: 320px; overflow-y: auto; color: var(--dc-text);
  }

  .ai-attach-badge {
    font-size: 10px; color: var(--dc-text-fade); font-family: var(--dc-mono);
    margin-bottom: 2px; text-align: right;
  }
  .ai-attach-bar {
    display: flex; align-items: center; gap: 6px;
    padding: 4px 8px; background: var(--dc-surface);
    border: 1px solid var(--dc-border); border-radius: 4px; margin-bottom: 4px;
  }
  .ai-error {
    background: var(--dc-err-bg, #2a1010); border: 1px solid var(--dc-err-border, #5a2020);
    border-radius: 6px; padding: 8px 12px; font-size: 12px; color: var(--dc-err-text);
    margin: 4px 0;
  }

  .ai-cursor {
    display: inline-block; animation: blink .7s step-end infinite;
    color: var(--dc-accent); font-size: 14px; line-height: 1;
  }
  @keyframes blink { 50% { opacity: 0 } }
</style>
