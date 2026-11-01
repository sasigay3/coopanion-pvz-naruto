#pragma once

#include "target_file.h"
#include "candidate_snapshot.h"
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

// A research-only attachment with no rights to write memory, inject, create threads,
// send input, or terminate a process. Never launches a process.
class ReadOnlyResearchProcess {
public:
    ProcessStatus Open(DWORD pid, const wchar_t* path) {
        process_.reset();
        file_.reset();
        image_base_ = 0;
        UniqueHandle verified;
        if (VerifyResearchFile(path, verified) != FileStatus::ok) return ProcessStatus::file_rejected;
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
        if (!QueryFullProcessImageNameW(process.get(), 0, image_path.data(), &image_length))
            return ProcessStatus::path_unavailable;
        UniqueHandle process_file(CreateFileW(image_path.data(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!process_file || !SameFile(verified.get(), process_file.get())) return ProcessStatus::image_mismatch;
        std::array<HMODULE, 1024> modules{};
        DWORD needed = 0;
        if (!EnumProcessModulesEx(process.get(), modules.data(), static_cast<DWORD>(sizeof(modules)), &needed,
                                  LIST_MODULES_32BIT) || needed < sizeof(HMODULE) || needed > sizeof(modules) ||
            needed % sizeof(HMODULE) != 0) return ProcessStatus::module_unavailable;
        std::vector<wchar_t> module_path(32768);
        if (!GetModuleFileNameExW(process.get(), modules[0], module_path.data(), static_cast<DWORD>(module_path.size())))
            return ProcessStatus::module_unavailable;
        UniqueHandle module_file(CreateFileW(module_path.data(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!module_file || !SameFile(verified.get(), module_file.get())) return ProcessStatus::module_mismatch;
        MODULEINFO module_info{};
        if (!GetModuleInformation(process.get(), modules[0], &module_info, sizeof(module_info)))
            return ProcessStatus::module_unavailable;
        const auto base = reinterpret_cast<uintptr_t>(module_info.lpBaseOfDll);
        if (base != 0x400000 || module_info.SizeOfImage != 0x7b7000)
            return ProcessStatus::module_mismatch;
        // Validate all signatures and any initialized candidate object before exposing a reader.
        auto memory = [&](uint32_t address, void* destination, size_t size) {
            if (!size || size > 256 || uint64_t(address) + size > uint64_t(UINT32_MAX) + 1) return false;
            SIZE_T read = 0;
            return ReadProcessMemory(process.get(), reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),
                                     destination, size, &read) && read == size;
        };
        CandidateSnapshot candidate{};
        const auto status = ReadCandidateSnapshot(memory, static_cast<uint32_t>(base), {}, candidate);
        if (status == CandidateStatus::not_ready) return ProcessStatus::not_ready;
        if (status != CandidateStatus::ok) return ProcessStatus::memory_mismatch;
        process_ = std::move(process);
        file_ = std::move(verified);
        image_base_ = static_cast<uint32_t>(base);
        return ProcessStatus::ok;
    }

    void Close() { process_.reset(); file_.reset(); image_base_ = 0; }
    CandidateStatus Snapshot(CandidateOptions options, CandidateSnapshot& output) const {
        output = {};
        if (!process_) return CandidateStatus::not_ready;
        auto read = [&](uint32_t address, void* destination, size_t size) {
            return Read(address, destination, size);
        };
        return ReadCandidateSnapshot(read, image_base_, options, output);
    }
    uint32_t image_base() const { return image_base_; }
private:
    bool Read(uint32_t address, void* destination, size_t size) const {
        if (!process_ || !destination || !size || size > 256 ||
            uint64_t(address) + size > uint64_t(UINT32_MAX) + 1) return false;
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process_.get(), &exit_code) || exit_code != STILL_ACTIVE) return false;
        SIZE_T read = 0;
        return ReadProcessMemory(process_.get(), reinterpret_cast<const void*>(static_cast<uintptr_t>(address)),
                                 destination, size, &read) && read == size;
    }
    UniqueHandle process_;
    UniqueHandle file_;
    uint32_t image_base_ = 0;
};

}  // namespace naruto
