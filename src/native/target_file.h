#pragma once

#ifndef _WIN32
#error This research component requires Windows.
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>
#pragma comment(lib, "bcrypt.lib")

namespace naruto {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) : value_(value) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) { reset(); value_ = other.release(); }
        return *this;
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE release() { HANDLE value = value_; value_ = nullptr; return value; }
    void reset(HANDLE value = nullptr) {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_ = nullptr;
};

enum class FileStatus { ok, open_failed, invalid_file, read_failed, pe_mismatch, crypto_failed, hash_mismatch };

// Retain the verified handle during the research session. It allows other readers,
// but denies later write/delete opens while held. This alone does not authenticate
// the already-loaded process image; process and in-memory checks are separate.
inline FileStatus VerifyResearchFile(const wchar_t* path, UniqueHandle& verified_file) {
    verified_file.reset();
    if (!path || !*path) return FileStatus::open_failed;
    UniqueHandle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file) return FileStatus::open_failed;
    BY_HANDLE_FILE_INFORMATION file_info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file.get(), &file_info) || !GetFileSizeEx(file.get(), &size))
        return FileStatus::read_failed;
    if ((file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || size.QuadPart != 7340032)
        return FileStatus::invalid_file;
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
    size_t total = 0;
    while (total < bytes.size()) {
        DWORD received = 0;
        const DWORD request = static_cast<DWORD>(bytes.size() - total);
        if (!ReadFile(file.get(), bytes.data() + total, request, &received, nullptr) || !received)
            return FileStatus::read_failed;
        total += received;
    }
    IMAGE_DOS_HEADER dos{};
    if (bytes.size() < sizeof(dos)) return FileStatus::pe_mismatch;
    std::memcpy(&dos, bytes.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        uint64_t(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes.size())
        return FileStatus::pe_mismatch;
    IMAGE_NT_HEADERS32 nt{};
    std::memcpy(&nt, bytes.data() + dos.e_lfanew, sizeof(nt));
    const auto& opt = nt.OptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.FileHeader.TimeDateStamp != 0x49ecf563 || nt.FileHeader.SizeOfOptionalHeader < sizeof(opt) ||
        opt.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || opt.ImageBase != 0x400000 ||
        opt.AddressOfEntryPoint != 0x6ea62c || opt.SizeOfImage != 0x7b7000 ||
        opt.CheckSum != 0x70cdb3 || (opt.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) ||
        opt.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_BASERELOC ||
        opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress != 0x77f000 ||
        opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size != 0x518) return FileStatus::pe_mismatch;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return FileStatus::crypto_failed;
    struct AlgorithmGuard { BCRYPT_ALG_HANDLE h; ~AlgorithmGuard() { BCryptCloseAlgorithmProvider(h, 0); } } guard{algorithm};
    DWORD object_size = 0, returned = 0, digest_size = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &returned, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size), sizeof(digest_size), &returned, 0) < 0 ||
        !object_size || object_size > 1024 * 1024 || digest_size != 32) return FileStatus::crypto_failed;
    std::vector<uint8_t> object(object_size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0)
        return FileStatus::crypto_failed;
    struct HashGuard { BCRYPT_HASH_HANDLE h; ~HashGuard() { BCryptDestroyHash(h); } } hash_guard{hash};
    std::array<uint8_t, 32> digest{};
    if (BCryptHashData(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        return FileStatus::crypto_failed;
    constexpr std::array<uint8_t, 32> expected{{
        0xc5,0x36,0xaa,0xff,0x66,0x79,0xfd,0x39,0x1e,0xe2,0xdd,0xb4,0x71,0x1b,0x4f,0x98,
        0x3f,0x4c,0xe0,0xea,0x9b,0x32,0xec,0xc6,0x64,0x56,0xce,0xad,0x5f,0xdc,0x59,0xa2
    }};
    if (digest != expected) return FileStatus::hash_mismatch;
    verified_file = std::move(file);
    return FileStatus::ok;
}

}  // namespace naruto
