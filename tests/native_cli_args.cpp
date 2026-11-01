#include "../src/native/research_cli_args.h"
#include <cassert>

using namespace naruto;
int main() {
    ResearchCliArgs args{};
    const wchar_t* valid[] = {L"probe", L"--pid", L"42", L"--executable", L"example.exe", L"--sun", L"--cards"};
    assert(ParseResearchCli(7, valid, args));
    assert(args.pid == 42 && args.executable == L"example.exe" && args.sun && args.cards);
    const wchar_t* zero[] = {L"probe", L"--pid", L"0", L"--executable", L"x"};
    assert(!ParseResearchCli(5, zero, args) && args.pid == 0);
    const wchar_t* negative[] = {L"probe", L"--pid", L"-1", L"--executable", L"x"};
    assert(!ParseResearchCli(5, negative, args));
    const wchar_t* overflow[] = {L"probe", L"--pid", L"4294967296", L"--executable", L"x"};
    assert(!ParseResearchCli(5, overflow, args));
    const wchar_t* duplicate[] = {L"probe", L"--pid", L"1", L"--pid", L"2", L"--executable", L"x"};
    assert(!ParseResearchCli(7, duplicate, args));
    const wchar_t* unknown[] = {L"probe", L"--pid", L"1", L"--executable", L"x", L"--write"};
    assert(!ParseResearchCli(6, unknown, args));
    const wchar_t* empty[] = {L"probe", L"--pid", L"1", L"--executable", L""};
    assert(!ParseResearchCli(5, empty, args));
}
