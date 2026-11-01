#include "../src/native/plant_candidate.h"
#include <cassert>
#include <map>

using namespace naruto;
int main() {
    constexpr uint32_t image = 0x400000, root = 0x10000000, board = 0x20000000, block = 0x30000000;
    std::map<uint32_t, std::vector<uint8_t>> memory;
    auto put = [&](uint32_t at, const void* data, size_t n) {
        const auto* begin = static_cast<const uint8_t*>(data);
        memory[at] = std::vector<uint8_t>(begin, begin + n);
    };
    auto word = [&](uint32_t at, uint32_t value) { put(at, &value, 4); };
    for (const auto& sig : kResearchCodePrefixes) put(image + sig.rva, sig.bytes.data(), sig.bytes.size());
    word(image + 0x2a9ec0, root); word(root, image + 0x267ba0); word(root + 4, image + 0x267d50);
    word(root + 0x7fc, 3); word(root + 0x7f8, 0); word(root + 0x768, board);
    word(board + 0x554c, 0);
    ArrayHeader header{block, 2, 2048, 0, 2, 3, 0};
    put(board + 0xac, &header, sizeof(header));
    word(block + 0x148, 0x10000); word(block + 0x1c, 2); word(block + 0x24, 1); word(block + 0x28, 0);
    word(block + 0x300 + 0x148, 0x10001); word(block + 0x300 + 0x1c, 2);
    word(block + 0x300 + 0x24, 0); word(block + 0x300 + 0x28, 3);
    auto read = [&](uint32_t at, void* dst, size_t n) {
        auto it = memory.find(at);
        if (it == memory.end() || it->second.size() != n) return false;
        std::memcpy(dst, it->second.data(), n);
        return true;
    };
    std::vector<PlantCandidateWords> result{{}};
    assert(ReadVisiblePlantCandidates(read, image, false, result) == PlantCandidateStatus::confirmation_required && result.empty());
    assert(ReadVisiblePlantCandidates(read, image, true, result) == PlantCandidateStatus::ok);
    assert(result.size() == 2 && result[0].index == 0 && result[0].word_24 == 1);
    assert(result[1].index == 1 && result[1].word_1c == 2 && result[1].word_28 == 3);
    word(board + 0x554c, 1);
    assert(ReadVisiblePlantCandidates(read, image, true, result) == PlantCandidateStatus::not_ready && result.empty());
    word(board + 0x554c, 0);
    word(root + 0x7f8, 1);
    assert(ReadVisiblePlantCandidates(read, image, true, result) == PlantCandidateStatus::not_ready && result.empty());
    word(root + 0x7f8, 0);
    int background_reads = 0;
    auto change_background = [&](uint32_t at, void* dst, size_t n) {
        if (at == board + 0x554c && ++background_reads == 2) {
            const uint32_t changed = 1;
            std::memcpy(dst, &changed, n);
            return true;
        }
        return read(at, dst, n);
    };
    assert(ReadVisiblePlantCandidates(change_background, image, true, result) == PlantCandidateStatus::changed && result.empty());
    auto too_many = header;
    too_many.used = 17;
    put(board + 0xac, &too_many, sizeof(too_many));
    assert(ReadVisiblePlantCandidates(read, image, true, result) == PlantCandidateStatus::not_ready && result.empty());
    put(board + 0xac, &header, sizeof(header));
    int reads = 0;
    auto change_slot = [&](uint32_t at, void* dst, size_t n) {
        if (at == block + 0x148 && ++reads == 2) {
            const uint32_t changed = 0x20000;
            std::memcpy(dst, &changed, n);
            return true;
        }
        return read(at, dst, n);
    };
    assert(ReadVisiblePlantCandidates(change_slot, image, true, result) == PlantCandidateStatus::changed && result.empty());
    auto fail = [&](uint32_t at, void* dst, size_t n) { return at != block + 0x1c && read(at, dst, n); };
    assert(ReadVisiblePlantCandidates(fail, image, true, result) == PlantCandidateStatus::read_failed && result.empty());
}
