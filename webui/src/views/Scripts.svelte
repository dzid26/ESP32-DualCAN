<script lang="ts">
  import type { Transport } from '../transport/types';
  import type { Protocol, ScriptInfo } from '../transport/protocol';

  let { transport, proto }: { transport: Transport; proto: Protocol } = $props();

  let code = $state('# Write your Berry script here\ndef setup()\n  print("hello")\nend\n\ndef loop()\n  # Runs every 100ms on the device\nend\n');
  let filename = $state('my_script.be');
  let scripts = $state<ScriptInfo[]>([]);
  let status = $state('');
  let busy = $state(false);

  async function refresh() {
    if (!transport.connected) return;
    try {
      const res = await proto.listScripts();
      scripts = res.scripts;
    } catch (e: any) {
      status = `list failed: ${e.message}`;
    }
  }

  async function upload() {
    if (!filename.trim()) { status = 'filename required'; return; }
    busy = true;
    status = 'uploading...';
    try {
      await proto.writeScript(filename, code);
      status = 'uploaded';
      await refresh();
    } catch (e: any) {
      status = `upload failed: ${e.message}`;
    } finally {
      busy = false;
    }
  }

  async function enable(fn: string) {
    busy = true;
    status = `enabling ${fn}...`;
    try {
      await proto.enableScript(fn);
      status = `enabled ${fn}`;
      await refresh();
    } catch (e: any) {
      status = `enable failed: ${e.message}`;
    } finally {
      busy = false;
    }
  }

  async function disable(fn: string) {
    busy = true;
    status = `disabling ${fn}...`;
    try {
      await proto.disableScript(fn);
      status = `disabled ${fn}`;
      await refresh();
    } catch (e: any) {
      status = `disable failed: ${e.message}`;
    } finally {
      busy = false;
    }
  }

  async function toggleScript(script: ScriptInfo, checked: boolean) {
    if (checked) {
      await enable(script.filename);
    } else {
      await disable(script.filename);
    }
  }

  async function loadForEdit(fn: string) {
    try {
      const res = await proto.readScript(fn);
      code = res.code;
      filename = fn;
      status = `loaded ${fn}`;
    } catch (e: any) {
      status = `read failed: ${e.message}`;
    }
  }

  $effect(() => {
    if (transport.connected) refresh();
  });
</script>

<div class="scripts">
  <h2>Scripts</h2>
  <p>Edit and deploy Berry automation scripts to the device.</p>

  <div class="row">
    <input type="text" bind:value={filename} placeholder="filename.be" />
    <button onclick={upload} disabled={!transport.connected || busy}>Upload</button>
    <button onclick={() => enable(filename)} disabled={!transport.connected || busy}>Enable</button>
    <button onclick={() => disable(filename)} disabled={!transport.connected || busy}>Disable</button>
  </div>

  <textarea bind:value={code} rows="16" spellcheck="false"></textarea>

  <p class="status">{status}</p>

  {#if scripts.length > 0}
    <h3>Scripts on device</h3>
    <ul class="script-list">
      {#each scripts as s}
        <li>
          <span class="fn">{s.filename}</span>
          {#if s.enabled}<span class="tag on">enabled</span>{/if}
          {#if s.errored}<span class="tag err" title={s.error}>error</span>{/if}
          <button class="small" onclick={() => loadForEdit(s.filename)}>edit</button>
          <label class="toggle">
            <input
              type="checkbox"
              checked={s.enabled}
              disabled={!transport.connected || busy}
              onchange={(e) => toggleScript(s, (e.currentTarget as HTMLInputElement).checked)}
            />
            <span>{s.enabled ? 'on' : 'off'}</span>
          </label>
        </li>
      {/each}
    </ul>
  {/if}
</div>

<style>
  .row {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.5rem;
  }
  input[type="text"] {
    flex: 1;
    background: #0d0d1a;
    color: #e0e0e0;
    border: 1px solid #333;
    border-radius: 4px;
    padding: 0.4rem 0.6rem;
    font-family: monospace;
  }
  textarea {
    width: 100%;
    font-family: 'Fira Code', 'Cascadia Code', monospace;
    font-size: 0.9rem;
    background: #0d0d1a;
    color: #e0e0e0;
    border: 1px solid #333;
    border-radius: 4px;
    padding: 0.75rem;
    resize: vertical;
    box-sizing: border-box;
  }
  button {
    background: #2a4a2a;
    border: 1px solid #4a4;
    color: #ccc;
    padding: 0.4rem 1rem;
    border-radius: 4px;
    cursor: pointer;
  }
  button.small {
    padding: 0.2rem 0.5rem;
    font-size: 0.8rem;
  }
  button:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
  .status {
    color: #888;
    font-size: 0.85rem;
    min-height: 1.2em;
  }
  .script-list {
    list-style: none;
    padding: 0;
  }
  .script-list li {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.3rem 0;
    border-bottom: 1px solid #222;
  }
  .fn {
    font-family: monospace;
    flex: 1;
  }
  .tag {
    font-size: 0.75rem;
    padding: 0.1rem 0.4rem;
    border-radius: 3px;
  }
  .tag.on { background: #1a4a1a; color: #8f8; }
  .tag.err { background: #4a1a1a; color: #f88; }
  .toggle {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    font-size: 0.85rem;
    color: #bbb;
  }
  .toggle input[type="checkbox"] {
    width: 1rem;
    height: 1rem;
    accent-color: #56b870;
  }
</style>
