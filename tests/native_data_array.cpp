#include "../src/native/data_array_reader.h"
#include <cassert>
#include <cstring>
#include <map>

using namespace naruto;
int main() {
    constexpr uint32_t board = 0x1000, block = 0x5000;
    ArrayHeader header{block, 1, 2048, 0, 1, 2, 0};
    uint32_t id = 0x10000, value = 42;
    std::map<uint32_t, std::vector<uint8_t>> memory;
    auto put = [&](uint32_t at, const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        memory[at] = std::vector<uint8_t>(p, p + n);
    };
    put(board, &header, sizeof(header));
    put(block + 0x148, &id, sizeof(id));
    put(block + 0x24, &value, sizeof(value));
    auto read = [&](uint32_t at, void* out, size_t n) {
        const auto it = memory.find(at);
        if (it == memory.end() || it->second.size() != n) return false;
        std::memcpy(out, it->second.data(), n);
        return true;
    };
    std::vector<Slot> slots;
    ArrayLayout plant{0x300, 0x148, 2048, 16, {{0x24, 4}}};
    assert(ReadArray(read, board, plant, slots) == ArrayStatus::ok);
    assert(slots.size() == 1 && slots[0].id == id && slots[0].fields[0][0] == 42);
    auto invalid = plant;
    invalid.id_offset = invalid.stride;
    assert(ReadArray(read, board, invalid, slots) == ArrayStatus::invalid_layout && slots.empty());
    invalid = plant;
    invalid.fields = {{0x2ff, 4}};
    assert(ReadArray(read, board, invalid, slots) == ArrayStatus::invalid_layout && slots.empty());
    auto hd_limit = plant;
    hd_limit.capacity_limit = 1024;
    assert(ReadArray(read, board, hd_limit, slots) == ArrayStatus::invalid_header && slots.empty());
    int reads = 0;
    auto race = [&](uint32_t at, void* out, size_t n) {
        if (at == block + 0x148 && ++reads == 2) {
            const uint32_t other = 0x20000;
            std::memcpy(out, &other, sizeof(other));
            return true;
        }
        return read(at, out, n);
    };
    assert(ReadArray(race, board, plant, slots) == ArrayStatus::changed && slots.empty());
    auto fail = [&](uint32_t at, void* out, size_t n) {
        return at != block + 0x24 && read(at, out, n);
    };
    assert(ReadArray(fail, board, plant, slots) == ArrayStatus::read_failed && slots.empty());
    auto change_header = [&](uint32_t at, void* out, size_t n) {
        static int header_reads = 0;
        if (at == board && ++header_reads == 2) {
            auto changed = header;
            changed.next_key++;
            std::memcpy(out, &changed, n);
            return true;
        }
        return read(at, out, n);
    };
    assert(ReadArray(change_header, board, plant, slots) == ArrayStatus::changed && slots.empty());
    auto overflow = header;
    overflow.block = 0xffffff00U;
    put(board, &overflow, sizeof(overflow));
    assert(ReadArray(read, board, plant, slots) == ArrayStatus::invalid_header && slots.empty());
    put(board, &header, sizeof(header));
    auto two = header;
    two.used = 2;
    two.count = 2;
    put(board, &two, sizeof(two));
    auto fail_second = [&](uint32_t at, void* out, size_t n) {
        return at != block + 0x300 + 0x148 && read(at, out, n);
    };
    assert(ReadArray(fail_second, board, plant, slots) == ArrayStatus::read_failed && slots.empty());
    put(board, &header, sizeof(header));
    // A 0x408 stride needs no oversized temporary buffer and remains bounded.
    ArrayLayout other{0x408, 0x404, 2048, 16, {{0x24, 4}}};
    put(block + 0x404, &id, sizeof(id));
    assert(ReadArray(read, board, other, slots) == ArrayStatus::ok && slots.size() == 1);
}
