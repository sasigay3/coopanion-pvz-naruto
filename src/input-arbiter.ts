/** FIFO ownership for game input. Not yet connected to the native bridge. */
export interface InputLease {
  /** Claim a validated ID once for this arbiter's lifetime, before native dispatch. */
  claimAction(id: string): 'claimed' | 'duplicate' | 'unavailable';
  release(): void;
}
interface Waiter {
  resolve: (lease: InputLease) => void;
  reject: (error: Error) => void;
  signal?: AbortSignal;
  onAbort?: () => void;
  waiting: boolean;
}

/**
 * A granted lease is not released merely because its signal aborts: the holder
 * must first wait for the native action/cancellation release fence. Releasing
 * early could let an automatic pickup and a model action overlap.
 */
export class InputArbiter {
  private active = false;
  private closed = false;
  private readonly waiters: Waiter[] = [];
  private readonly actionIds = new Set<string>();

  acquire(signal?: AbortSignal): Promise<InputLease> {
    if (this.closed) return Promise.reject(new Error('input arbiter is closed'));
    if (signal?.aborted) return Promise.reject(new Error('input request cancelled'));
    return new Promise((resolve, reject) => {
      const waiter: Waiter = { resolve, reject, signal, waiting: true };
      waiter.onAbort = () => {
        if (!waiter.waiting) return;
        waiter.waiting = false;
        const index = this.waiters.indexOf(waiter);
        if (index !== -1) this.waiters.splice(index, 1);
        signal?.removeEventListener('abort', waiter.onAbort!);
        reject(new Error('input request cancelled'));
        this.pump();
      };
      this.waiters.push(waiter);
      signal?.addEventListener('abort', waiter.onAbort, { once: true });
      if (signal?.aborted) waiter.onAbort();
      this.pump();
    });
  }

  /** Reject queued and future requests; an existing lease remains held until released. */
  close(): void {
    if (this.closed) return;
    this.closed = true;
    for (const waiter of this.waiters.splice(0)) {
      waiter.waiting = false;
      if (waiter.onAbort) waiter.signal?.removeEventListener('abort', waiter.onAbort);
      waiter.reject(new Error('input arbiter is closed'));
    }
  }

  private pump(): void {
    if (this.active || this.closed) return;
    const waiter = this.waiters.shift();
    if (!waiter) return;
    waiter.waiting = false;
    if (waiter.onAbort) waiter.signal?.removeEventListener('abort', waiter.onAbort);
    this.active = true;
    let released = false;
    waiter.resolve(Object.freeze({ claimAction: (id: string) => {
      if (released || this.closed || typeof id !== 'string' || !id || id.length > 128) return 'unavailable' as const;
      if (this.actionIds.has(id)) return 'duplicate' as const;
      // Do not evict IDs: eviction would silently make old actions replayable.
      if (this.actionIds.size >= 10_000) { this.close(); return 'unavailable' as const; }
      this.actionIds.add(id);
      return 'claimed' as const;
    }, release: () => {
      if (released) return;
      released = true;
      this.active = false;
      this.pump();
    } }));
  }
}
