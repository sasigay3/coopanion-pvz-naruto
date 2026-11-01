import { checkBeforeDispatch, verifyAction, type ActionTicket, type BoardObservation, type NativeTerminal, type Verification } from './action-fence.ts';
import { InputArbiter } from './input-arbiter.ts';

export type GuardedOutcome = Verification | { status: 'blocked'; reason: string };
export interface GuardedDriver<TState, TCommand> {
  /** A fresh immutable observation; the bridge supplies verified session and run identities. */
  observe(): Promise<BoardObservation<TState>>;
  /**
   * Must resolve with releaseConfirmed only after native input is actually released.
   * It must honor cancellation and recheck the session/run at the native boundary.
   */
  dispatch(command: TCommand, ticket: ActionTicket, signal?: AbortSignal): Promise<{
    terminal: NativeTerminal | null;
    releaseConfirmed: boolean;
    /** Correlates the native release acknowledgement to this ticket. */
    releaseFor: string;
  }>;
}

/**
 * Prototype orchestration, not wired to the game. A failed or ambiguous release
 * permanently closes the arbiter so later input cannot overlap an unknown action.
 */
export async function runGuardedAction<TState, TCommand>(options: {
  arbiter: InputArbiter;
  driver: GuardedDriver<TState, TCommand>;
  ticket: ActionTicket;
  command: TCommand;
  now: () => number;
  effect: (before: TState, after: TState) => boolean;
  signal?: AbortSignal;
}): Promise<GuardedOutcome> {
  const { arbiter, driver, ticket, command, now, effect, signal } = options;
  let lease;
  try { lease = await arbiter.acquire(signal); }
  catch { return { status: 'blocked', reason: signal?.aborted ? 'cancelled' : 'input unavailable' }; }
  let releaseUncertain = false;
  try {
    let before: BoardObservation<TState>;
    try { before = await driver.observe(); }
    catch { return { status: 'blocked', reason: 'fresh observation unavailable' }; }
    const decision = checkBeforeDispatch(ticket, before, now(), signal?.aborted === true);
    if (!decision.allowed) return { status: 'blocked', reason: decision.reason };
    releaseUncertain = true;
    let receipt: Awaited<ReturnType<GuardedDriver<TState, TCommand>['dispatch']>>;
    try { receipt = await driver.dispatch(command, ticket, signal); }
    catch { return { status: 'unverified', reason: 'native release not confirmed' }; }
    if (!receipt || receipt.releaseConfirmed !== true || receipt.releaseFor !== ticket.id)
      return { status: 'unverified', reason: 'native release not confirmed' };
    releaseUncertain = false;
    if (!receipt.terminal || receipt.terminal.outcome !== 'executed')
      return verifyAction(ticket, before, receipt.terminal, null, effect);
    let after: BoardObservation<TState>;
    try { after = await driver.observe(); }
    catch { return { status: 'unverified', reason: 'post-action observation unavailable' }; }
    return verifyAction(ticket, before, receipt.terminal, after, effect);
  } finally {
    if (releaseUncertain) arbiter.close();
    lease.release();
  }
}
