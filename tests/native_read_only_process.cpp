#include "../src/native/read_only_process.h"
#include <cassert>
#include <iostream>

using namespace naruto;
int wmain(int argc, wchar_t** argv) {
    ReadOnlyResearchProcess process;
    CandidateSnapshot snapshot{};
    assert(process.Snapshot({true, true}, snapshot) == CandidateStatus::not_ready);
    assert(!snapshot.sun_candidate);
    assert(process.Open(GetCurrentProcessId(), nullptr) == ProcessStatus::file_rejected);
    assert(process.image_base() == 0);
    if (argc == 3 && std::wstring(argv[1]) == L"--game") {
        const auto result = process.Open(GetCurrentProcessId(), argv[2]);
        std::wcout << L"unrelated process rejected: " << (result == ProcessStatus::image_mismatch ? L"yes" : L"no") << L"\n";
        assert(process.Snapshot({true, true}, snapshot) == CandidateStatus::not_ready);
        return result == ProcessStatus::image_mismatch && process.image_base() == 0 ? 0 : 1;
    }
    process.Close();
    assert(process.image_base() == 0);

    // Exercise the same OS-level file matching and bounded read primitives against
    // this x86 test process only; production must first authenticate its own file.
    wchar_t self_path[MAX_PATH]{};
    assert(GetModuleFileNameW(nullptr, self_path, MAX_PATH) != 0);
    UniqueHandle self_file(CreateFileW(self_path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    assert(self_file);
    const auto self_module = GetModuleHandleW(nullptr);
    MODULEINFO self_info{};
    assert(GetModuleInformation(GetCurrentProcess(), self_module, &self_info, sizeof(self_info)));
    const auto base = reinterpret_cast<uintptr_t>(self_info.lpBaseOfDll);
    UniqueHandle self_process;
    assert(detail::OpenMatchingProcess(GetCurrentProcessId(), self_file.get(), base, self_info.SizeOfImage, self_process)
           == ProcessStatus::ok);
    assert(self_process);
    uint32_t original = 0x12345678;
    uint32_t observed = 0;
    assert(detail::ReadBounded(self_process.get(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&original)),
                               &observed, sizeof(observed)));
    assert(observed == original);
    // The fixture is its own target: its read-only handle must not permit writes.
    const uint32_t replacement = 0x87654321;
    SIZE_T written = 0;
    assert(!WriteProcessMemory(self_process.get(), &original, &replacement, sizeof(replacement), &written));
    assert(original == 0x12345678);
    assert(!detail::ReadBounded(self_process.get(), 0xffffffffU, &observed, 2));
    assert(!detail::ReadBounded(self_process.get(), 0, &observed, 257));
    assert(!detail::ReadBounded(self_process.get(), 0, nullptr, 4));
    UniqueHandle mismatch;
    assert(detail::OpenMatchingProcess(GetCurrentProcessId(), self_file.get(), base + 1, self_info.SizeOfImage, mismatch)
           == ProcessStatus::module_mismatch && !mismatch);
    self_process.reset();
    assert(!detail::ReadBounded(self_process.get(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&original)),
                                &observed, sizeof(observed)));
}
