#pragma once

#include "candidate_snapshot.h"
#include "data_array_reader.h"
#include <cstring>

namespace naruto {

struct PlantCandidateWords { uint32_t index, word_1c, word_24, word_28; };
enum class PlantCandidateStatus { ok, confirmation_required, not_ready, invalid, read_failed, changed };

// Research only. The caller must authenticate the process and independently
// confirm a visible, ordinary daytime board. Candidate values cannot prove it.
template<class Read>
PlantCandidateStatus ReadVisiblePlantCandidates(Read read, uint32_t image_base,
                                              bool visible_daytime_confirmed,
                                              std::vector<PlantCandidateWords>& output) {
    output.clear();
    if (!visible_daytime_confirmed) return PlantCandidateStatus::confirmation_required;
    CandidateSnapshot before{};
    const auto first = ReadCandidateSnapshot(read, image_base, {false, false, true}, before);
    if (first != CandidateStatus::ok)
        return first == CandidateStatus::changed ? PlantCandidateStatus::changed : PlantCandidateStatus::not_ready;
    if (before.scene_candidate != 3 || before.mode_candidate != 0 || !before.plant_array_candidate ||
        before.plant_array_candidate->used > 16) return PlantCandidateStatus::not_ready;
    auto word = [&](uint64_t address, uint32_t& out) {
        return address + 4 <= uint64_t(UINT32_MAX) + 1 && read(static_cast<uint32_t>(address), &out, 4);
    };
    uint32_t root = 0, board = 0, background = 0;
    if (!word(uint64_t(image_base) + 0x2a9ec0, root) || !root ||
        !word(uint64_t(root) + 0x768, board) || !board ||
        !word(uint64_t(board) + 0x554c, background)) return PlantCandidateStatus::read_failed;
    if (background != 0) return PlantCandidateStatus::not_ready;
    if (uint64_t(board) + 0xac + sizeof(ArrayHeader) > uint64_t(UINT32_MAX) + 1)
        return PlantCandidateStatus::invalid;
    const ArrayLayout layout{0x300, 0x148, 4096, 16, {{0x1c, 4}, {0x24, 4}, {0x28, 4}}};
    std::vector<Slot> slots;
    const auto status = ReadArray(read, board + 0xac, layout, slots);
    if (status == ArrayStatus::changed) return PlantCandidateStatus::changed;
    if (status == ArrayStatus::read_failed) return PlantCandidateStatus::read_failed;
    if (status != ArrayStatus::ok) return PlantCandidateStatus::invalid;
    uint32_t root_after = 0, board_after = 0, background_after = 0;
    if (!word(uint64_t(image_base) + 0x2a9ec0, root_after)) return PlantCandidateStatus::read_failed;
    if (root_after != root) return PlantCandidateStatus::changed;
    if (!word(uint64_t(root) + 0x768, board_after)) return PlantCandidateStatus::read_failed;
    if (board_after != board) return PlantCandidateStatus::changed;
    if (!word(uint64_t(board) + 0x554c, background_after)) return PlantCandidateStatus::read_failed;
    if (background_after != background) return PlantCandidateStatus::changed;
    CandidateSnapshot after{};
    const auto last = ReadCandidateSnapshot(read, image_base, {false, false, true}, after);
    if (last != CandidateStatus::ok) return PlantCandidateStatus::changed;
    if (after.scene_candidate != before.scene_candidate || after.mode_candidate != before.mode_candidate ||
        !after.plant_array_candidate ||
        after.plant_array_candidate->used != before.plant_array_candidate->used ||
        after.plant_array_candidate->capacity != before.plant_array_candidate->capacity ||
        after.plant_array_candidate->count != before.plant_array_candidate->count)
        return PlantCandidateStatus::changed;
    uint32_t final_root = 0, final_board = 0, final_background = 0;
    if (!word(uint64_t(image_base) + 0x2a9ec0, final_root)) return PlantCandidateStatus::read_failed;
    if (final_root != root) return PlantCandidateStatus::changed;
    if (!word(uint64_t(root) + 0x768, final_board)) return PlantCandidateStatus::read_failed;
    if (final_board != board) return PlantCandidateStatus::changed;
    if (!word(uint64_t(board) + 0x554c, final_background)) return PlantCandidateStatus::read_failed;
    if (final_background != background) return PlantCandidateStatus::changed;
    std::vector<PlantCandidateWords> pending;
    for (const auto& slot : slots) {
        PlantCandidateWords item{slot.id & 0xffffU, 0, 0, 0};
        std::memcpy(&item.word_1c, slot.fields[0].data(), 4);
        std::memcpy(&item.word_24, slot.fields[1].data(), 4);
        std::memcpy(&item.word_28, slot.fields[2].data(), 4);
        pending.push_back(item);
    }
    output = std::move(pending);
    return PlantCandidateStatus::ok;
}
}  // namespace naruto
