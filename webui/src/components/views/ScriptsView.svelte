<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import type { ScriptInfo } from '../../transport/protocol';
  import { examples } from '../../examples';
  import SectionHead from '../SectionHead.svelte';
  import CodeMirrorEditor from '../../editor/CodeMirrorEditor.svelte';
  import Icon from '../Icon.svelte';
  import { onMount } from 'svelte';

  // Boilerplate users land in for a fresh script.
  const NEW_SCRIPT = '# Write your Berry script here\n\ndef setup()\n  print("hello from setup")\nend\n';

  // Server state — populated on connect.
  let scripts = $state<ScriptInfo[]>([]);
  let listError = $state<string | null>(null);
  let busy = $state(false);
  let status = $state<string>('');

  // Editor state — selected file, its loaded code, dirty flag.
  let selFn = $state<string | null>(null);
  let code = $state<string>(NEW_SCRIPT);
  let savedCode = $state<string>(NEW_SCRIPT);
  let editorFilename = $state<string>('my_script.be');

  let confirmRemove = $state<string | null>(null);
  const dirty = $derived(code !== savedCode);
  let lastScriptStatusSeq = 0;

  let isMobile = $state(false);
  let editorPanelHeight = $state<number | null>(null);
  let editorPanelEl: HTMLElement | undefined;

  $effect(() => {
    const mq = window.matchMedia('(max-width: 720px)');
    const update = () => { isMobile = mq.matches; };
    update();
    mq.addEventListener('change', update);
    return () => mq.removeEventListener('change', update);
  });

  function startEditorResize(e: PointerEvent) {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    e.preventDefault();
  }

  function handleResizeMove(e: PointerEvent) {
    if (!e.buttons) return;
    if (editorPanelEl) {
      const rect = editorPanelEl.getBoundingClientRect();
      editorPanelHeight = Math.max(100, e.clientY - rect.top);
    }
  }

  function handleResizeEnd() {}

  async function refresh(): Promise<void> {
    if (!app.connected) return;
    try {
      const r = await app.proto.listScripts();
      scripts = r.scripts ?? [];
      listError = null;
      // Auto-select first script if nothing's selected and no draft started.
      if (!selFn && scripts.length > 0 && code === NEW_SCRIPT) {
        await loadScript(scripts[0].filename);
      }
    } catch (e) {
      listError = e instanceof Error ? e.message : String(e);
    }
  }

  async function loadScript(filename: string): Promise<void> {
    if (dirty && !confirm('Discard unsaved changes?')) return;
    selFn = filename;
    editorFilename = filename;
    try {
      const r = await app.proto.readScript(filename);
      code = r.code;
      savedCode = r.code;
      status = `loaded ${filename}`;
    } catch (e) {
      const m = e instanceof Error ? e.message : String(e);
      status = `read failed: ${m}`;
    }
  }

  async function save(): Promise<void> {
    const fn = (selFn ?? editorFilename).trim();
    if (!fn) { status = 'filename required'; return; }
    busy = true;
    status = `uploading ${fn}…`;
    try {
      await app.proto.writeScript(fn, code);
      savedCode = code;
      selFn = fn;
      editorFilename = fn;
      status = `saved ${fn}`;
      await refresh();
    } catch (e) {
      status = `upload failed: ${e instanceof Error ? e.message : e}`;
    } finally {
      busy = false;
    }
  }

  async function saveAndEnable(): Promise<void> {
    await save();
    if (selFn) await enable(selFn);
  }

  async function enable(filename: string): Promise<void> {
    busy = true;
    try {
      await app.proto.enableScript(filename);
      scripts = scripts.map(s => s.filename === filename
        ? { ...s, enabled: true, errored: false } : s);
      status = `enabled ${filename}`;
    } catch (e) {
      status = `enable failed: ${e instanceof Error ? e.message : e}`;
    } finally { busy = false; }
  }

  async function disable(filename: string): Promise<void> {
    busy = true;
    try {
      await app.proto.disableScript(filename);
      scripts = scripts.map(s => s.filename === filename
        ? { ...s, enabled: false } : s);
      status = `disabled ${filename}`;
    } catch (e) {
      status = `disable failed: ${e instanceof Error ? e.message : e}`;
    } finally { busy = false; }
  }

  async function uninstall(filename: string): Promise<void> {
    busy = true;
    try {
      await app.proto.deleteScript(filename);
      scripts = scripts.filter(s => s.filename !== filename);
      if (filename === selFn) {
        selFn = scripts[0]?.filename ?? null;
        if (selFn) await loadScript(selFn);
        else { code = NEW_SCRIPT; savedCode = NEW_SCRIPT; editorFilename = 'my_script.be'; }
      }
      status = `deleted ${filename}`;
    } catch (e) {
      status = `delete failed: ${e instanceof Error ? e.message : e}`;
    } finally {
      busy = false;
      confirmRemove = null;
    }
  }

  function newScript(): void {
    if (dirty && !confirm('Discard unsaved changes?')) return;
    selFn = null;
    editorFilename = 'my_script.be';
    code = NEW_SCRIPT;
    savedCode = '';
    status = 'new script — set a filename and Save';
  }

  /** Drop a bundled `firmware/scripts_examples/*.be` into the editor. */
  function loadExample(e: Event): void {
    const sel = e.currentTarget as HTMLSelectElement;
    const fn = sel.value;
    sel.value = '';
    if (!fn) return;
    if (dirty && !confirm('Discard unsaved changes?')) return;
    const ex = examples.find(x => x.filename === fn);
    if (!ex) { status = `unknown example: ${fn}`; return; }
    code = ex.code;
    savedCode = '';
    selFn = null;
    editorFilename = ex.filename;
    status = `loaded example: ${ex.name}`;
  }

  function revert(): void {
    code = savedCode;
    status = 'reverted';
  }

  function importFile(e: Event): void {
    const input = e.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      code = reader.result as string;
      savedCode = '';
      editorFilename = file.name;
      selFn = null;
      status = `imported ${file.name} (not yet saved)`;
    };
    reader.readAsText(file);
  }

  onMount(refresh);
  $effect(() => {
    // Re-list whenever connection flips, or whenever the global kill switch
    // bumps scriptsVersion (engage / release sweeps change enabled state).
    void app.connected; void app.scriptsVersion;
    if (app.connected) refresh();
    else { scripts = []; }
  });

  $effect(() => {
    const patch = app.scriptStatusPatch;
    if (!patch || patch.seq === lastScriptStatusSeq) return;
    lastScriptStatusSeq = patch.seq;
    scripts = scripts.map(s => {
      if (s.filename !== patch.filename) return s;
      return {
        ...s,
        enabled: patch.enabled,
        errored: patch.clearError ? false : s.errored,
        error: patch.clearError ? undefined : s.error,
      };
    });
  });

  /** Cross-view hand-off: EventsView (or anything else) sets
   * app.pendingExample to a filename in firmware/scripts_examples/ then
   * navigates here. Consume + clear so a re-mount doesn't replay it.
   * Confirms before clobbering an unsaved buffer. */
  $effect(() => {
    const fn = app.pendingExample;
    if (!fn) return;
    app.pendingExample = null;
    if (dirty && !confirm('Discard unsaved changes and load the example?')) {
      status = 'kept your unsaved buffer';
      return;
    }
    const ex = examples.find(x => x.filename === fn);
    if (ex) {
      code = ex.code;
      savedCode = '';
      selFn = null;
      editorFilename = ex.filename;
      status = `loaded example: ${ex.name}`;
    } else {
      status = `unknown example: ${fn}`;
    }
  });
