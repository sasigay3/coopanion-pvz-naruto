#pragma once

#include "target_file.h"
#include "candidate_snapshot.h"
#include "plant_candidate.h"
#include <psapi.h>
#include <string>
#pragma comment(lib, "psapi.lib")

namespace naruto {

enum class ProcessStatus {
    ok, file_rejected, open_failed, exited, architecture_mismatch,
    path_unavailable, image_mismatch, module_unavailable, module_mismatch, not_ready, memory_mismatch
};

inline bool SameFile(HANDLE first, HANDLE second) {
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    return GetFileInformationByHandle(first, &a) && GetFileInformationByHandle(second, &b) &&
        a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
        a.nFileIndexLow == b.nFileIndexLow;
}

namespace detail {

// Internal primitive: the caller must authenticate `verified_file` first.
// Expected base and size are supplied by the pinned production profile.
inline ProcessStatus OpenMatchingProcess(DWORD pid, HANDLE verified_file, uintptr_t expected_base,
                                         DWORD expected_size, UniqueHandle& output) {
    output.reset();
    if (!verified_file || verified_file == INVALID_HANDLE_VALUE) return ProcessStatus::file_rejected;
    UniqueHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (!process) return ProcessStatus::open_failed;
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.get(), &exit_code) || exit_code != STILL_ACTIVE)
        return ProcessStatus::exited;
    BOOL self_wow = FALSE, target_wow = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &self_wow) || !IsWow64Process(process.get(), &target_wow) ||
        !self_wow || !target_wow) return ProcessStatus::architecture_mismatch;
    std::vector<wchar_t> image_path(32768);
    DWORD image_length = static_cast<DWORD>(image_path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, image_path.data(), &image_length) ||
        image_length >= image_path.size()) return ProcessStatus::path_unavailable;
    image_path[image_length] = L'\0';
    UniqueHandle process_file(CreateFileW(image_path.data(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!process_file || !SameFile(verified_file, process_file.get())) return ProcessStatus::image_mismatch;
    std::array<HMODULE, 1024> modules{};
    DWORD needed = 0;
    if (!EnumProcessModulesEx(process.get(), modules.data(), static_cast<DWORD>(sizeof(modules)), &needed,
                              LIST_MODULES_32BIT) || needed < sizeof(HMODULE) || needed > sizeof(modules) ||
        needed % sizeof(HMODULE) != 0) return ProcessStatus::module_unavailable;
    std::vector<wchar_t> module_path(32768);
    const DWORD module_length = GetModuleFileNameExW(process.get(), modules[0], module_path.data(),
                                                      static_cast<DWORD>(module_path.size()));
    if (!module_length || module_length >= module_path.size() - 1) return ProcessStatus::module_unavailable;
    UniqueHandle module_file(CreateFileW(module_path.data(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!module_file || !SameFile(verified_file, module_file.get())) return ProcessStatus::module_mismatch;
    MODULEINFO module_info{};
    if (!GetModuleInformation(process.get(), modules[0], &module_info, sizeof(module_info)))
        return ProcessStatus::module_unavailable;
    if (reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll) != expected_base ||
        module_info.SizeOfImage != expected_size) return ProcessStatus::module_mismatch;
    output = std::move(process);
    return ProcessStatus::ok;
}

inline bool ReadBounded(HANDLE process, uint32_t address, void* destination, size_t size) {
    if (!process || process == INVALID_HANDLE_VALUE || !destination || !size || size > 256 ||
        uint64_t(address) + size > uint64_t(UINT32_MAX) + 1) return false;
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process, &exit_code) || exit_code != STILL_ACTIVE) return false;
    SIZE_T count = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),
                             destination, size, &count) && count == size;
}
}  // namespace detail

// A research-only attachment with no rights to write memory, inject, create threads,
// send input, or terminate a process. Never launches a process.
class ReadOnlyResearchProcess {
public:
    ProcessStatus Open(DWORD pid, const wchar_t* path) {
        Close();
        UniqueHandle verified;
        if (VerifyResearchFile(path, verified) != FileStatus::ok) return ProcessStatus::file_rejected;
        UniqueHandle process;
        const auto identity = detail::OpenMatchingProcess(pid, verified.get(), 0x400000, 0x7b7000, process);
        if (identity != ProcessStatus::ok) return identity;
        auto memory = [&](uint32_t address, void* destination, size_t size) {
            return detail::ReadBounded(process.get(), address, destination, size);
        };
        CandidateSnapshot candidate{};
        const auto status = ReadCandidateSnapshot(memory, 0x400000, {}, candidate);
        if (status == CandidateStatus::not_ready) return ProcessStatus::not_ready;
        if (status != CandidateStatus::ok) return ProcessStatus::memory_mismatch;
        process_ = std::move(process);
        file_ = std::move(verified);
        image_base_ = 0x400000;
        return ProcessStatus::ok;
    }

    void Close() { process_.reset(); file_.reset(); image_base_ = 0; }
    CandidateStatus Snapshot(CandidateOptions options, CandidateSnapshot& output) const {
        output = {};
        if (!process_) return CandidateStatus::not_ready;
        auto read = [&](uint32_t address, void* destination, size_t size) {
            return detail::ReadBounded(process_.get(), address, destination, size);
        };
        return ReadCandidateSnapshot(read, image_base_, options, output);
    }
    // Research only: the caller must independently confirm a visible ordinary
    // daytime board. This is not exposed by the World or CLI.
    PlantCandidateStatus VisiblePlantCandidates(bool confirmed, std::vector<PlantCandidateWords>& output) const {
        output.clear();
        if (!process_) return PlantCandidateStatus::not_ready;
        auto read = [&](uint32_t address, void* destination, size_t size) {
            return detail::ReadBounded(process_.get(), address, destination, size);
        };
        return ReadVisiblePlantCandidates(read, image_base_, confirmed, output);
    }
    uint32_t image_base() const { return image_base_; }
private:
    UniqueHandle process_;
    UniqueHandle file_;
    uint32_t image_base_ = 0;
};

}  // namespace naruto
