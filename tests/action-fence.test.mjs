import test from 'node:test';
import assert from 'node:assert/strict';
import { createActionTicket, checkBeforeDispatch, verifyAction } from '../src/action-fence.ts';

const board = { session: 'process-a', run: 'level-1-run-a', revision: 10, phase: 'board', paused: false, state: { sun: 100 } };
const ticket = createActionTicket('task-1', board, 1000, 5000);

test('prevents input on a stale session, new run, pause, timeout or cancellation', () => {
  assert.deepEqual(checkBeforeDispatch(ticket, { ...board, revision: 11 }, 1500), { allowed: true });
  for (const [current, now, cancelled, reason] of [
    [{ ...board, session: 'process-b' }, 1500, false, 'stale_context'],
    [{ ...board, run: 'level-2' }, 1500, false, 'stale_context'],
    [{ ...board, phase: 'transition' }, 1500, false, 'stale_context'],
    [{ ...board, paused: true }, 1500, false, 'paused'],
    [{ ...board, revision: 9 }, 1500, false, 'out_of_order'],
    [board, 6000, false, 'expired'],
    [board, 1500, true, 'cancelled'],
  ]) assert.deepEqual(checkBeforeDispatch(ticket, current, now, cancelled), { allowed: false, reason });
});

test('requires correlated terminal result, later same-run state, and independent visible evidence', () => {
  const terminal = { id: 'task-1', outcome: 'executed', revision: 11 };
  const after = { ...board, revision: 12, state: { sun: 75 } };
  assert.deepEqual(verifyAction(ticket, board, terminal, after, (before, next) => next.sun === before.sun - 25), { status: 'verified' });
  for (const [before, result, snapshot, effect] of [
    [board, null, after, () => true],
    [board, { ...terminal, id: 'other' }, after, () => true],
    [board, { ...terminal, revision: 9 }, after, () => true],
    [board, terminal, { ...after, revision: 11 }, () => true],
    [board, terminal, { ...after, run: 'new-run' }, () => true],
    [{ ...board, paused: true }, terminal, after, () => true],
    [board, terminal, after, () => false],
    [board, terminal, after, () => { throw new Error('unavailable'); }],
  ]) assert.equal(verifyAction(ticket, before, result, snapshot, effect).status, 'unverified');
  assert.equal(verifyAction(ticket, board, { ...terminal, outcome: 'rejected' }, after, () => true).status, 'rejected');
  assert.equal(verifyAction(ticket, board, { ...terminal, outcome: 'cancelled' }, after, () => true).status, 'cancelled');
});

test('refuses to create a ticket from invalid, paused or non-board observations', () => {
  for (const state of [{ ...board, session: '' }, { ...board, phase: 'other' }, { ...board, paused: true }, { ...board, revision: -1 }, { ...board, state: undefined }])
    assert.throws(() => createActionTicket('task-1', state, 1000, 5000));
  assert.throws(() => createActionTicket('task-1', board, 1000, 0));
});
