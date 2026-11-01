import test from 'node:test';
import assert from 'node:assert/strict';
import { parseResearchOutput } from '../src/research-protocol.ts';

const valid = { status: 'ok', research_only: true, scene_candidate: 3, mode_candidate: 0 };
test('accepts zero and only explicitly supplied candidate values', () => {
  assert.deepEqual(parseResearchOutput(JSON.stringify({ ...valid, sun_candidate: 0, card_count_candidate: 5 })),
    { ok: true, sceneCandidate: 3, modeCandidate: 0, sunCandidate: 0, cardCountCandidate: 5 });
  assert.deepEqual(parseResearchOutput(JSON.stringify({ ...valid, scene_candidate: 1 })),
    { ok: true, sceneCandidate: 1, modeCandidate: 0 });
  assert.deepEqual(parseResearchOutput(JSON.stringify({ ...valid, plant_array_candidate: { used: 1, capacity: 2048, count: 1 } })),
    { ok: true, sceneCandidate: 3, modeCandidate: 0, plantArrayCandidate: { used: 1, capacity: 2048, count: 1 } });
  assert.deepEqual(parseResearchOutput('{"status":"image_mismatch"}'), { ok: false, reason: 'image_mismatch' });
});

test('rejects bad or ambiguous outputs rather than inventing a game state', () => {
  const invalid = [
    '', '{}', '[]', JSON.stringify({ ...valid, research_only: false }),
    JSON.stringify({ ...valid, scene_candidate: 1, sun_candidate: 75 }),
    JSON.stringify({ ...valid, card_count_candidate: 11 }),
    JSON.stringify({ ...valid, sun_candidate: -1 }),
    JSON.stringify({ ...valid, mode_candidate: 1.5 }),
    JSON.stringify({ ...valid, extra_pointer: 123 }),
    JSON.stringify({ ...valid, scene_candidate: 1, plant_array_candidate: { used: 1, capacity: 2, count: 1 } }),
    JSON.stringify({ ...valid, plant_array_candidate: { used: 2, capacity: 1, count: 1 } }),
    JSON.stringify({ ...valid, plant_array_candidate: { used: 1, capacity: 4097, count: 1 } }),
    JSON.stringify({ ...valid, plant_array_candidate: { used: 1, capacity: 2, count: 2 } }),
    JSON.stringify({ ...valid, plant_array_candidate: { used: 1, capacity: 2, count: 1, block: 42 } }),
    JSON.stringify({ ...valid, plant_array_candidate: null }),
    JSON.stringify({ status: 'unexpected' }),
    JSON.stringify({ status: 'not_ready', scene_candidate: 3 }),
    ' '.repeat(4097),
  ];
  for (const input of invalid) assert.throws(() => parseResearchOutput(input));
});
