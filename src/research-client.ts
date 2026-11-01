import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { lstat } from 'node:fs/promises';
import { isAbsolute } from 'node:path';
import { parseResearchOutput, type ResearchResult } from './research-protocol.ts';

export interface ResearchProbeRequest {
  /** Must be built locally from reviewed source; the digest is an integrity check, not a signature. */
  cliPath: string;
  cliSha256: string;
  executablePath: string;
  pid: number;
  sun?: boolean;
  cards?: boolean;
}

async function matchesArtifact(path: string, expected: string): Promise<boolean> {
  const info = await lstat(path);
  if (!info.isFile() || info.isSymbolicLink() || info.size <= 0 || info.size > 32 * 1024 * 1024) return false;
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(path)) hash.update(chunk as Buffer);
  return hash.digest('hex') === expected;
}

/**
 * Internal, research-only IPC. It never starts the game or uses a shell.
 * The World is disabled; the caller must build the helper from trusted source and
 * supply its reviewed digest. A digest is not a signature or a complete defense
 * against a hostile local process replacing the file between verification and launch.
 */
export async function runResearchProbe(request: ResearchProbeRequest): Promise<ResearchResult> {
  if (process.platform !== 'win32') throw new Error('research probe requires Windows');
  if (typeof request.cliPath !== 'string' || typeof request.executablePath !== 'string' ||
      typeof request.cliSha256 !== 'string' || !isAbsolute(request.cliPath) || !isAbsolute(request.executablePath) ||
      !/^[0-9a-f]{64}$/.test(request.cliSha256) ||
      (request.sun !== undefined && typeof request.sun !== 'boolean') ||
      (request.cards !== undefined && typeof request.cards !== 'boolean') ||
      !Number.isSafeInteger(request.pid) || request.pid < 1 || request.pid > 0xffffffff) {
    throw new Error('invalid research probe request');
  }
  const artifactMatches = await matchesArtifact(request.cliPath, request.cliSha256).catch(() => false);
  if (!artifactMatches) throw new Error('research helper integrity check failed');
  const args = ['--pid', String(request.pid), '--executable', request.executablePath];
  if (request.sun) args.push('--sun');
  if (request.cards) args.push('--cards');
  return new Promise((resolve, reject) => {
    execFile(request.cliPath, args, { encoding: 'utf8', windowsHide: true, timeout: 15_000, maxBuffer: 4096, shell: false },
      (error, stdout, stderr) => {
        if (error && (typeof error.code !== 'number' || error.killed || error.signal)) {
          reject(new Error('research helper failed to run or timed out'));
          return;
        }
        if (stderr.trim()) { reject(new Error('research helper reported an unexpected error')); return; }
        try {
          const parsed = parseResearchOutput(stdout);
          if ((error && parsed.ok) || (!error && !parsed.ok)) throw new Error('research helper exit status mismatch');
          resolve(parsed);
        } catch {
          reject(new Error('research helper returned an invalid response'));
        }
      });
  });
}
