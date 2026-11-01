#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace naruto {

// The target is x86 regardless of the architecture of the test runner.
struct ArrayHeader {
    uint32_t block, used, capacity, free_head, count, next_key, name;
};
static_assert(sizeof(ArrayHeader) == 28);

struct FieldRange { uint32_t offset, size; };
struct ArrayLayout {
    uint32_t stride, id_offset, capacity_limit, used_limit;
    std::vector<FieldRange> fields;
};
struct Slot {
    uint32_t id;
    std::vector<std::vector<uint8_t>> fields;
};
enum class ArrayStatus { ok, invalid_layout, invalid_header, read_failed, changed };

// A reader copies only requested field ranges; it never copies an entire stride.
// Results are published only after ID and header checks pass. This does not make
// independently read fields atomic, establish their meaning, or filter hidden data.
template<class Read>
ArrayStatus ReadArray(Read read, uint32_t address, const ArrayLayout& layout,
                      std::vector<Slot>& output) {
    output.clear();
    if (layout.stride < 4 || layout.stride > 4096 || layout.capacity_limit > 4096 ||
        layout.used_limit > 1024 || layout.used_limit > layout.capacity_limit ||
        layout.fields.size() > 16 || uint64_t(layout.id_offset) + 4 > layout.stride ||
        uint64_t(address) + sizeof(ArrayHeader) > uint64_t(UINT32_MAX) + 1) {
        return ArrayStatus::invalid_layout;
    }
    uint32_t total = 0;
    for (const auto& field : layout.fields) {
        if (!field.size || field.size > 64 || uint64_t(field.offset) + field.size > layout.stride)
            return ArrayStatus::invalid_layout;
        total += field.size;
    }
    if (total > 256) return ArrayStatus::invalid_layout;

    ArrayHeader header{};
    if (!read(address, &header, sizeof(header))) return ArrayStatus::read_failed;
    if (header.capacity > layout.capacity_limit || header.used > header.capacity ||
        header.used > layout.used_limit || header.count > header.used ||
        (header.count && !header.block)) return ArrayStatus::invalid_header;
    if (header.used && (!header.block || uint64_t(header.block) + uint64_t(header.used) * layout.stride > uint64_t(UINT32_MAX) + 1))
        return ArrayStatus::invalid_header;

    std::vector<Slot> pending;
    std::vector<uint32_t> observed_ids;
    observed_ids.reserve(header.used);
    for (uint32_t i = 0; i < header.used; ++i) {
        const uint32_t slot = header.block + i * layout.stride;
        uint32_t id = 0;
        if (!read(slot + layout.id_offset, &id, sizeof(id))) return ArrayStatus::read_failed;
        observed_ids.push_back(id);
        if (!(id >> 16) || (id & 0xffffU) != i) continue;
        Slot item{id, {}};
        for (const auto& field : layout.fields) {
            std::vector<uint8_t> data(field.size);
            if (!read(slot + field.offset, data.data(), data.size())) return ArrayStatus::read_failed;
            item.fields.push_back(std::move(data));
        }
        uint32_t current_id = 0;
        if (!read(slot + layout.id_offset, &current_id, sizeof(current_id))) return ArrayStatus::read_failed;
        if (current_id != id) return ArrayStatus::changed;
        pending.push_back(std::move(item));
    }
    // Earlier slots (including free slots) may change while later ones are read.
    for (uint32_t i = 0; i < header.used; ++i) {
        uint32_t current_id = 0;
        const uint32_t slot = header.block + i * layout.stride;
        if (!read(slot + layout.id_offset, &current_id, sizeof(current_id))) return ArrayStatus::read_failed;
        if (current_id != observed_ids[i]) return ArrayStatus::changed;
    }
    ArrayHeader current{};
    if (!read(address, &current, sizeof(current))) return ArrayStatus::read_failed;
    if (std::memcmp(&header, &current, sizeof(header)) != 0) return ArrayStatus::changed;
    if (pending.size() != header.count) return ArrayStatus::changed;
    output = std::move(pending);
    return ArrayStatus::ok;
}

}  // namespace naruto
