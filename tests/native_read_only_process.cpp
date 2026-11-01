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
}
