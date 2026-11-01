#include "../src/native/candidate_snapshot.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>

using namespace naruto;
int main() {
    constexpr uint32_t image = 0x400000, root = 0x10000000;
    constexpr uint32_t board = 0x20000000, bank = 0x30000000;
    std::map<uint32_t, std::vector<uint8_t>> memory;
    auto bytes = [&](uint32_t address, const void* data, size_t size) {
        auto p = static_cast<const uint8_t*>(data);
        memory[address] = std::vector<uint8_t>(p, p + size);
    };
    auto put = [&](uint32_t address, uint32_t value) { bytes(address, &value, 4); };
    for (const auto& sig : kResearchCodePrefixes) bytes(image + sig.rva, sig.bytes.data(), sig.bytes.size());
    put(image + 0x2a9ec0, root);
    put(root, image + 0x267ba0);
    put(root + 4, image + 0x267d50);
    put(root + 0x7fc, 3);
    put(root + 0x7f8, 0);
    put(root + 0x768, board);
    put(board + 0x5560, 75);
    put(board + 0x144, bank);
    put(bank + 0x24, 5);
    auto read = [&](uint32_t address, void* out, size_t size) {
        const auto found = memory.find(address);
        if (found == memory.end() || found->second.size() != size) return false;
        std::memcpy(out, found->second.data(), size);
        return true;
    };
    CandidateSnapshot snapshot{};
    assert(ReadCandidateSnapshot(read, image, {true, true}, snapshot) == CandidateStatus::ok);
    assert(snapshot.scene_candidate == 3 && snapshot.mode_candidate == 0);
    assert(snapshot.sun_candidate == 75U && snapshot.card_count_candidate == 5U);
    assert(ReadCandidateSnapshot(read, image + 1, {true, true}, snapshot) == CandidateStatus::invalid_base);
    assert(!snapshot.sun_candidate && !snapshot.card_count_candidate);

    const auto saved = memory.at(image + kResearchCodePrefixes[0].rva);
    memory[image + kResearchCodePrefixes[0].rva][0] ^= 1;
    auto no_root_on_bad_code = [&](uint32_t address, void* out, size_t size) {
        assert(address != image + 0x2a9ec0 && "root read before code validation");
        return read(address, out, size);
    };
    assert(ReadCandidateSnapshot(no_root_on_bad_code, image, {true, true}, snapshot) == CandidateStatus::code_mismatch);
    assert(!snapshot.sun_candidate && !snapshot.card_count_candidate);
    memory[image + kResearchCodePrefixes[0].rva] = saved;
    put(root, 0x1234);
    assert(ReadCandidateSnapshot(read, image, {true, true}, snapshot) == CandidateStatus::object_mismatch);
    put(root, image + 0x267ba0);
    put(root + 0x7fc, 1);
    put(root + 0x768, 0);
    assert(ReadCandidateSnapshot(read, image, {false, false}, snapshot) == CandidateStatus::ok);
    assert(snapshot.scene_candidate == 1 && !snapshot.sun_candidate);
    assert(ReadCandidateSnapshot(read, image, {true, true}, snapshot) == CandidateStatus::not_ready);
    put(root + 0x7fc, 3);
    put(root + 0x768, board);
    put(bank + 0x24, 11);
    assert(ReadCandidateSnapshot(read, image, {true, true}, snapshot) == CandidateStatus::invalid_value);
    assert(!snapshot.sun_candidate && !snapshot.card_count_candidate);
    put(bank + 0x24, 5);
    int scene_reads = 0;
    auto changed_scene = [&](uint32_t address, void* out, size_t size) {
        if (address == root + 0x7fc && ++scene_reads == 2) {
            const uint32_t later = 1;
            std::memcpy(out, &later, size);
            return true;
        }
        return read(address, out, size);
    };
    assert(ReadCandidateSnapshot(changed_scene, image, {true, true}, snapshot) == CandidateStatus::changed);
    assert(!snapshot.sun_candidate && !snapshot.card_count_candidate);
    int bank_reads = 0;
    auto changed_bank = [&](uint32_t address, void* out, size_t size) {
        if (address == board + 0x144 && ++bank_reads == 2) {
            const uint32_t later = bank + 0x1000;
            std::memcpy(out, &later, size);
            return true;
        }
        if (bank_reads == 2 && address == bank + 0x24) assert(false && "stale bank read");
        return read(address, out, size);
    };
    assert(ReadCandidateSnapshot(changed_bank, image, {true, true}, snapshot) == CandidateStatus::changed);
    int sun_reads = 0;
    auto changed_sun = [&](uint32_t address, void* out, size_t size) {
        if (address == board + 0x5560 && ++sun_reads == 2) {
            const uint32_t later = 100;
            std::memcpy(out, &later, size);
            return true;
        }
        return read(address, out, size);
    };
    assert(ReadCandidateSnapshot(changed_sun, image, {true, true}, snapshot) == CandidateStatus::changed);
    assert(!snapshot.sun_candidate && !snapshot.card_count_candidate);
    int root_reads = 0;
    auto changed_root = [&](uint32_t address, void* out, size_t size) {
        if (address == image + 0x2a9ec0 && ++root_reads == 2) {
            const uint32_t later = root + 0x1000;
            std::memcpy(out, &later, size);
            return true;
        }
        if (root_reads == 2 && address == root) assert(false && "stale root read");
        return read(address, out, size);
    };
    assert(ReadCandidateSnapshot(changed_root, image, {true, true}, snapshot) == CandidateStatus::changed);
    assert(!snapshot.sun_candidate && !snapshot.card_count_candidate);
    auto fail_sun = [&](uint32_t address, void* out, size_t size) {
        return address != board + 0x5560 && read(address, out, size);
    };
    assert(ReadCandidateSnapshot(fail_sun, image, {true, false}, snapshot) == CandidateStatus::read_failed);
    assert(!snapshot.sun_candidate);
}
