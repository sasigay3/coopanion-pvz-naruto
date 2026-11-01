#include "../src/native/target_file.h"
#include <cassert>
#include <iostream>

using namespace naruto;
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--game") {
        UniqueHandle target;
        const auto result = VerifyResearchFile(argv[2], target);
        std::wcout << L"pinned file verified: " << (result == FileStatus::ok && bool(target) ? L"yes" : L"no") << L"\n";
        return result == FileStatus::ok && target ? 0 : 1;
    }
    UniqueHandle file;
    assert(VerifyResearchFile(nullptr, file) == FileStatus::open_failed && !file);
    wchar_t current[MAX_PATH]{};
    assert(GetModuleFileNameW(nullptr, current, MAX_PATH) != 0);
    assert(VerifyResearchFile(current, file) == FileStatus::invalid_file && !file);
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, directory) != 0);
    assert(GetTempFileNameW(directory, L"npv", 0, path) != 0);
    {
        UniqueHandle temp(CreateFileW(path, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        assert(temp);
        std::vector<uint8_t> data(7340032);
        IMAGE_DOS_HEADER dos{};
        dos.e_magic = 0;
        dos.e_lfanew = 0x80;
        std::memcpy(data.data(), &dos, sizeof(dos));
        IMAGE_NT_HEADERS32 nt{};
        nt.Signature = IMAGE_NT_SIGNATURE;
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt.FileHeader.TimeDateStamp = 0x49ecf563;
        nt.FileHeader.SizeOfOptionalHeader = sizeof(nt.OptionalHeader);
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt.OptionalHeader.ImageBase = 0x400000;
        nt.OptionalHeader.AddressOfEntryPoint = 0x6ea62c;
        nt.OptionalHeader.SizeOfImage = 0x7b7000;
        nt.OptionalHeader.CheckSum = 0x70cdb3;
        nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = 0x77f000;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0x518;
        std::memcpy(data.data() + dos.e_lfanew, &nt, sizeof(nt));
        DWORD written = 0;
        assert(WriteFile(temp.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr));
        assert(written == data.size());
    }
    assert(VerifyResearchFile(path, file) == FileStatus::pe_mismatch && !file);
    {
        UniqueHandle temp(CreateFileW(path, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        assert(temp);
        const WORD dos_signature = IMAGE_DOS_SIGNATURE;
        DWORD written = 0;
        assert(WriteFile(temp.get(), &dos_signature, sizeof(dos_signature), &written, nullptr));
        assert(written == sizeof(dos_signature));
    }
    assert(VerifyResearchFile(path, file) == FileStatus::hash_mismatch && !file);
    assert(DeleteFileW(path));
}
