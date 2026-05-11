<script lang="ts">
  import { onDestroy, onMount } from 'svelte';
  import { acceptCompletion, completionKeymap } from '@codemirror/autocomplete';
  import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands';
  import { EditorState } from '@codemirror/state';
  import { EditorView, keymap } from '@codemirror/view';
  import { createCodeMirrorExtensions } from './codemirror-setup';

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
  let view: EditorView | undefined;
  let suppressNextChange = false;

  onMount(() => {
    view = new EditorView({
      parent: host!,
      state: EditorState.create({
        doc: value,
        extensions: [
          history(),
          createCodeMirrorExtensions(onSave, language),
          keymap.of([
            { key: 'Tab', run: acceptCompletion },
            indentWithTab,
            ...completionKeymap,
            ...historyKeymap,
            ...defaultKeymap,
          ]),
          EditorView.updateListener.of(update => {
            if (!update.docChanged) return;
            if (suppressNextChange) {
              suppressNextChange = false;
              return;
            }
            const next = update.state.doc.toString();
            if (next !== value) value = next;
          }),
        ],
      }),
    });
  });

  /* Push parent updates into the editor without triggering an echo loop. */
  $effect(() => {
    if (!view) return;
    const current = view.state.doc.toString();
    if (current !== value) {
      suppressNextChange = true;
      view.dispatch({
        changes: { from: 0, to: current.length, insert: value },
      });
    }
  });

  onDestroy(() => {
    view?.destroy();
  });
</script>

<div bind:this={host} class="editor" style:height></div>

<style>
  .editor {
    width: 100%;
    height: 100%;
    border: 1px solid #333;
    border-radius: 4px;
    overflow: auto;
  }
</style>
