import test from 'node:test';
import assert from 'node:assert/strict';
import { InputArbiter } from '../src/input-arbiter.ts';

test('serializes model and automatic input in FIFO order and removes cancelled waiters', async () => {
  const arbiter = new InputArbiter();
  const first = await arbiter.acquire();
  const cancel = new AbortController();
  const removed = arbiter.acquire(cancel.signal);
  const removedCheck = assert.rejects(removed, /cancelled/);
  let granted = false;
  const next = arbiter.acquire().then(lease => { granted = true; return lease; });
  cancel.abort();
  await removedCheck;
  await Promise.resolve();
  assert.equal(granted, false);
  first.release();
  const second = await next;
  assert.equal(granted, true);
  let thirdGranted = false;
  const thirdPending = arbiter.acquire().then(lease => { thirdGranted = true; return lease; });
  first.release(); // an old lease cannot release a later one
  await Promise.resolve();
  assert.equal(thirdGranted, false);
  second.release();
  const third = await thirdPending;
  assert.equal(thirdGranted, true);
  third.release();
});

test('aborting a granted lease does not let later input overlap', async () => {
  const arbiter = new InputArbiter();
  const controller = new AbortController();
  const active = await arbiter.acquire(controller.signal);
  let granted = false;
  const next = arbiter.acquire().then(lease => { granted = true; return lease; });
  controller.abort();
  await Promise.resolve();
  assert.equal(granted, false);
  active.release(); // caller does this only after native cancellation is confirmed
  (await next).release();
});

test('closing rejects waiting and future requests while preserving the active lease', async () => {
  const arbiter = new InputArbiter();
  const active = await arbiter.acquire();
  const waiting = arbiter.acquire();
  const check = assert.rejects(waiting, /closed/);
  arbiter.close();
  await check;
  await assert.rejects(arbiter.acquire(), /closed/);
  active.release();
  arbiter.close();
});
