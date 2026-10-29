import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import world from '../src/index.ts';
import { checkExecutableIdentity } from '../src/identity.ts';

test('world uses isolated identity and refuses to activate or construct', () => {
  assert.equal(world.id, 'pvz-naruto');
  assert.deepEqual(world.defaults(), { enabled: false, executable: '' });
  assert.notEqual(world.defaults(), world.defaults());
  assert.throws(() => world.preflight({}), /尚未完成/);
  assert.throws(() => world.create({}), /尚未完成/);
  assert.deepEqual(world.describe, { tools: [], events: [] });
});

test('identity check rejects unrelated files and directories', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'pvz-naruto-test-'));
  try {
    const file = join(dir, 'other.exe');
    await writeFile(file, 'not the examined executable');
    assert.equal(await checkExecutableIdentity(file), false);
    await writeFile(file, Buffer.alloc(7_340_032));
    assert.equal(await checkExecutableIdentity(file), false);
    assert.equal(await checkExecutableIdentity(dir), false);
    await assert.rejects(checkExecutableIdentity(join(dir, 'missing.exe')));
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
