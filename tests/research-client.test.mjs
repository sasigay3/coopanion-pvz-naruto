import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { runResearchProbe } from '../src/research-client.ts';

const windows = process.platform === 'win32';
test('rejects malformed requests before spawning a process', { skip: !windows }, async () => {
  const base = { cliPath: resolve('missing.exe'), cliSha256: 'a'.repeat(64), executablePath: resolve('game.exe'), pid: process.pid };
  for (const request of [
    { ...base, pid: 0 }, { ...base, pid: -1 }, { ...base, pid: 0x100000000 },
    { ...base, cliSha256: 'bad' }, { ...base, cliPath: 'relative.exe' },
    { ...base, sun: 'true' }, { ...base, executablePath: null },
  ]) await assert.rejects(runResearchProbe(request), /invalid research probe request/);
});

test('rejects a mismatched helper digest without executing it', { skip: !windows }, async () => {
  await assert.rejects(runResearchProbe({
    cliPath: resolve('missing.exe'), cliSha256: '0'.repeat(64), executablePath: process.execPath, pid: process.pid,
  }), /integrity check failed/);
  await assert.rejects(runResearchProbe({
    cliPath: process.execPath, cliSha256: '0'.repeat(64), executablePath: process.execPath, pid: process.pid,
  }), /integrity check failed/);
});

test('native helper rejects an unrelated process', { skip: !windows || !process.env.NARUTO_RESEARCH_TARGET || !process.env.NARUTO_RESEARCH_CLI }, async () => {
  const cliPath = resolve(process.env.NARUTO_RESEARCH_CLI);
  const cliSha256 = createHash('sha256').update(await readFile(cliPath)).digest('hex');
  const result = await runResearchProbe({
    cliPath, cliSha256, executablePath: resolve(process.env.NARUTO_RESEARCH_TARGET), pid: process.pid, sun: true, cards: true,
  });
  assert.equal(result.ok, false);
  assert.ok(['architecture_mismatch', 'image_mismatch'].includes(result.reason));
});
