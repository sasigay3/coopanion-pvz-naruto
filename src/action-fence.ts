/** Version-independent action guards. The native bridge and World are not wired yet. */
export interface BoardObservation<TState = unknown> {
  /** Opaque identity from a verified process session, replaced after every restart. */
  session: string;
  /** Opaque identity for a particular run, replaced on restart or level transition. */
  run: string;
  /** Monotonically increasing within the session. */
  revision: number;
  phase: 'board' | 'transition' | 'other';
  paused: boolean;
  /** Immutable, independently observed visible data; never a mutable live object. */
  state: TState;
}
export interface ActionTicket {
  readonly id: string;
  readonly session: string;
  readonly run: string;
  readonly submittedRevision: number;
  readonly expiresAtMs: number;
}
export type ActionBlock = 'cancelled' | 'invalid_ticket' | 'expired' | 'stale_context' | 'paused' | 'out_of_order';
export type DispatchDecision = { allowed: true } | { allowed: false; reason: ActionBlock };
export type NativeTerminal =
  | { id: string; outcome: 'executed'; revision: number }
  | { id: string; outcome: 'rejected'; revision: number }
  | { id: string; outcome: 'cancelled'; revision: number };
export type Verification =
  | { status: 'verified' }
  | { status: 'rejected' | 'cancelled' | 'unverified'; reason: string };

function validRevision(value: number): boolean { return Number.isSafeInteger(value) && value >= 0; }
function validIdentity(value: string): boolean { return typeof value === 'string' && value.length > 0 && value.length <= 128; }
function validTicket(value: ActionTicket): boolean {
  return value !== null && typeof value === 'object' && validIdentity(value.id) && validIdentity(value.session)
    && validIdentity(value.run) && validRevision(value.submittedRevision) && validRevision(value.expiresAtMs);
}
function validSnapshot(value: BoardObservation): boolean {
  return value !== null && typeof value === 'object' && validIdentity(value.session) && validIdentity(value.run)
    && validRevision(value.revision) && ['board', 'transition', 'other'].includes(value.phase)
    && typeof value.paused === 'boolean' && value.state !== null && value.state !== undefined;
}

/** Create a ticket only from a verified active-board snapshot; queueing is not success. */
export function createActionTicket(id: string, observation: BoardObservation, nowMs: number, ttlMs: number): ActionTicket {
  if (!validIdentity(id) || !validSnapshot(observation) || observation.phase !== 'board' || observation.paused ||
      !Number.isSafeInteger(nowMs) || nowMs < 0 || !Number.isSafeInteger(ttlMs) || ttlMs <= 0 ||
      !Number.isSafeInteger(nowMs + ttlMs)) throw new Error('invalid action ticket');
  return Object.freeze({ id, session: observation.session, run: observation.run,
    submittedRevision: observation.revision, expiresAtMs: nowMs + ttlMs });
}

/** Must run on a new observation immediately before any actual input. */
export function checkBeforeDispatch(ticket: ActionTicket, current: BoardObservation, nowMs: number, cancelled = false): DispatchDecision {
  if (cancelled) return { allowed: false, reason: 'cancelled' };
  if (!validTicket(ticket)) return { allowed: false, reason: 'invalid_ticket' };
  if (!Number.isSafeInteger(nowMs) || nowMs < 0 || nowMs >= ticket.expiresAtMs) return { allowed: false, reason: 'expired' };
  if (!validSnapshot(current) || current.session !== ticket.session || current.run !== ticket.run || current.phase !== 'board')
    return { allowed: false, reason: 'stale_context' };
  if (current.revision < ticket.submittedRevision) return { allowed: false, reason: 'out_of_order' };
  if (current.paused) return { allowed: false, reason: 'paused' };
  return { allowed: true };
}

/**
 * A terminal result is not verification: only a matching result and a later,
 * same-run observation with independently confirmed visible effect can verify.
 * Callers must supply separate immutable snapshots and a game-specific verifier.
 */
export function verifyAction<TState>(
  ticket: ActionTicket,
  before: BoardObservation<TState>,
  terminal: NativeTerminal | null,
  after: BoardObservation<TState> | null,
  visibleEffect: (before: TState, after: TState) => boolean,
): Verification {
  if (!validTicket(ticket)) return { status: 'unverified', reason: 'invalid action ticket' };
  if (!validSnapshot(before) || before.phase !== 'board' || before.paused || before.session !== ticket.session ||
      before.run !== ticket.run || before.revision < ticket.submittedRevision)
    return { status: 'unverified', reason: 'invalid pre-action observation' };
  if (!terminal || terminal.id !== ticket.id || !validRevision(terminal.revision) || terminal.revision < before.revision)
    return { status: 'unverified', reason: 'missing or invalid terminal result' };
  if (terminal.outcome === 'rejected') return { status: 'rejected', reason: 'native action rejected' };
  if (terminal.outcome === 'cancelled') return { status: 'cancelled', reason: 'native action cancelled' };
  if (terminal.outcome !== 'executed') return { status: 'unverified', reason: 'unknown native outcome' };
  if (!after || !validSnapshot(after) || after.session !== ticket.session || after.run !== ticket.run ||
      after.phase !== 'board' || after.revision <= terminal.revision)
    return { status: 'unverified', reason: 'missing or stale post-action observation' };
  try {
    return visibleEffect(before.state, after.state) === true
      ? { status: 'verified' }
      : { status: 'unverified', reason: 'visible effect not confirmed' };
  } catch {
    return { status: 'unverified', reason: 'effect verification failed' };
  }
}
