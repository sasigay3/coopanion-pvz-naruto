#include "read_only_process.h"
#include "research_cli_args.h"
#include <iostream>

using namespace naruto;

static const char* ProcessReason(ProcessStatus status) {
    switch (status) {
    case ProcessStatus::ok: return "ok";
    case ProcessStatus::file_rejected: return "file_rejected";
    case ProcessStatus::open_failed: return "open_failed";
    case ProcessStatus::exited: return "exited";
    case ProcessStatus::architecture_mismatch: return "architecture_mismatch";
    case ProcessStatus::path_unavailable: return "path_unavailable";
    case ProcessStatus::image_mismatch: return "image_mismatch";
    case ProcessStatus::module_unavailable: return "module_unavailable";
    case ProcessStatus::module_mismatch: return "module_mismatch";
    case ProcessStatus::not_ready: return "not_ready";
    case ProcessStatus::memory_mismatch: return "memory_mismatch";
    }
    return "unknown";
}
static const char* SnapshotReason(CandidateStatus status) {
    switch (status) {
    case CandidateStatus::ok: return "ok";
    case CandidateStatus::invalid_base: return "invalid_base";
    case CandidateStatus::read_failed: return "read_failed";
    case CandidateStatus::code_mismatch: return "code_mismatch";
    case CandidateStatus::not_ready: return "not_ready";
    case CandidateStatus::object_mismatch: return "object_mismatch";
    case CandidateStatus::invalid_value: return "invalid_value";
    case CandidateStatus::changed: return "changed";
    }
    return "unknown";
}
int wmain(int argc, wchar_t** argv) {
    ResearchCliArgs args{};
    if (!ParseResearchCli(argc, argv, args)) {
        std::cout << "{\"status\":\"invalid_arguments\"}\n";
        return 2;
    }
    ReadOnlyResearchProcess target;
    const auto status = target.Open(args.pid, args.executable.c_str());
    if (status != ProcessStatus::ok) {
        std::cout << "{\"status\":\"" << ProcessReason(status) << "\"}\n";
        return 1;
    }
    CandidateSnapshot snapshot{};
    const auto result = target.Snapshot({args.sun, args.cards, args.plant_array}, snapshot);
    if (result != CandidateStatus::ok) {
        std::cout << "{\"status\":\"" << SnapshotReason(result) << "\"}\n";
        return 1;
    }
    std::cout << "{\"status\":\"ok\",\"research_only\":true,\"scene_candidate\":" << snapshot.scene_candidate
              << ",\"mode_candidate\":" << snapshot.mode_candidate;
    if (snapshot.sun_candidate) std::cout << ",\"sun_candidate\":" << *snapshot.sun_candidate;
    if (snapshot.card_count_candidate) std::cout << ",\"card_count_candidate\":" << *snapshot.card_count_candidate;
    if (snapshot.plant_array_candidate) {
        const auto& counts = *snapshot.plant_array_candidate;
        std::cout << ",\"plant_array_candidate\":{\"used\":" << counts.used
                  << ",\"capacity\":" << counts.capacity << ",\"count\":" << counts.count << "}";
    }
    std::cout << "}\n";
    return 0;
}
