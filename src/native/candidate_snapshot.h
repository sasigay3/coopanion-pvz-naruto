#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace naruto {

// Candidate checks from the single examined executable, not a complete support profile.
// The caller must independently authenticate the file, process image and loaded module.
struct CodePrefix { uint32_t rva; std::array<uint8_t, 8> bytes; };
inline constexpr std::array<CodePrefix, 10> kResearchCodePrefixes{{
    {0x4e8a0, {0xa1,0xc0,0x9e,0x6a,0x00,0x85,0xc0,0x75}},
    {0x4e8c0, {0xa1,0xc0,0x9e,0x6a,0x00,0x85,0xc0,0x74}},
    {0x4eacb, {0xc7,0x06,0xa0,0x7b,0x66,0x00,0xc7,0x46}},
    {0x4ee11, {0xc7,0x07,0xa0,0x7b,0x66,0x00,0xc7,0x47}},
    {0x0b069, {0xc7,0x85,0x60,0x55,0x00,0x00,0x96,0x00}},
    {0x08771, {0x8d,0xb5,0xac,0x00,0x00,0x00,0x74,0x23}},
    {0x1de9b, {0x69,0xc0,0x00,0x03,0x00,0x00,0x8b,0x94}},
    {0x5db78, {0x89,0x7b,0x28,0x89,0x73,0x1c,0x74,0x53}},
    {0x07e7c, {0x89,0x85,0x44,0x01,0x00,0x00,0xe8,0xa9}},
    {0x89000, {0x33,0xd2,0xd9,0xee,0x89,0x51,0x08,0x89}},
}};

struct CandidateArrayCounts { uint32_t used, capacity, count; };
struct CandidateSnapshot {
    uint32_t scene_candidate = 0;
    uint32_t mode_candidate = 0;
    std::optional<uint32_t> sun_candidate;
    std::optional<uint32_t> card_count_candidate;
    std::optional<CandidateArrayCounts> plant_array_candidate;
};
struct CandidateOptions { bool sun = false; bool card_count = false; bool plant_array = false; };
enum class CandidateStatus {
    ok, invalid_base, read_failed, code_mismatch, not_ready,
    object_mismatch, invalid_value, changed
};

