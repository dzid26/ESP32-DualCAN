// Lightweight toast notifications — Svelte 5 runes, no external lib.
// Usage: toast.show({ message, severity?, link?, duration? })

export type ToastSeverity = 'info' | 'warn' | 'error';

export interface ToastEntry {
  id: number;
  message: string;
  severity: ToastSeverity;
  link?: { href: string; text: string };
}

export interface ToastOptions {
  message: string;
  severity?: ToastSeverity;
  link?: { href: string; text: string };
  /** 0 disables auto-dismiss; default 6000 ms. */
  duration?: number;
}

class ToastStore {
  list = $state<ToastEntry[]>([]);
  private nextId = 1;

  show(opts: ToastOptions): number {
    const id = this.nextId++;
    this.list = [
      ...this.list,
      {
        id,
        message: opts.message,
        severity: opts.severity ?? 'info',
        link: opts.link,
      },
    ];
    const duration = opts.duration ?? 6000;
    if (duration > 0) {
      setTimeout(() => this.dismiss(id), duration);
    }
    return id;
  }

  dismiss(id: number): void {
    this.list = this.list.filter((t) => t.id !== id);
  }

  clear(): void {
    this.list = [];
  }
}

export const toast = new ToastStore();
