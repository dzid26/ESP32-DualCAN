// Lightweight toast notifications — Svelte 5 runes, no external lib.
// Usage: toast.show({ message, severity?, link?, duration? })

export type ToastSeverity = 'info' | 'warn' | 'error';

export interface ToastEntry {
  id: number;
  message: string;
  severity: ToastSeverity;
  link?: { href: string; text: string };
  flash?: boolean;
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
  private timers = new Map<number, ReturnType<typeof setTimeout>>();
  private flashTimers = new Map<number, ReturnType<typeof setTimeout>>();

  show(opts: ToastOptions): number {
    const severity = opts.severity ?? 'info';
    const existing = this.list.find((t) =>
      t.message === opts.message &&
      t.severity === severity &&
      ((t.link?.href ?? null) === (opts.link?.href ?? null)) &&
      ((t.link?.text ?? null) === (opts.link?.text ?? null))
    );

    const duration = opts.duration ?? 6000;
    if (existing) {
      this.resetTimer(existing.id, duration);
      this.flash(existing.id);
      return existing.id;
    }

    const id = this.nextId++;
    this.list = [
      ...this.list,
      {
        id,
        message: opts.message,
        severity,
        link: opts.link,
      },
    ];

    if (duration > 0) {
      this.timers.set(id, setTimeout(() => this.dismiss(id), duration));
    }
    return id;
  }

  private resetTimer(id: number, duration: number): void {
    const existing = this.timers.get(id);
    if (existing) {
      clearTimeout(existing);
    }
    if (duration > 0) {
      this.timers.set(id, setTimeout(() => this.dismiss(id), duration));
    } else {
      this.timers.delete(id);
    }
  }

  private flash(id: number): void {
    this.list = this.list.map((t) =>
      t.id === id ? { ...t, flash: true } : t
    );

    const existing = this.flashTimers.get(id);
    if (existing) {
      clearTimeout(existing);
    }
    this.flashTimers.set(id, setTimeout(() => {
      this.list = this.list.map((t) =>
        t.id === id ? { ...t, flash: false } : t
      );
      this.flashTimers.delete(id);
    }, 260));
  }

  dismiss(id: number): void {
    this.timers.get(id) && clearTimeout(this.timers.get(id));
    this.flashTimers.get(id) && clearTimeout(this.flashTimers.get(id));
    this.timers.delete(id);
    this.flashTimers.delete(id);
    this.list = this.list.filter((t) => t.id !== id);
  }

  clear(): void {
    this.timers.forEach((timer) => clearTimeout(timer));
    this.flashTimers.forEach((timer) => clearTimeout(timer));
    this.timers.clear();
    this.flashTimers.clear();
    this.list = [];
  }
}

export const toast = new ToastStore();
