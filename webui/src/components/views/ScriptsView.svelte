<script lang="ts">
  import { Switch, AlertDialog } from 'bits-ui';
  import { app } from '../../lib/store.svelte';
  import { dbcStore } from '../../dbcStore.svelte';
  import type { ScriptInfo } from '../../transport/protocol';
  import { examples } from '../../examples';
  import { preprocessScript } from '../../lib/preprocessor';
  import type { Message } from '../../dbc/parser';
  import SectionHead from '../SectionHead.svelte';
  import CodeMirrorEditor from '../../editor/CodeMirrorEditor.svelte';
  import Icon from '../Icon.svelte';
  import ScriptingGuide from '../ScriptingGuide.svelte';


  const NEW_SCRIPT = '# Write your Berry script here\n\ndef setup()\n  print("hello from setup")\nend\n';

  let scripts = $state<ScriptInfo[]>([]);
  let listError = $state<string | null>(null);
  let busy = $state(false);
  function refreshWifiIp(): void { app.refreshWifiIp(); }

  let selFn = $state<string | null>(null);
  let code = $state<string>(NEW_SCRIPT);
  let savedCode = $state<string>(NEW_SCRIPT);
  let editorFilename = $state<string>('my_script.be');

  let confirmRemove = $state<string | null>(null);
  const dirty = $derived(code !== savedCode);
  let lastScriptStatusSeq = 0;
  let bepNavigationActive = $state(false);
  const canShowInstalled = $derived(selFn !== null && (!dirty || bepNavigationActive));

  let editorScrollTop = $state(0);

  let scrollPositions = new Map<string, number>();

  let isMobile = $state(false);
  let showPreprocessed = $state(false);
  let showGuide = $state(false);
  let preprocessedCode = $state<string>('');
  let deviceBepCode = $state<string | null>(null);
  let skipPreprocessedEffect = $state(false);
  let editorPaneEl = $state<HTMLDivElement | undefined>();
  let containerEl = $state<HTMLDivElement | undefined>();
  let watermarkAngle = $state(45);
  let watermarkFontSize = $state(120);

  // Drag-to-stretch state for mobile
  let stretchOffset = $state(0);
  let dragging = $state(false);
  let dragStartY = $state(0);
  let dragStartStretch = $state(0);

  function onDragStart(e: PointerEvent) {
    const t = e.target as HTMLElement;
    // Don't intercept clicks/drags on interactive elements or scrollable text areas
    if (t.closest('button, select, input, label, a, .cm-editor, .cm-scroller, .srow, .log-link, [role="button"]')) return;
    
    // Don't intercept if they are scrolling the Installed list itself
    const frameList = t.closest('.frame');
    if (frameList && frameList.scrollHeight > frameList.clientHeight) return;

    dragging = true;
    dragStartY = e.clientY;
    dragStartStretch = stretchOffset;
    if (containerEl) containerEl.setPointerCapture(e.pointerId);
  }

  function onDragMove(e: PointerEvent) {
    if (!dragging) return;
    // e.clientY decreases when dragging UP, so delta is positive
    const delta = dragStartY - e.clientY;
    stretchOffset = Math.max(0, Math.min(250, dragStartStretch + delta));
  }

  function onDragEnd(e: PointerEvent) {
    dragging = false;
    if (containerEl && containerEl.hasPointerCapture(e.pointerId)) {
      containerEl.releasePointerCapture(e.pointerId);
    }
  }

  $effect(() => {
    const el = editorPaneEl;
    if (!el) return;
    const ro = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      if (width === 0 || height === 0) return;
      const angle = Math.atan2(height, width) * (180 / Math.PI);
      watermarkAngle = angle;
      const d = Math.sqrt(width * width + height * height);
      watermarkFontSize = Math.round(d / 8);
    });
    ro.observe(el);
    return () => ro.disconnect();
  });

  let gotoLine = $state<number | null>(null);
  let preprocessedGotoLine = $state<number | null>(null);
  let sourceCursorLine = $state(0);
  let preprocessedCursorLine = $state(0);

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
      if (!selFn && scripts.length > 0 && code === NEW_SCRIPT) {
        const lastFn = app.lastScriptFilename;
        const target = lastFn && scripts.find(s => s.filename === lastFn)
          ? lastFn
          : scripts[0].filename;
        await loadScript(target);
      }
    } catch (e) {
      listError = e instanceof Error ? e.message : String(e);
    }
  }

  async function loadScript(filename: string): Promise<boolean> {
    if (dirty && !confirm('Discard unsaved changes?')) return false;
    if (selFn) scrollPositions.set(selFn, editorScrollTop);
    selFn = filename;
    editorFilename = filename;
    app.setLastScriptFilename(filename);
    try {
      const r = await app.proto.readScript(filename);
      code = r.code;
      savedCode = r.code;
      bepNavigationActive = false;
      try {
        const bep = await app.proto.readScript(filename.replace(/\.be$/, '.bep'));
        deviceBepCode = bep.code;
        preprocessedCode = bep.code;
      } catch {
        deviceBepCode = null;
      }
    } catch (e) {
      app.pushLog(`scripts: read failed — ${e instanceof Error ? e.message : e}`, 'error', 'scripts');
      return false;
    }
    const saved = scrollPositions.get(filename);
    if (saved !== undefined) editorScrollTop = saved;
    return true;
  }

  /** Collect full Message[] from all loaded buses for preprocessing. */
  function getFullMessages(): Message[] {
    return Object.values(dbcStore.fullMessages).flat();
  }

  async function save(): Promise<void> {
    const fn = normalizeScriptFilename((selFn ?? editorFilename).trim());
    if (!fn) { app.pushLog('scripts: filename required', 'error', 'scripts'); return; }
    const wasEnabled = scripts.find(s => s.filename === fn)?.enabled ?? false;
    busy = true;
    try {
      const codeChanged = code !== savedCode;
      // Upload .be immediately so the save feels fast.
      await app.proto.writeScript(fn, code, undefined);
      savedCode = code;
      selFn = fn;
      editorFilename = fn;
      app.setLastScriptFilename(fn);
      app.pushLog(`scripts: saved ${fn}`, 'info', 'scripts');
      await refresh();

      // Then preprocess and upload .bep in the background if needed.
      const result = preprocessScript(code, getFullMessages());
      if (result.errors.length > 0) {
        const errMsg = result.errors.join('; ');
        app.pushLog(`scripts: preprocessing warnings — ${errMsg}`, 'warn', 'scripts');
      }
      if (result.code !== code) {
        await app.proto.writeScript(fn, code, result.code);
        if (result.errors.length === 0) {
          app.pushLog(`scripts: preprocessed runtime updated`, 'info', 'scripts');
        }
      } else if (!codeChanged && deviceBepCode !== null) {
        // No DBC and source unchanged — preserve the existing .bep from device
        await app.proto.writeScript(fn, code, deviceBepCode);
      }
      // Reload the saved script from device to confirm clean state.
      await loadScript(fn);
      // Re-enable if it was running before editing.
      if (wasEnabled) await enable(fn);
      busy = false;
    } catch (e) {
      app.pushLog(`scripts: upload failed — ${e instanceof Error ? e.message : e}`, 'error', 'scripts');
    } finally {
      busy = false;
    }
  }

  async function enable(filename: string): Promise<void> {
    busy = true;
    scripts = scripts.map(s => s.filename === filename
      ? { ...s, enabled: true } : s);
    try {
      await app.proto.enableScript(filename);
      scripts = scripts.map(s => s.filename === filename
        ? { ...s, enabled: true, errored: false } : s);
      app.pushLog(`scripts: enabled ${filename}`, 'info', 'scripts');
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      app.pushLog(`scripts: enable failed — ${msg}`, 'error', 'scripts');
      scripts = scripts.map(s => s.filename === filename
        ? { ...s, enabled: false, errored: true, error: msg } : s);
    } finally { busy = false; }
  }

  async function disable(filename: string): Promise<void> {
    busy = true;
    scripts = scripts.map(s => s.filename === filename
      ? { ...s, enabled: false } : s);
    try {
      await app.proto.disableScript(filename);
      app.pushLog(`scripts: disabled ${filename}`, 'info', 'scripts');
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      app.pushLog(`scripts: disable failed — ${msg}`, 'error', 'scripts');
      scripts = scripts.map(s => s.filename === filename
        ? { ...s, enabled: true, errored: false } : s);
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
      app.pushLog(`scripts: deleted ${filename}`, 'info', 'scripts');
    } catch (e) {
      app.pushLog(`scripts: delete failed — ${e instanceof Error ? e.message : e}`, 'error', 'scripts');
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
    savedCode = NEW_SCRIPT;
  }

  function loadExample(e: Event): void {
    const sel = e.currentTarget as HTMLSelectElement;
    const fn = sel.value;
    sel.value = '';
    if (!fn) return;
    if (dirty && !confirm('Discard unsaved changes?')) return;
    const ex = findExample(fn);
    if (!ex) return;
    code = ex.code;
    savedCode = ex.code;
    selFn = null;
    editorFilename = ex.filename.replace(/\.be$/, '');
  }

  function revert(): void {
    code = savedCode;
  }

  function importFile(e: Event): void {
    const input = e.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      code = reader.result as string;
      savedCode = code;
      editorFilename = normalizeScriptFilename(file.name);
      selFn = null;
      app.pushLog(`scripts: imported ${file.name}`, 'info', 'scripts');
    };
    reader.readAsText(file);
  }

  // Re-list on mount, reconnect, enter view, or kill switch.
  $effect(() => {
    void app.connected; void app.scriptsVersion; void app.view;
    if (app.connected && app.view === 'scripts') { refresh(); refreshWifiIp(); }
    else if (!app.connected) { scripts = []; }
  });

  // Update preprocessed code display
  $effect(() => {
    void showPreprocessed; void code; void dbcStore.fullMessages; void canShowInstalled; void skipPreprocessedEffect;
    if (skipPreprocessedEffect) {
      skipPreprocessedEffect = false;
    } else if (showPreprocessed && canShowInstalled) {
      // Installed tab: preprocessedCode already set by loadScript / error handler
      preprocessedCode = deviceBepCode ?? '(no preprocessed file on device)';
    } else if (showPreprocessed) {
      // Preview tab: preprocess client-side
      const messages = getFullMessages();
      if (messages.length > 0) {
        const result = preprocessScript(code, messages);
        preprocessedCode = result.code;
      } else {
        preprocessedCode = '(no DBC loaded - pick your car)';
      }
    }
  });

  $effect(() => {
    const patch = app.scriptStatusPatch;
    if (!patch || patch.seq === lastScriptStatusSeq) return;
    lastScriptStatusSeq = patch.seq;
    scripts = scripts.map(s => {
      if (s.filename !== patch.filename) return s;
      return { ...s, enabled: patch.enabled, errored: patch.clearError ? false : s.errored, error: patch.clearError ? undefined : s.error };
    });
  });

  $effect(() => {
    const fn = app.pendingExample;
    if (!fn) return;
    app.pendingExample = null;
    if (dirty && !confirm('Discard unsaved changes and load the example?')) return;
    const ex = findExample(fn);
    if (ex) {
      code = ex.code;
      savedCode = '';
      selFn = null;
    editorFilename = ex.filename;
    }
  });

  // Navigate editor to a specific line from a log click.
  $effect(() => {
    const target = app.gotoEditorLine;
    if (!target) return;
    app.gotoEditorLine = null;

    const isBep = target.filename.endsWith('.bep');
    const fn = target.filename.replace(/\.bep$/, '.be');

    const targetLine = target.line;
    function doScroll() {
      if (isBep) {
        if (dirty && fn === selFn) {
          // Error link for .bep while dirty: fetch device .bep directly,
          // prevent effect from overriding, and mark Installed tab
          preprocessedCode = '(loading .bep from device\u2026)';
          bepNavigationActive = true;
          skipPreprocessedEffect = true;
          app.proto.readScript(target!.filename)
            .then(r => {
              preprocessedCode = r.code;
              deviceBepCode = r.code;
              skipPreprocessedEffect = true;
              preprocessedGotoLine = targetLine;
            })
            .catch(() => {
              preprocessedCode = '(no preprocessed file on device)';
            });
        } else {
          bepNavigationActive = true;
        }
        showPreprocessed = true;
        requestAnimationFrame(() => { preprocessedGotoLine = targetLine; });
      } else {
        showPreprocessed = false;
        bepNavigationActive = false;
        gotoLine = targetLine;
      }
    }

    if (fn !== selFn) {
      loadScript(fn).then(ok => ok && doScroll()).catch(() => {});
    } else {
      doScroll();
    }
  });

  /* Sync cursor line between .be and .bep editors on toggle (no scroll). */
  $effect(() => {
    if (showPreprocessed) {
      if (sourceCursorLine > 0) preprocessedCursorLine = sourceCursorLine;
    } else {
      if (preprocessedCursorLine > 0) sourceCursorLine = preprocessedCursorLine;
    }
  });

  function normalizeScriptFilename(filename: string): string {
    if (!filename.includes('.')) return filename + '.be';
    return filename;
  }

  function stripExt(name: string): string {
    return name.endsWith('.be') ? name.slice(0, -3) : name;
  }

  function findExample(filename: string) {
    const normalized = normalizeScriptFilename(filename);
    return examples.find(x => x.filename === normalized);
  }
