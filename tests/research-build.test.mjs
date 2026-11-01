import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { buildResearchHelper } from '../src/research-build.ts';
import { runResearchProbe } from '../src/research-client.ts';

const windows = process.platform === 'win32';
test('refuses relative and shell-sensitive build paths', { skip: !windows }, async () => {
  await assert.rejects(buildResearchHelper('relative'), /unsafe native build path/);
  await assert.rejects(buildResearchHelper(resolve('bad&path')), /unsafe native build path/);
});

test('builds in an isolated namespace, reuses a valid cache, and refuses a modified manifest',
  { skip: !windows || process.env.NARUTO_TEST_NATIVE_BUILD !== '1' }, async () => {
    const scratch = resolve('scratch');
    await mkdir(scratch, { recursive: true });
    const root = await mkdtemp(join(scratch, 'build-fixture-'));
    const artifact = await buildResearchHelper(root);
    assert.ok(artifact.path.startsWith(join(root, 'pvz-naruto-research')));
    assert.equal(createHash('sha256').update(await readFile(artifact.path)).digest('hex'), artifact.sha256);
    assert.deepEqual(await buildResearchHelper(root), artifact);
    if (process.env.NARUTO_RESEARCH_TARGET) {
      const result = await runResearchProbe({ cliPath: artifact.path, cliSha256: artifact.sha256,
        executablePath: resolve(process.env.NARUTO_RESEARCH_TARGET), pid: process.pid, sun: true });
      assert.equal(result.ok, false);
      assert.ok(['architecture_mismatch', 'image_mismatch'].includes(result.reason));
    }
    const binary = await readFile(artifact.path);
    await writeFile(artifact.path, Buffer.concat([binary, Buffer.from([0])]));
    await assert.rejects(buildResearchHelper(root), /integrity mismatch/);
    await writeFile(artifact.path, binary); // Only this test's newly built fixture is restored.
    const manifest = join(dirname(artifact.path), 'artifact.json');
    await writeFile(manifest, JSON.stringify({ schema: 1, sourceHash: artifact.sourceHash, sha256: '0'.repeat(64), size: 1 }));
    await assert.rejects(buildResearchHelper(root), /integrity mismatch/);
  });
