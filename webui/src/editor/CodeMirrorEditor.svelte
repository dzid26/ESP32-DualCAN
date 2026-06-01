<script lang="ts">
  import { onDestroy, onMount, untrack } from 'svelte';
  import { acceptCompletion, completionKeymap } from '@codemirror/autocomplete';
  import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands';
  import { EditorState, EditorSelection } from '@codemirror/state';
  import { EditorView, keymap } from '@codemirror/view';
  import { searchKeymap } from '@codemirror/search';
  import { createCodeMirrorExtensions } from './codemirror-setup';

  /* Bound value uses Svelte 5 $bindable() so the parent can write the editor
   * contents (e.g. when loading a script from the device or an example) and
   * also receive every keystroke. */
  let { value = $bindable(''), language = 'berry', height = '380px', onSave, scrollTop = $bindable(0), readOnly = false, gotoLine = $bindable<number | null>(null), autoFocus = false, cursorLine = $bindable(0) }: {
    value: string;
    language?: string;
    height?: string;
    /** Bound to Cmd/Ctrl+S inside the editor; default browser save is suppressed. */
    onSave?: () => void;
    scrollTop?: number;
    readOnly?: boolean;
    /** Set to a 1-based line number to scroll the editor to that line.
     *  Consumed internally (reset to null after scroll). */
    gotoLine?: number | null;
    /** Focus the editor when this is true. Useful for auto-focusing after toggle. */
    autoFocus?: boolean;
    /** Two-way: reports the 1-based cursor line on user interaction; setting it
     *  moves the cursor to that line without scrolling. */
    cursorLine?: number;
  } = $props();

  let host: HTMLDivElement | undefined = $state();
  let view: EditorView | undefined;
  let suppressNextChange = false;
  let viewReady = $state(false);

  let ro: ResizeObserver | undefined;

  onMount(() => {
    view = new EditorView({
      parent: host!,
      state: EditorState.create({
        doc: value,
        extensions: [
          history(),
          createCodeMirrorExtensions(onSave, language),
          readOnly
            ? EditorState.transactionFilter.of(tr =>
                tr.docChanged && (tr.isUserEvent('input') || tr.isUserEvent('delete')) ? [] : [tr]
              )
            : [],
          keymap.of([
            { key: 'Tab', run: acceptCompletion },
            indentWithTab,
            ...completionKeymap,
            ...historyKeymap,
            ...defaultKeymap,
            ...searchKeymap,
          ]),
          EditorView.updateListener.of(update => {
            if (update.docChanged) {
              if (suppressNextChange) {
                suppressNextChange = false;
                return;
              }
              const next = update.state.doc.toString();
              if (next !== value) value = next;
            }
            if (update.selectionSet) {
              cursorLine = update.state.doc.lineAt(update.state.selection.main.from).number;
            }
          }),
        ],
      }),
    });
    /* Sync scroll position upward whenever the user scrolls. */
    view.scrollDOM.addEventListener('scroll', () => {
      scrollTop = view!.scrollDOM.scrollTop;
    });
    /* Restore saved scroll position (e.g. after .be/.bep toggle or initial mount). */
    if (scrollTop > 0) view.scrollDOM.scrollTop = scrollTop;

    /* Observe layout changes (hidden→visible due to .be/.bep toggle) so we
       can restore scroll on the now-visible editor. */
    ro = new ResizeObserver(() => {
      if (!view || !host || host.clientHeight === 0) return;
      const saved = scrollTop;
      if (view.scrollDOM.scrollTop !== saved) {
        view.scrollDOM.scrollTop = saved;
      }
    });
    ro.observe(host!);
    viewReady = true;
  });

  /* Push parent updates into the editor without triggering an echo loop.
     Restore scroll synchronously after parent-initiated content changes —
     the browser hasn't painted yet, so the user sees the final scroll
     position on the first frame (no jump). User typing never hits this
     because the update listener already re-assigned value. */
  $effect(() => {
    if (!view) return;
    const current = view.state.doc.toString();
    if (current !== value) {
      suppressNextChange = true;
      view.dispatch({
        changes: { from: 0, to: current.length, insert: value },
      });
      const saved = untrack(() => scrollTop);
      if (view.scrollDOM.scrollTop !== saved) {
        view.scrollDOM.scrollTop = saved;
      }
    }
  });

  /* Scroll to and select a specific line when gotoLine is set.
     Re-fires when the editor view becomes ready so a goto queued before
     mount still works. */
  $effect(() => {
    void viewReady;
    if (!view || !gotoLine) return;
    try {
      const line = view.state.doc.line(gotoLine);
      view.dispatch({
        selection: EditorSelection.single(line.from, line.to),
        effects: EditorView.scrollIntoView(line.from, { y: 'start' }),
      });
    } catch { /* line out of range */ }
    gotoLine = null;
  });

  /* Auto-focus when autoFocus becomes true (e.g. after toggling to this view). */
  $effect(() => {
    void viewReady;
    if (!view || !autoFocus) return;
    view.focus();
  });

  /* Move cursor to cursorLine when the parent sets it (no scroll). */
  $effect(() => {
    void viewReady;
    if (!view || cursorLine === 0) return;
    const cur = view.state.doc.lineAt(view.state.selection.main.from).number;
    if (cursorLine === cur) return;
    const line = view.state.doc.line(Math.min(cursorLine, view.state.doc.lines));
    view.dispatch({
      selection: EditorSelection.single(line.to),
    });
  });

  onDestroy(() => {
    ro?.disconnect();
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
