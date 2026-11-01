#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace naruto {
struct ResearchCliArgs {
    uint32_t pid = 0;
    std::wstring executable;
    bool sun = false;
    bool cards = false;
};

// Reject ambiguity: a caller must explicitly name one PID and one executable.
inline bool ParseResearchCli(int argc, const wchar_t* const* argv, ResearchCliArgs& output) {
    output = {};
    if (argc < 1 || !argv) return false;
    ResearchCliArgs parsed{};
    bool saw_pid = false, saw_executable = false;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) return false;
        const std::wstring_view name(argv[i]);
        if (name == L"--sun" && !parsed.sun) { parsed.sun = true; continue; }
        if (name == L"--cards" && !parsed.cards) { parsed.cards = true; continue; }
        if (name == L"--pid" && !saw_pid && i + 1 < argc && argv[i + 1]) {
            const std::wstring_view value(argv[++i]);
            if (value.empty() || value.size() > 10) return false;
            uint64_t pid = 0;
            for (const wchar_t c : value) {
                if (c < L'0' || c > L'9') return false;
                pid = pid * 10 + static_cast<uint32_t>(c - L'0');
                if (pid > UINT32_MAX) return false;
            }
            if (!pid) return false;
            parsed.pid = static_cast<uint32_t>(pid);
            saw_pid = true;
            continue;
        }
        if (name == L"--executable" && !saw_executable && i + 1 < argc && argv[i + 1]) {
            parsed.executable = argv[++i];
            if (parsed.executable.empty()) return false;
            saw_executable = true;
            continue;
        }
        return false;
    }
    if (!saw_pid || !saw_executable) return false;
    output = std::move(parsed);
    return true;
}
}  // namespace naruto
