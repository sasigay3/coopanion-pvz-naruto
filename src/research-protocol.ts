/** Wire format for the research-only native reader. Not a gameplay snapshot. */
export type ResearchResult =
  | { ok: true; sceneCandidate: number; modeCandidate: number; sunCandidate?: number; cardCountCandidate?: number;
      plantArrayCandidate?: { used: number; capacity: number; count: number } }
  | { ok: false; reason: ResearchFailure };

const FAILURE_REASONS = [
  'invalid_arguments', 'file_rejected', 'open_failed', 'exited', 'architecture_mismatch',
  'path_unavailable', 'image_mismatch', 'module_unavailable', 'module_mismatch',
  'not_ready', 'memory_mismatch', 'invalid_base', 'read_failed', 'code_mismatch',
  'object_mismatch', 'invalid_value', 'changed', 'unknown',
] as const;
export type ResearchFailure = typeof FAILURE_REASONS[number];
const failureSet = new Set<string>(FAILURE_REASONS);
const successKeys = new Set(['status', 'research_only', 'scene_candidate', 'mode_candidate', 'sun_candidate', 'card_count_candidate', 'plant_array_candidate']);

function word(value: unknown): value is number {
  return typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
}

/**
 * Fail closed on malformed, unexpected or oversized data. This parser deliberately
 * returns candidate values only; it never names game modes, cards, plants or scenes.
 */
export function parseResearchOutput(text: string): ResearchResult {
  if (Buffer.byteLength(text, 'utf8') > 4096) throw new Error('research output exceeds limit');
  let value: unknown;
  try { value = JSON.parse(text); } catch { throw new Error('invalid research JSON'); }
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('invalid research response');
  const data = value as Record<string, unknown>;
  if (typeof data.status !== 'string') throw new Error('missing research status');
  if (data.status !== 'ok') {
    if (Object.keys(data).length !== 1 || !failureSet.has(data.status)) throw new Error('invalid research failure');
    return { ok: false, reason: data.status as ResearchFailure };
  }
  if (Object.keys(data).some(key => !successKeys.has(key)) || data.research_only !== true ||
      !word(data.scene_candidate) || !word(data.mode_candidate)) throw new Error('invalid research snapshot');
  const sun = data.sun_candidate;
  const cards = data.card_count_candidate;
  const array = data.plant_array_candidate;
  let plantArray: { used: number; capacity: number; count: number } | undefined;
  if (array !== undefined) {
    if (!array || typeof array !== 'object' || Array.isArray(array)) throw new Error('invalid array candidate');
    const candidate = array as Record<string, unknown>;
    if (Object.keys(candidate).length !== 3 || !word(candidate.used) || !word(candidate.capacity) ||
        !word(candidate.count) || candidate.capacity > 4096 || candidate.used > candidate.capacity || candidate.count > candidate.used)
      throw new Error('invalid array candidate');
    plantArray = { used: candidate.used, capacity: candidate.capacity, count: candidate.count };
  }
  if ((sun !== undefined && !word(sun)) || (cards !== undefined && (!word(cards) || cards > 10)) ||
      ((sun !== undefined || cards !== undefined || array !== undefined) && data.scene_candidate !== 3)) {
    throw new Error('invalid research candidate value');
  }
  return {
    ok: true,
    sceneCandidate: data.scene_candidate,
    modeCandidate: data.mode_candidate,
    ...(sun !== undefined ? { sunCandidate: sun as number } : {}),
    ...(cards !== undefined ? { cardCountCandidate: cards as number } : {}),
    ...(plantArray ? { plantArrayCandidate: plantArray } : {}),
  };
}