</script>

<div style="padding: 12px; display: flex; flex-direction: column; flex: 1; min-height: 0; gap: 10px">
  <SectionHead
    title="Automations"
    sub="Berry scripts that run on their own — timers, event handlers, callbacks"
  >
    {#snippet actions()}
      <a
        href="https://berry-lang.github.io/"
        target="_blank"
        rel="noopener"
        class="btn btn--sm"
        title="Berry language docs — syntax, stdlib, examples"
        style="display: inline-flex; align-items: center; gap: 4px; text-decoration: none"
      >
        Berry guide
        <span aria-hidden="true" style="font-size: 9px; opacity: 0.7">↗</span>
      </a>
      <button class="btn btn--sm" onclick={newScript}><Icon name="up" size={13} />New</button>
      <select
        class="sel"
        onchange={loadExample}
        title="Load a bundled Berry example into the editor (firmware/scripts_examples)"
        style="flex: 1 1 auto; min-width: 80px; max-width: 160px; padding-right: 24px"
      >
        <option value="">Load example…</option>
        {#each examples as ex}
          <option value={ex.filename}>{ex.name}</option>
        {/each}
      </select>
      <label class="btn btn--sm btn--info" title="Read a .be file from disk into the editor">
        Import
        <input type="file" accept=".be,text/plain" onchange={importFile} style="display: none" />
      </label>
    {/snippet}
  </SectionHead>

  {#if listError}
    <div class="empty" style="border-color: var(--dc-err-border); color: var(--dc-err-text)">{listError}</div>
  {/if}

  <div
    style:display="grid"
    style:gap="10px"
    style:flex="1"
    style:min-height="0"
    style:overflow="auto"
    style:grid-template-columns={isMobile ? '1fr' : 'minmax(180px, 240px) 1fr'}
  >
    <div
      class="frame"
      style="display: flex; flex-direction: column; overflow: hidden; min-height: 130px"
    >
      <div class="frame__head">
        Installed <span class="ghost mono">{scripts.length}</span>
      </div>
      <div style="overflow-y: auto; flex: 1">
          {#if !app.connected}
            <div class="empty" style="margin: 10px">Not connected.</div>
          {:else if scripts.length === 0}
            <div class="empty" style="margin: 10px">No scripts installed. Use New or Import to add one.</div>
          {/if}
          {#each scripts as s (s.filename)}
          <div
            class="srow"
            style:background={selFn === s.filename ? 'var(--dc-surface-2)' : 'transparent'}
            onclick={() => loadScript(s.filename)}
            onkeydown={(e) => e.key === 'Enter' && loadScript(s.filename)}
            role="button"
            tabindex="0"
          >
            <span
              class={'tog ' + (s.enabled ? 'tog--on' : '')}
              onclick={(e) => { e.stopPropagation(); s.enabled ? disable(s.filename) : enable(s.filename); }}
              onkeydown={(e) => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); e.stopPropagation(); s.enabled ? disable(s.filename) : enable(s.filename); } }}
              role="switch"
              aria-checked={s.enabled}
              tabindex="0"
              title={s.enabled ? 'Disable' : 'Enable'}
            >
              <span class="tog__knob"></span>
            </span>
            <div style="min-width: 0">
              <div style="font-size: 12px; color: var(--dc-text); font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis">
                {s.filename}
              </div>
              <div class="mono" style:font-size="10px" style:color={s.errored ? 'var(--dc-err-text)' : 'var(--dc-text-fade)'} title={s.error ?? ''}>
                {s.errored ? 'error' : s.enabled ? 'running' : 'disabled'}
              </div>
            </div>
            <button
              class="btn btn--sm btn--ghost btn--icon"
              title={`Uninstall ${s.filename}`}
              aria-label={`Uninstall ${s.filename}`}
              onclick={(e) => { e.stopPropagation(); confirmRemove = s.filename; }}
            >
              <Icon name="trash" size={14} />
            </button>
          </div>
        {/each}
      </div>
    </div>

    <div
      class="frame"
      style="display: flex; flex-direction: column; min-height: 0; overflow: hidden; position: relative"
      bind:this={editorPanelEl}
      style:height={editorPanelHeight ? `${editorPanelHeight}px` : 'auto'}
      style:min-height={isMobile ? '300px' : '0'}
    >
      <div class="frame__head" style="flex-wrap: wrap; gap: 4px">
        {#if dirty}<span class="mono" style="color: var(--dc-warn); font-size: 10px; white-space: nowrap">● unsaved</span>{/if}
        <span class="row-flex" style="flex-wrap: wrap; gap: 4px; margin-left: auto">
          <button class="btn btn--sm"
            onclick={() => { app.pendingAiScript = { filename: selFn ?? editorFilename, code }; app.setView('ai'); }}
            title="Send this script to the AI assistant for editing">
            <Icon name="sparkle" size={16} /> AI
          </button>
          <button class="btn btn--sm" onclick={save} disabled={!app.connected || busy || !dirty} title="Save without changing enable state">
            <Icon name="up" size={13} />Save
          </button>
          <button class="btn btn--sm btn--primary" onclick={saveAndEnable} disabled={!app.connected || busy} title="Save and enable">
            <Icon name="up" size={13} />Save & enable
          </button>
          <button class="btn btn--sm" onclick={revert} disabled={!dirty}>Revert</button>
          {#if selFn}
            <button class="btn btn--sm btn--danger" onclick={() => (confirmRemove = selFn)} title="Remove this script from the device">
              <Icon name="trash" size={13} />Uninstall
            </button>
          {/if}
        </span>
      </div>

      {#if status}
        <div style="padding: 4px 12px; font-size: 11px; color: var(--dc-text-fade); border-bottom: 1px solid var(--dc-border)" class="mono">
          {status}
        </div>
      {/if}

      <div style="flex: 1; min-height: 0; display: flex; position: relative">
        <CodeMirrorEditor bind:value={code} height="100%" onSave={save} />
        <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
        <div
          class="resize-corner-editor"
          onpointerdown={startEditorResize}
          onpointermove={handleResizeMove}
          onpointerup={handleResizeEnd}
          title="Drag to resize editor"
          role="button"
          aria-label="Resize editor"
          tabindex="0"
        ></div>
      </div>
    </div>
  </div>

  {#if confirmRemove}
    <div
      class="cp-backdrop"
      onclick={() => (confirmRemove = null)}
      onkeydown={(e) => e.key === 'Escape' && (confirmRemove = null)}
      role="presentation"
    >
      <div
        class="frame"
        onclick={(e) => e.stopPropagation()}
        style="width: min(420px, 92vw); background: var(--dc-surface)"
        role="dialog"
        aria-modal="true"
      >
        <div class="frame__head" style="color: var(--dc-err-text)">
          <span class="row-flex"><Icon name="trash" size={13} /><span>Uninstall script</span></span>
          <button class="btn btn--sm btn--ghost btn--icon" onclick={() => (confirmRemove = null)} aria-label="Cancel">
            <Icon name="x" size={13} />
          </button>
        </div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
          <p style="margin: 0; font-size: 13px; color: var(--dc-text-dim); line-height: 1.5">
            Remove <strong style="color: var(--dc-text); font-family: var(--dc-font-mono)">{confirmRemove}</strong> from the device?
          </p>
          <p style="margin: 0; font-size: 12px; color: var(--dc-text-fade); line-height: 1.45">
            The Berry source and any persisted state will be erased. This cannot be undone unless you reinstall from a backup or the gallery.
          </p>
          <div class="row-flex" style="justify-content: flex-end; margin-top: 4px">
            <button class="btn btn--sm btn--ghost" onclick={() => (confirmRemove = null)}>Cancel</button>
            <button class="btn btn--sm btn--danger" onclick={() => uninstall(confirmRemove!)} disabled={busy}>
              <Icon name="trash" size={13} />Uninstall
            </button>
          </div>
        </div>
      </div>
    </div>
  {/if}
</div>

<style>
  .srow {
    width: 100%;
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: center;
    gap: 8px;
    padding: 6px 8px 6px 10px;
    border-bottom: 1px solid var(--dc-border);
    cursor: pointer;
    min-height: 0;
  }

  .resize-corner-editor {
    position: absolute;
    bottom: 0;
    right: 0;
    width: 20px;
    height: 20px;
    cursor: nwse-resize;
    background: linear-gradient(135deg, transparent 50%, var(--dc-border-2) 50%);
    border-radius: 0 0 4px 0;
    pointer-events: auto;
    touch-action: none;
  }

  .resize-corner-editor:hover {
    background: linear-gradient(135deg, transparent 50%, var(--dc-border-hi) 50%);
  }
</style>