// Pure research reader: no process opening, writes, input, injection or mode mapping.
// Even success does not authorize gameplay. A twice-checked snapshot can still
// change between reads, and candidate fields are not established across modes.
template<class Read>
CandidateStatus ReadCandidateSnapshot(Read read, uint32_t image_base,
                                      CandidateOptions options,
                                      CandidateSnapshot& output) {
    output = {};
    if (image_base != 0x400000) return CandidateStatus::invalid_base;
    for (const auto& sig : kResearchCodePrefixes) {
        std::array<uint8_t, 8> bytes{};
        if (!read(image_base + sig.rva, bytes.data(), bytes.size())) return CandidateStatus::read_failed;
        if (bytes != sig.bytes) return CandidateStatus::code_mismatch;
    }
    auto word = [&](uint64_t at, uint32_t& value) {
        return at + sizeof(value) <= uint64_t(UINT32_MAX) + 1 &&
               read(static_cast<uint32_t>(at), &value, sizeof(value));
    };
    uint32_t root = 0;
    if (!word(uint64_t(image_base) + 0x2a9ec0, root)) return CandidateStatus::read_failed;
    if (!root || uint64_t(root) + 0x830 > uint64_t(UINT32_MAX) + 1) return CandidateStatus::not_ready;
    uint32_t primary = 0, secondary = 0;
    if (!word(root, primary) || !word(uint64_t(root) + 4, secondary)) return CandidateStatus::read_failed;
    if (primary != image_base + 0x267ba0 || secondary != image_base + 0x267d50)
        return CandidateStatus::object_mismatch;
    uint32_t scene = 0, mode = 0, board = 0;
    if (!word(uint64_t(root) + 0x7fc, scene) || !word(uint64_t(root) + 0x7f8, mode) ||
        !word(uint64_t(root) + 0x768, board)) return CandidateStatus::read_failed;
    CandidateSnapshot candidate{};
    candidate.scene_candidate = scene;
    candidate.mode_candidate = mode;
    uint32_t bank = 0;
    std::array<uint32_t, 7> array_header{};
    if (options.sun || options.card_count || options.plant_array) {
        if (scene != 3 || !board || uint64_t(board) + 0x5564 > uint64_t(UINT32_MAX) + 1)
            return CandidateStatus::not_ready;
        if (options.sun) {
            uint32_t sun = 0;
            if (!word(uint64_t(board) + 0x5560, sun)) return CandidateStatus::read_failed;
            candidate.sun_candidate = sun;
        }
        if (options.card_count) {
            if (!word(uint64_t(board) + 0x144, bank)) return CandidateStatus::read_failed;
            if (!bank || uint64_t(bank) + 0x28 > uint64_t(UINT32_MAX) + 1) return CandidateStatus::not_ready;
            uint32_t count = 0;
            if (!word(uint64_t(bank) + 0x24, count)) return CandidateStatus::read_failed;
            if (count > 10) return CandidateStatus::invalid_value;
            candidate.card_count_candidate = count;
        }
        if (options.plant_array) {
            if (!read(board + 0xac, array_header.data(), sizeof(array_header))) return CandidateStatus::read_failed;
            const auto block = array_header[0], used = array_header[1], capacity = array_header[2], count = array_header[4];
            if (capacity > 4096 || used > capacity || count > used || (count && !block))
                return CandidateStatus::invalid_value;
            candidate.plant_array_candidate = CandidateArrayCounts{used, capacity, count};
        }
    }
    auto check_context = [&]() {
        uint32_t root_after = 0, primary_after = 0, secondary_after = 0;
        uint32_t scene_after = 0, mode_after = 0, board_after = 0;
        if (!word(uint64_t(image_base) + 0x2a9ec0, root_after)) return CandidateStatus::read_failed;
        if (root_after != root) return CandidateStatus::changed;
        if (!word(root, primary_after) || !word(uint64_t(root) + 4, secondary_after))
            return CandidateStatus::read_failed;
        if (primary_after != primary || secondary_after != secondary) return CandidateStatus::changed;
        if (!word(uint64_t(root) + 0x7fc, scene_after) || !word(uint64_t(root) + 0x7f8, mode_after) ||
            !word(uint64_t(root) + 0x768, board_after)) return CandidateStatus::read_failed;
        if (scene_after != scene || mode_after != mode || board_after != board) return CandidateStatus::changed;
        return CandidateStatus::ok;
    };
    const auto first_check = check_context();
    if (first_check != CandidateStatus::ok) return first_check;
    if (options.sun) {
        uint32_t sun_after = 0;
        if (!word(uint64_t(board) + 0x5560, sun_after)) return CandidateStatus::read_failed;
        if (sun_after != *candidate.sun_candidate) return CandidateStatus::changed;
    }
    if (options.card_count) {
        uint32_t bank_after = 0, count_after = 0;
        if (!word(uint64_t(board) + 0x144, bank_after)) return CandidateStatus::read_failed;
        if (bank_after != bank) return CandidateStatus::changed;
        if (!word(uint64_t(bank) + 0x24, count_after)) return CandidateStatus::read_failed;
        if (count_after != *candidate.card_count_candidate) return CandidateStatus::changed;
    }
    if (options.plant_array) {
        std::array<uint32_t, 7> current{};
        if (!read(board + 0xac, current.data(), sizeof(current))) return CandidateStatus::read_failed;
        if (current != array_header) return CandidateStatus::changed;
    }
    const auto final_check = check_context();
    if (final_check != CandidateStatus::ok) return final_check;
    output = candidate;
    return CandidateStatus::ok;
}

}  // namespace naruto
