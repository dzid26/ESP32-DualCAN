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

  /* Give .cm-scroller a definite pixel height so it becomes an independent
     scroll container. Without this the height:100% chain collapses to auto
     somewhere up the ancestor tree, making .cm-scroller size to its content
     with no overflow — scroll events then bubble up to the grid or frame
     and shift the entire view instead of just the editor. */
  function syncScrollerSize() {
    if (!host || !view) return;
    const h = host.clientHeight;
    if (h > 0) view.scrollDOM.style.height = h + 'px';
  }

  /* Force .cm-content to be as wide as its widest rendered line so that
     overflow-x:auto on .cm-scroller triggers a horizontal scrollbar.
     CM6's default flex-grow:2 defeats any CSS width:min-content or
     width:max-content on .cm-content — the flex algorithm distributes
     remaining space, pushing the element back to scroller width.
     Directly measuring a .cm-line's scrollWidth and setting it as the
     explicit width on .cm-content bypasses the flex calculation entirely. */
  function syncContentWidth() {
    if (!view) return;
    const el = view.contentDOM;
    /* Measure the widest rendered .cm-line — its scrollWidth includes
       text that overflows due to white-space:pre. */
    let max = 0;
    for (const line of el.querySelectorAll('.cm-line')) {
      const w = (line as HTMLElement).scrollWidth;
      if (w > max) max = w;
    }
    if (max > 0) el.style.width = max + 'px';
  }

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
              /* Re-measure after content changes so new long lines trigger
                 the horizontal scrollbar. */
              requestAnimationFrame(syncContentWidth);
            }
            if (update.selectionSet) {
              cursorLine = update.state.doc.lineAt(update.state.selection.main.from).number;
              if (lastErrorLine > 0 && cursorLine !== lastErrorLine) {
                view!.dispatch({ effects: [setErrorLine.of(null)] });
                lastErrorLine = 0;
              }
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

    syncScrollerSize();
    requestAnimationFrame(syncContentWidth);

    /* Observe layout changes (hidden→visible due to .be/.bep toggle, resize)
       so we keep the scroller height/content-width in sync and restore
       scroll position. */
    ro = new ResizeObserver(() => {
      if (!view || !host || host.clientHeight === 0) return;
      syncScrollerSize();
      syncContentWidth();
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
      lastErrorLine = 0;
      view.dispatch({
        changes: { from: 0, to: current.length, insert: value },
        effects: [setErrorLine.of(null)],
      });
      const saved = untrack(() => scrollTop);
      if (view.scrollDOM.scrollTop !== saved) {
        view.scrollDOM.scrollTop = saved;
      }
      requestAnimationFrame(syncContentWidth);
    }
  });

  /* Scroll to and highlight a specific line when gotoLine is set.
     Re-fires when the editor view becomes ready so a goto queued before
     mount still works. */
  $effect(() => {
    void viewReady;
    if (!view || !gotoLine) return;
    try {
      const line = view.state.doc.line(gotoLine);
      lastErrorLine = gotoLine;
      view.dispatch({
        selection: EditorSelection.single(line.from, line.to),
        effects: [
          EditorView.scrollIntoView(line.from, { y: 'nearest' }),
          setErrorLine.of(gotoLine),
        ],
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
    /* CM's .cm-scroller handles all scrolling internally — don't intercept it */
    overflow: hidden;
  }
</style>