</script>

<div
  bind:this={containerEl}
  class="scripts-container"
  style="padding: 12px; display: flex; flex-direction: column; flex: 1; min-height: 0; gap: 10px; touch-action: {isMobile ? 'none' : 'auto'}"
  style:overflow-y={isMobile ? 'hidden' : 'visible'}
  onpointerdown={onDragStart}
  onpointermove={onDragMove}
  onpointerup={onDragEnd}
  onpointercancel={onDragEnd}
>
  <div style="overflow: hidden; display: flex; flex-direction: column; gap: 10px; flex-shrink: 0; max-height: {Math.max(0, 100 - stretchOffset)}px; opacity: {Math.max(0, 1 - stretchOffset/60)}; margin-bottom: {Math.max(-20, -10 - Math.max(0, stretchOffset - 90))}px">
    <SectionHead
      title="Automations"
      sub="Berry scripts that run on their own — timers, event handlers, callbacks"
    >
      {#snippet actions()}
        <button class="btn btn--sm" onclick={() => showGuide = true} title="Scripting guide — writing and using scripts" style="flex: 1 1 30px; overflow: hidden; justify-content: center">
          <svg width="12" height="12" viewBox="0 0 14 14" fill="none" style="flex-shrink: 0">
            <path d="M2 3h10M2 7h10M2 11h7" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
          </svg><span style="overflow: hidden; text-overflow: ellipsis; white-space: nowrap">Scripting Guide</span>
        </button>
        <button class="btn btn--sm" style="flex-shrink: 0" onclick={newScript}><Icon name="up" size={13} />New</button>
        <select
          class="sel"
          onchange={loadExample}
          title="Load a bundled Berry example into the editor (firmware/scripts_examples)"
          style="flex: 1 1 auto; flex-shrink: 0; min-width: 80px; max-width: 160px; padding-right: 24px"
        >
          <option value="">Load example…</option>
          {#each examples as ex}
            <option value={ex.filename}>{ex.name}</option>
          {/each}
        </select>
        <label class="btn btn--sm btn--info" style="flex-shrink: 0" title="Read a .be file from disk into the editor">
          Import
          <input type="file" accept=".be;text/plain" onchange={importFile} style="display: none" />
        </label>
      {/snippet}
    </SectionHead>

    {#if listError}
      <div class="empty" style="border-color: var(--dc-err-border); color: var(--dc-err-text)">{listError}</div>
    {/if}
  </div>

  <div
    style:display={isMobile ? 'flex' : 'grid'}
    style:flex-direction={isMobile ? 'column' : undefined}
    style:gap="10px"
    style:flex="1"
    style:min-height="0"
    style:overflow={isMobile ? 'visible' : 'auto'}
    style:grid-template-columns={isMobile ? undefined : 'minmax(180px, 240px) 1fr'}
    style:grid-template-rows={isMobile ? undefined : '1fr'}
  >
    <div
      class="frame"
      style="display: flex; flex-direction: column; overflow: hidden"
      style:min-height={isMobile ? '0' : '130px'}
      style:max-height={isMobile ? Math.max(0, 123 - Math.max(0, stretchOffset - 80)) + 'px' : 'none'}
      style:opacity={isMobile ? Math.max(0, 1 - Math.max(0, stretchOffset - 80) / 100) : 1}
      style:flex-shrink={isMobile ? '0' : undefined}
      style:margin-bottom={isMobile ? Math.max(-10, -Math.max(0, stretchOffset - 193)) + 'px' : '0'}
    >
      <div class="frame__head">
        Installed <span class="ghost mono">{scripts.length}</span>
        {#if app.wifiIp}
          <a href="http://{app.wifiIp}/scripts/" target="_blank" rel="noopener" class="ghost" style="margin-left: auto; font-size: 11px; color: var(--dc-accent); text-decoration: none">
            Browse files &rarr;
          </a>
        {:else}
          <span class="ghost" style="margin-left: auto; font-size: 11px; color: var(--dc-text-fade)" title="Connect to WiFi in Settings to browse scripts via HTTP">
            Browse files
          </span>
        {/if}
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
            <Switch.Root
              checked={s.enabled}
              onCheckedChange={(checked) => checked ? enable(s.filename) : disable(s.filename)}
              onclick={(e: MouseEvent) => e.stopPropagation()}
              class={'tog ' + (s.enabled ? 'tog--on' : '')}
              title={s.enabled ? 'Disable' : 'Enable'}
            >
              <Switch.Thumb class="tog__knob" />
            </Switch.Root>
              <div style="min-width: 0">
                <div style="font-size: 12px; color: var(--dc-text); font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis">
                  {stripExt(s.filename)}
                </div>
              <div class="mono" style:font-size="10px" style:color={s.errored ? 'var(--dc-err-text)' : 'var(--dc-text-fade)'} title={s.error ?? ''}>
                {s.errored ? 'error' : s.enabled ? 'running' : 'disabled'}
              </div>
            </div>
            <button
              class="btn btn--sm btn--ghost btn--icon"
              title={`Uninstall ${stripExt(s.filename)}`}
              aria-label={`Uninstall ${stripExt(s.filename)}`}
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
      style="display: flex; flex-direction: column; overflow: hidden; position: relative"
      style:flex={isMobile ? '1' : undefined}
      style:min-height={isMobile ? '180px' : undefined}
    >
      {#if isMobile}
        <div class="row-flex" style="flex-wrap: wrap; gap: 4px; padding: 6px 10px; border-bottom: 1px solid var(--dc-border); touch-action: pan-y;">
          <button class="btn btn--sm" style="touch-action: pan-y;"
            onclick={() => { app.pendingAiScript = { filename: selFn ?? editorFilename, code }; app.setView('ai'); }}
            title="Send this script to the AI assistant for editing">
            <Icon name="sparkle" size={16} /> AI edit
          </button>
          <button class="btn btn--sm" style="touch-action: pan-y;" onclick={save} disabled={!app.connected || busy} title="Upload and preprocess">
            <Icon name="up" size={13} />Save
          </button>
          <button class="btn btn--sm" style="touch-action: pan-y;" onclick={revert} disabled={!dirty}>Revert</button>
        </div>
      {/if}
      <div class="script-tabs" style="touch-action: pan-y;">
        <span class="row-flex" style="gap: 6px; min-width: 0; margin-right: auto">
          <input
            class="inp"
            value={showPreprocessed ? editorFilename.replace(/\.be$/, '.bep') : editorFilename}
            oninput={(e) => { editorFilename = (e.currentTarget as HTMLInputElement).value.replace(/\.bep$/, '.be'); }}
            placeholder={showPreprocessed ? 'filename.bep' : 'filename.be'}
            style="width: 220px; min-width: 0; max-width: 280px; height: 22px; font-size: 12px"
          />
          {#if dirty}<span class="mono" style="color: var(--dc-warn); font-size: 10px; white-space: nowrap">● unsaved</span>{/if}
        </span>
        <button
          class="script-tab"
          style="touch-action: pan-y;"
          class:script-tab--active={!showPreprocessed}
          onclick={() => { showPreprocessed = false; bepNavigationActive = false; }}
          title="Edit the the source code">
          Source
        </button>
        <button
          class="script-tab"
          style="touch-action: pan-y;"
          class:script-tab--active={showPreprocessed}
          onclick={() => { showPreprocessed = true; bepNavigationActive = false; }}
          title={canShowInstalled ? 'View the preprocessed script running on the device' : 'Preview what preprocessing'}>
          {canShowInstalled ? 'Installed' : 'Preview'}
        </button>
        {#if !isMobile}
        <span class="row-flex" style="flex-wrap: wrap; gap: 4px; margin-left: auto">
          <button class="btn btn--sm"
            onclick={() => { app.pendingAiScript = { filename: selFn ?? editorFilename, code }; app.setView('ai'); }}
            title="Send this script to the AI assistant for editing">
            <Icon name="sparkle" size={16} /> AI edit
          </button>
          <button class="btn btn--sm" onclick={save} disabled={!app.connected || busy} title="Upload and preprocess">
            <Icon name="up" size={13} />Save
          </button>
          <button class="btn btn--sm" onclick={revert} disabled={!dirty}>Revert</button>
        </span>
        {/if}
      </div>

      <div style="flex: 1; min-height: 0; min-width: 0; display: flex">
        <div bind:this={editorPaneEl} class:ce-hide={!showPreprocessed} style="flex:1;min-height:0;min-width:0;display:flex;position:relative;overflow:hidden">
          <CodeMirrorEditor bind:value={preprocessedCode} height="100%" bind:scrollTop={editorScrollTop} readOnly={true} bind:gotoLine={preprocessedGotoLine} autoFocus={showPreprocessed} bind:cursorLine={preprocessedCursorLine} />
          <span class="watermark" style="transform:rotate(-{watermarkAngle}deg);font-size:{watermarkFontSize}px">{canShowInstalled ? 'Installed' : 'Preview'}</span>
        </div>
        <div class:ce-hide={showPreprocessed} style="flex:1;min-height:0;min-width:0;display:flex">
          <CodeMirrorEditor bind:value={code} height="100%" onSave={save} bind:scrollTop={editorScrollTop} bind:gotoLine autoFocus={!showPreprocessed} bind:cursorLine={sourceCursorLine} />
        </div>
      </div>
    </div>
  </div>

  <AlertDialog.Root open={confirmRemove !== null} onOpenChange={(v) => !v && (confirmRemove = null)}>
    <AlertDialog.Portal>
      <AlertDialog.Overlay class="ad-overlay" />
      <AlertDialog.Content class="frame ad-content"
        onOpenAutoFocus={(e) => { e.preventDefault(); setTimeout(() => document.getElementById('ad-uninstall-action')?.focus(), 20); }}
      >
        <div class="frame__head" style="color: var(--dc-err-text)">
          <AlertDialog.Title style="margin: 0; font-size: inherit; font-weight: inherit;" class="row-flex">
            <Icon name="trash" size={13} /><span>Uninstall script</span>
          </AlertDialog.Title>
          <AlertDialog.Cancel class="btn btn--sm btn--ghost btn--icon" aria-label="Cancel">
            <Icon name="x" size={13} />
          </AlertDialog.Cancel>
        </div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
          <AlertDialog.Description style="margin: 0; font-size: 13px; color: var(--dc-text-dim); line-height: 1.5">
            Remove <strong style="color: var(--dc-text); font-family: var(--dc-font-mono)">{confirmRemove}</strong> from the device?
          </AlertDialog.Description>
          <p style="margin: 0; font-size: 12px; color: var(--dc-text-fade); line-height: 1.45">
            The Berry source and any persisted state will be erased. This cannot be undone unless you reinstall from a backup or the gallery.
          </p>
          <div class="row-flex" style="justify-content: flex-end; margin-top: 4px">
            <AlertDialog.Cancel class="btn btn--sm btn--ghost">Cancel</AlertDialog.Cancel>
            <AlertDialog.Action
              id="ad-uninstall-action"
              class="btn btn--sm btn--danger"
              onclick={() => uninstall(confirmRemove!)}
              disabled={busy}
            >
              <Icon name="trash" size={13} />Uninstall
            </AlertDialog.Action>
          </div>
        </div>
      </AlertDialog.Content>
    </AlertDialog.Portal>
  </AlertDialog.Root>
</div>

{#if showGuide}
  <ScriptingGuide onClose={() => showGuide = false} />
{/if}

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

  .script-tabs {
    display: flex;
    align-items: center;
    gap: 0;
    border-bottom: 1px solid var(--dc-border);
    padding: 0 10px;
    flex-shrink: 0;
  }
  .script-tab {
    padding: 8px 14px;
    border: none;
    background: transparent;
    color: var(--dc-text-dim);
    cursor: pointer;
    font-size: 11px;
    font-family: inherit;
    border-bottom: 2px solid transparent;
    margin-bottom: -1px;
    transition: color 0.15s, border-color 0.15s;
  }
  .script-tab:hover {
    color: var(--dc-text);
  }
  .script-tab--active {
    color: var(--dc-text);
    border-bottom-color: var(--dc-text);
  }
  .ce-hide { display: none !important; }
  :global(.ad-overlay) {
    position: fixed; inset: 0; background: rgba(10, 8, 4, 0.55); z-index: 200;
  }
  :global(.ad-content) {
    position: fixed; z-index: 201;
    top: 50%; left: 50%; transform: translate(-50%, -50%);
    width: min(420px, 92vw);
  }
  .scripts-container {
    scrollbar-width: none;
    -ms-overflow-style: none;
  }
  .scripts-container::-webkit-scrollbar {
    display: none;
  }
  .watermark {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    pointer-events: none;
    font-weight: 900;
    color: var(--dc-err-text);
    opacity: 0.06;
    white-space: nowrap;
    user-select: none;
    z-index: 1;
    font-family: var(--dc-font-mono);
    text-transform: uppercase;
    letter-spacing: 0.1em;
  }
</style>
