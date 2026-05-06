<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { monaco, registerBerry } from './monaco-setup';

  /* Bound value uses Svelte 5 $bindable() so the parent can write the editor
   * contents (e.g. when loading a script from the device or an example) and
   * also receive every keystroke. */
  let { value = $bindable(''), language = 'berry', height = '380px', onSave }: {
    value: string;
    language?: string;
    height?: string;
    /** Bound to Cmd/Ctrl+S inside the editor; default browser save is suppressed. */
    onSave?: () => void;
  } = $props();

  let host: HTMLDivElement | undefined = $state();
  let editor: ReturnType<typeof monaco.editor.create> | undefined;
  let suppressNextChange = false;

  onMount(() => {
    registerBerry();
    editor = monaco.editor.create(host!, {
      value,
      language,
      theme: 'dorky-dark',
      automaticLayout: true,
      fontFamily: "'Fira Code', 'Cascadia Code', monospace",
      fontSize: 13,
      minimap: { enabled: false },
      scrollBeyondLastLine: false,
      tabSize: 2,
      lineNumbers: 'on',
      renderWhitespace: 'selection',
    });
    editor.onDidChangeModelContent(() => {
      if (suppressNextChange) { suppressNextChange = false; return; }
      const v = editor!.getValue();
      if (v !== value) value = v;
    });

    /* Cmd/Ctrl+S → call onSave; suppresses the browser's "save page" dialog. */
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => {
      onSave?.();
    });
  });

  /* Push parent updates into the editor without triggering an echo loop. */
  $effect(() => {
    if (!editor) return;
    if (editor.getValue() !== value) {
      suppressNextChange = true;
      editor.setValue(value);
    }
  });

  onDestroy(() => {
    editor?.dispose();
  });
</script>

<div bind:this={host} class="editor" style:height></div>

<style>
  .editor {
    width: 100%;
    border: 1px solid #333;
    border-radius: 4px;
    overflow: hidden;
  }
</style>
