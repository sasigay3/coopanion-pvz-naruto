import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';

/** Identity of the file examined locally. Matching this is not evidence of compatibility. */
export const EXAMINED_EXECUTABLE = {
  bytes: 7_340_032,
  sha256: 'c536aaff6679fd391ee2ddb4711b4f983f4ce0ea9b32ecc66456cead5fdc59a2',
} as const;

export async function checkExecutableIdentity(path: string): Promise<boolean> {
  const info = await stat(path);
  if (!info.isFile() || info.size !== EXAMINED_EXECUTABLE.bytes) return false;
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(path)) hash.update(chunk as Buffer);
  return hash.digest('hex') === EXAMINED_EXECUTABLE.sha256;
}
