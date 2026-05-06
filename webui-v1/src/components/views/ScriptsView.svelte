<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import type { ScriptInfo } from '../../transport/protocol';
  import SectionHead from '../SectionHead.svelte';
  import MonacoEditor from '../../editor/MonacoEditor.svelte';
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

  let isMobile = $state(false);
  $effect(() => {
    const mq = window.matchMedia('(max-width: 720px)');
    const update = () => { isMobile = mq.matches; };
    update();
    mq.addEventListener('change', update);
    return () => mq.removeEventListener('change', update);
  });

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
    void app.connected;
    if (app.connected) refresh();
    else { scripts = []; }
  });
</script>

<div style="padding: 12px; display: flex; flex-direction: column; flex: 1; min-height: 0; gap: 10px">
  <SectionHead
    title="Automations"
    sub="Berry scripts that run on their own — timers, event handlers, callbacks"
  >
    {#snippet actions()}
      <button class="btn btn--sm" onclick={newScript}><Icon name="up" size={13} />New</button>
      <label class="btn btn--sm btn--info">
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
    style:grid-template-columns={isMobile ? '1fr' : 'minmax(220px, 280px) 1fr'}
  >
    <div class="frame" style="display: flex; flex-direction: column; min-height: 0">
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
              <div style="font-size: 13px; color: var(--dc-text); font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis">
                {s.filename}
              </div>
              <div class="mono" style:font-size="10.5px" style:color={s.errored ? 'var(--dc-err-text)' : 'var(--dc-text-fade)'} title={s.error ?? ''}>
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

    <div class="frame" style="display: flex; flex-direction: column; min-height: 0">
      <div class="frame__head">
        <span class="row-flex" style="min-width: 0; flex: 1">
          <input
            class="inp"
            bind:value={editorFilename}
            placeholder="filename.be"
            style="flex: 1; max-width: 280px; height: 22px; font-size: 12px"
          />
          {#if dirty}<span class="mono" style="color: var(--dc-warn); font-size: 10px">● unsaved</span>{/if}
          <a
            href="https://berry-lang.github.io/"
            target="_blank"
            rel="noopener"
            class="mono"
            title="Berry language docs — syntax, stdlib, examples"
            style="display: inline-flex; align-items: center; gap: 4px; font-size: 10.5px; letter-spacing: 0.12em; text-transform: uppercase; color: var(--dc-text-fade); text-decoration: none; padding: 2px 6px; border: 1px solid var(--dc-border); border-radius: 3px"
          >
            Berry guide
            <span aria-hidden="true" style="font-size: 9px; opacity: 0.7">↗</span>
          </a>
        </span>
        <span class="row-flex">
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

      <div style="flex: 1; min-height: 0; display: flex">
        <MonacoEditor bind:value={code} height="100%" />
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
    gap: 10px;
    padding: 8px 10px 8px 12px;
    border-bottom: 1px solid var(--dc-border);
    cursor: pointer;
  }
</style>
