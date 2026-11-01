import test from 'node:test';
import assert from 'node:assert/strict';
import { InputArbiter } from '../src/input-arbiter.ts';
import { createActionTicket } from '../src/action-fence.ts';
import { runGuardedAction } from '../src/guarded-action.ts';

const board = { session: 's1', run: 'r1', revision: 1, phase: 'board', paused: false, state: { sun: 100 } };
const ticket = createActionTicket('a1', board, 10, 100);
const effect = (before, after) => before.sun - after.sun === 25;
const args = (arbiter, driver) => ({ arbiter, driver, ticket, command: 'plant', now: () => 20, effect });

test('rechecks the board and verifies only after terminal result and fresh visible effect', async () => {
  const arbiter = new InputArbiter();
  let reads = 0, actions = 0;
  const driver = {
    observe: async () => ++reads === 1 ? board : { ...board, revision: 3, state: { sun: 75 } },
    dispatch: async () => { actions++; return { releaseConfirmed: true, releaseFor: 'a1', terminal: { id: 'a1', outcome: 'executed', revision: 2 } }; },
  };
  assert.equal((await runGuardedAction(args(arbiter, driver))).status, 'verified');
  assert.equal(actions, 1);
  assert.equal(reads, 2);
});

test('does not dispatch when the run changed while waiting', async () => {
  const arbiter = new InputArbiter();
  let actions = 0;
  const driver = { observe: async () => ({ ...board, run: 'r2' }), dispatch: async () => { actions++; } };
  assert.deepEqual(await runGuardedAction(args(arbiter, driver)), { status: 'blocked', reason: 'stale_context' });
  assert.equal(actions, 0);
});

test('an unknown native release closes the input arbiter before another action', async () => {
  const arbiter = new InputArbiter();
  const driver = { observe: async () => board, dispatch: async () => ({ releaseConfirmed: false, releaseFor: 'a1', terminal: null }) };
  assert.deepEqual(await runGuardedAction(args(arbiter, driver)), { status: 'unverified', reason: 'native release not confirmed' });
  await assert.rejects(arbiter.acquire(), /closed/);
});

test('a missing result or stale post-action observation cannot be marked successful', async () => {
  for (const driver of [
    { observe: async () => board, dispatch: async () => ({ releaseConfirmed: true, releaseFor: 'a1', terminal: null }) },
    { observe: async () => board, dispatch: async () => ({ releaseConfirmed: true, releaseFor: 'a1', terminal: { id: 'a1', outcome: 'executed', revision: 2 } }) },
  ]) assert.equal((await runGuardedAction(args(new InputArbiter(), driver))).status, 'unverified');
});

test('a release acknowledgement for a different task is not accepted', async () => {
  const arbiter = new InputArbiter();
  const driver = { observe: async () => board, dispatch: async () => ({ releaseConfirmed: true, releaseFor: 'other', terminal: null }) };
  assert.equal((await runGuardedAction(args(arbiter, driver))).status, 'unverified');
  await assert.rejects(arbiter.acquire(), /closed/);
});

test('a dispatch exception also closes the arbiter', async () => {
  const arbiter = new InputArbiter();
  const driver = { observe: async () => board, dispatch: async () => { throw new Error('transport lost'); } };
  assert.equal((await runGuardedAction(args(arbiter, driver))).status, 'unverified');
  await assert.rejects(arbiter.acquire(), /closed/);
});

test('cancellation holds the input lease until native release, then allows the next action', async () => {
  const arbiter = new InputArbiter();
  const controller = new AbortController();
  const secondTicket = createActionTicket('a2', board, 10, 100);
  let resolveFirst, signalStarted;
  const started = new Promise(resolve => { signalStarted = resolve; });
  const release = new Promise(resolve => { resolveFirst = resolve; });
  const calls = [];
  const driver = {
    observe: async () => board,
    dispatch: async (command) => {
      calls.push(command);
      if (command === 'first') { signalStarted(); return release; }
      return { releaseConfirmed: true, releaseFor: 'a2', terminal: { id: 'a2', outcome: 'rejected', revision: 1 } };
    },
  };
  const first = runGuardedAction({ ...args(arbiter, driver), command: 'first', signal: controller.signal });
  await started;
  const second = runGuardedAction({ ...args(arbiter, driver), ticket: secondTicket, command: 'second' });
  controller.abort();
  await Promise.resolve();
  assert.deepEqual(calls, ['first']);
  resolveFirst({ releaseConfirmed: true, releaseFor: 'a1', terminal: { id: 'a1', outcome: 'cancelled', revision: 1 } });
  assert.equal((await first).status, 'cancelled');
  assert.equal((await second).status, 'rejected');
  assert.deepEqual(calls, ['first', 'second']);
});

test('an uncertain release blocks a task already waiting for input', async () => {
  const arbiter = new InputArbiter();
  let finish, signalStarted;
  const started = new Promise(resolve => { signalStarted = resolve; });
  const release = new Promise(resolve => { finish = resolve; });
  let actions = 0;
  const driver = { observe: async () => board, dispatch: async () => { actions++; signalStarted(); return release; } };
  const first = runGuardedAction(args(arbiter, driver));
  await started;
  const second = runGuardedAction(args(arbiter, driver));
  finish({ releaseConfirmed: false, releaseFor: 'a1', terminal: null });
  assert.equal((await first).status, 'unverified');
  assert.deepEqual(await second, { status: 'blocked', reason: 'input unavailable' });
  assert.equal(actions, 1);
});
