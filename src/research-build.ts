import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { lstat, mkdir, mkdtemp, readFile, rename, writeFile } from 'node:fs/promises';
import { isAbsolute, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const PROJECT_ROOT = fileURLToPath(new URL('../', import.meta.url));
const BUILD_SCRIPT = join(PROJECT_ROOT, 'scripts', 'build-research.cmd');
const SOURCE_FILES = [
  'scripts/build-research.cmd', 'src/research-build.ts', 'src/native/candidate_snapshot.h', 'src/native/data_array_reader.h',
  'src/native/plant_candidate.h', 'src/native/read_only_process.h',
  'src/native/research_cli.cpp', 'src/native/research_cli_args.h', 'src/native/target_file.h',
] as const;
const BINARY = 'pvz-naruto-research.exe';
const MANIFEST = 'artifact.json';

export interface ResearchArtifact { path: string; sha256: string; sourceHash: string; }
function safeCmdPath(path: string): void {
  if (!isAbsolute(path) || /[&|<>^()%!"\x00\r\n]/.test(path)) throw new Error('unsafe native build path');
}

async function fileDigest(path: string): Promise<{ sha256: string; size: number }> {
  const info = await lstat(path);
  if (!info.isFile() || info.isSymbolicLink() || info.size <= 0 || info.size > 32 * 1024 * 1024)
    throw new Error('invalid research artifact');
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(path)) hash.update(chunk as Buffer);
  return { sha256: hash.digest('hex'), size: info.size };
}

async function sourceIdentity(): Promise<string> {
  const hash = createHash('sha256');
  for (const name of SOURCE_FILES) {
    hash.update(name); hash.update('\0'); hash.update(await readFile(join(PROJECT_ROOT, name))); hash.update('\0');
  }
  return hash.digest('hex');
}

async function loadArtifact(dir: string, sourceHash: string): Promise<ResearchArtifact> {
  const directory = await lstat(dir);
  if (!directory.isDirectory() || directory.isSymbolicLink()) throw new Error('invalid research cache directory');
  const manifestPath = join(dir, MANIFEST);
  const info = await lstat(manifestPath);
  if (!info.isFile() || info.isSymbolicLink() || info.size > 4096) throw new Error('invalid research manifest');
  const manifest: unknown = JSON.parse(await readFile(manifestPath, 'utf8'));
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) throw new Error('invalid research manifest');
  const entry = manifest as Record<string, unknown>;
  if (entry.schema !== 1 || entry.sourceHash !== sourceHash || typeof entry.sha256 !== 'string' ||
      !/^[0-9a-f]{64}$/.test(entry.sha256) || !Number.isSafeInteger(entry.size) || Number(entry.size) <= 0)
    throw new Error('invalid research manifest');
  const path = join(dir, BINARY);
  const actual = await fileDigest(path);
  if (actual.sha256 !== entry.sha256 || actual.size !== entry.size) throw new Error('research artifact integrity mismatch');
  return { path, sha256: actual.sha256, sourceHash };
}

function runBuild(script: string, staging: string): Promise<void> {
  safeCmdPath(script); safeCmdPath(staging);
  return new Promise((resolveBuild, reject) => {
    const command = `""${script}" "${staging}""`;
    const child = spawn('cmd.exe', ['/d', '/s', '/c', command], {
      cwd: PROJECT_ROOT, windowsHide: true, windowsVerbatimArguments: true, stdio: 'ignore', shell: false,
    });
    const timer = setTimeout(() => { child.kill(); }, 120_000);
    child.once('error', () => { clearTimeout(timer); reject(new Error('could not start native research build')); });
    child.once('exit', code => {
      clearTimeout(timer);
      if (code === 0) resolveBuild();
      else reject(new Error('native research build failed or timed out'));
    });
  });
}

/**
 * Isolated content-addressed research helper build. Existing invalid caches are
 * refused, never overwritten. A source hash and local manifest are integrity
 * checks, not proof that the compiler or local files are trustworthy.
 */
export async function buildResearchHelper(buildRoot: string): Promise<ResearchArtifact> {
  if (process.platform !== 'win32') throw new Error('native research build requires Windows');
  if (typeof buildRoot !== 'string') throw new Error('unsafe native build path');
  safeCmdPath(buildRoot); safeCmdPath(BUILD_SCRIPT);
  const root = resolve(buildRoot, 'pvz-naruto-research');
  safeCmdPath(root);
  await mkdir(root, { recursive: true });
  const rootInfo = await lstat(root);
  if (!rootInfo.isDirectory() || rootInfo.isSymbolicLink()) throw new Error('invalid research cache root');
  const sourceHash = await sourceIdentity();
  const final = join(root, sourceHash);
  try {
    const cached = await loadArtifact(final, sourceHash);
    if (await sourceIdentity() !== sourceHash) throw new Error('research source changed during cache verification');
    return cached;
  } catch (error) {
    // Only a missing cache may be built. A corrupted cache is never overwritten.
    if (!(error && typeof error === 'object' && 'code' in error && error.code === 'ENOENT')) throw error;
    try { await lstat(final); throw new Error('incomplete research artifact cache'); }
    catch (check) {
      if (!(check && typeof check === 'object' && 'code' in check && check.code === 'ENOENT')) throw check;
    }
  }
  const staging = await mkdtemp(join(root, '.tmp-'));
  await runBuild(BUILD_SCRIPT, staging);
  if (await sourceIdentity() !== sourceHash) throw new Error('research source changed during build');
  const artifact = await fileDigest(join(staging, BINARY));
  await writeFile(join(staging, MANIFEST), JSON.stringify({ schema: 1, sourceHash, ...artifact }) + '\n', { flag: 'wx' });
  try { await rename(staging, final); }
  catch (error) {
    // Another build may have finished first; only reuse a fully verified cache.
    try {
      const cached = await loadArtifact(final, sourceHash);
      if (await sourceIdentity() !== sourceHash) throw new Error('research source changed during cache verification');
      return cached;
    } catch { throw error; }
  }
  const built = await loadArtifact(final, sourceHash);
  if (await sourceIdentity() !== sourceHash) throw new Error('research source changed during build verification');
  return built;
}
