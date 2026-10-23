#include "OSGBFiles.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <cwctype>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace geostudio::native::io::detail
{
namespace fs = std::filesystem;
namespace
{
#ifdef _WIN32
// Win32 原生文件 API 使用扩展长度绝对路径；逻辑校验仍使用原路径，不改变相对路径语义。
fs::path extendedPath(const fs::path& path)
{
    auto value = fs::absolute(path).lexically_normal().wstring();
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.compare(0, 4, L"\\\\?\\") == 0) return fs::path(value);
    if (value.compare(0, 2, L"\\\\") == 0) return fs::path(L"\\\\?\\UNC\\" + value.substr(2));
    return fs::path(L"\\\\?\\" + value);
}
std::string fileFailure(const std::string& action, DWORD error, const fs::path& path)
{
    return action + " (Win32=" + std::to_string(error) + ", pathChars=" + std::to_string(path.native().size()) + "): " + path.u8string();
}
// 每次哈希持有独立 CNG 句柄；不跨线程共享可变状态，析构归还系统资源。
class Hash
{
public:
    Hash()
    {
        if (BCryptOpenAlgorithmProvider(&_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Cannot open SHA-256 provider");
        if (BCryptCreateHash(_algorithm, &_hash, nullptr, 0, nullptr, 0, 0) < 0)
        {
            BCryptCloseAlgorithmProvider(_algorithm, 0);
            _algorithm = nullptr;
            throw std::runtime_error("Cannot create SHA-256 context");
        }
    }
    ~Hash()
    {
        if (_hash) BCryptDestroyHash(_hash);
        if (_algorithm) BCryptCloseAlgorithmProvider(_algorithm, 0);
    }
    void update(const char* bytes, size_t length)
    {
        while (length)
        {
            auto count = static_cast<ULONG>(std::min<size_t>(length, 1024 * 1024));
            if (BCryptHashData(_hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)), count, 0) < 0)
                throw std::runtime_error("SHA-256 update failed");
            bytes += count;
            length -= count;
        }
    }
    std::string finish()
    {
        std::array<UCHAR, 32> digest{};
        if (BCryptFinishHash(_hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("SHA-256 finish failed");
        static constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
        return result;
    }
private:
    BCRYPT_ALG_HANDLE _algorithm = nullptr;
    BCRYPT_HASH_HANDLE _hash = nullptr;
};
#endif
}

fs::path nativeFilePath(const fs::path& path)
{
#ifdef _WIN32
    return extendedPath(path);
#else
    return fs::absolute(path).lexically_normal();
#endif
}

fs::path canonicalFilePath(const fs::path& path)
{
    auto resolved = fs::weakly_canonical(nativeFilePath(path));
#ifdef _WIN32
    auto value = resolved.wstring();
    if (value.compare(0, 8, L"\\\\?\\UNC\\") == 0) return fs::path(L"\\\\" + value.substr(8));
    if (value.compare(0, 4, L"\\\\?\\") == 0) return fs::path(value.substr(4));
#endif
    return resolved;
}

std::string sha256Bytes(std::string_view bytes)
{
#ifdef _WIN32
    Hash hash;
    hash.update(bytes.data(), bytes.size());
    return hash.finish();
#else
    (void)bytes;
    throw std::runtime_error("OSGB SHA-256 currently requires Windows BCrypt");
#endif
}

std::string sha256File(const fs::path& path)
{
#ifdef _WIN32
    std::ifstream stream(extendedPath(path), std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read file for SHA-256: " + path.u8string());
    Hash hash;
    std::array<char, 65536> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hash.update(buffer.data(), static_cast<size_t>(stream.gcount()));
    }
    if (!stream.eof()) throw std::runtime_error("Failed reading file for SHA-256: " + path.u8string());
    return hash.finish();
#else
    (void)path;
    throw std::runtime_error("OSGB SHA-256 currently requires Windows BCrypt");
#endif
}

std::string readBoundedFile(const fs::path& path, uint64_t maximumBytes)
{
    const auto native = nativeFilePath(path);
    if (!fs::is_regular_file(native) || fs::file_size(native) > maximumBytes)
        throw std::runtime_error("Missing or oversized file: " + path.u8string());
    std::ifstream stream(native, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read file: " + path.u8string());
    std::string result;
    std::array<char, 16384> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (result.size() + static_cast<uint64_t>(stream.gcount()) > maximumBytes)
            throw std::runtime_error("File grew beyond read limit: " + path.u8string());
        result.append(buffer.data(), static_cast<size_t>(stream.gcount()));
    }
    if (!stream.eof()) throw std::runtime_error("Failed reading file: " + path.u8string());
    return result;
}

bool pathWithin(const fs::path& child, const fs::path& parent)
{
    const auto c = canonicalFilePath(child);
    const auto p = canonicalFilePath(parent);
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci)
    {
        if (ci == c.end()) return false;
#ifdef _WIN32
        auto a = ci->wstring(), b = pi->wstring();
        std::transform(a.begin(), a.end(), a.begin(), [](wchar_t v) { return static_cast<wchar_t>(std::towlower(v)); });
        std::transform(b.begin(), b.end(), b.begin(), [](wchar_t v) { return static_cast<wchar_t>(std::towlower(v)); });
        if (a != b) return false;
#else
        if (*ci != *pi) return false;
#endif
    }
    return true;
}

void validateArtifactDirectory(const fs::path& directory, const fs::path& sourceRoot)
{
    if (directory.empty() || sourceRoot.empty() || !fs::is_directory(nativeFilePath(sourceRoot)))
        throw std::runtime_error("OSGB artifact directory requires a valid source root");
    if (pathWithin(directory, sourceRoot) || pathWithin(sourceRoot, directory))
        throw std::runtime_error("OSGB artifact directory must be separate from source data");
    if (fs::exists(nativeFilePath(directory)) && !fs::is_directory(nativeFilePath(directory)))
        throw std::runtime_error("OSGB artifact output is not a directory");
}

void publishArtifactText(const fs::path& directory, const fs::path& sourceRoot,
    const std::string& filename, std::string_view contents, bool durable)
{
    const auto name = fs::u8path(filename);
    if (filename.empty() || name.has_parent_path() || name.is_absolute() || filename == "." || filename == ".." ||
        filename.find(':') != std::string::npos || filename.find('\\') != std::string::npos || filename.find('/') != std::string::npos)
        throw std::runtime_error("Invalid OSGB artifact filename");
    validateArtifactDirectory(directory, sourceRoot);
#ifdef _WIN32
    fs::create_directories(nativeFilePath(directory));
    validateArtifactDirectory(directory, sourceRoot);
    const auto target = directory / name;
    const auto pending = directory / fs::u8path(filename + ".pending");
    const auto lockPath = directory / fs::u8path(filename + ".lock");
    if (!pathWithin(target, directory) || !pathWithin(pending, directory) || !pathWithin(lockPath, directory))
        throw std::runtime_error("OSGB artifact path escapes output directory");
    // CREATE_NEW 防止覆盖未知锁；本次创建的锁用关闭句柄时删除的系统语义释放。
    const auto nativeLock = extendedPath(lockPath), nativePending = extendedPath(pending), nativeTarget = extendedPath(target);
    HANDLE lock = CreateFileW(nativeLock.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error(fileFailure("OSGB artifact writer lock cannot be acquired", GetLastError(), lockPath));
    struct Owner { HANDLE handle; ~Owner() { CloseHandle(handle); } } lockOwner{lock};
    HANDLE file = CreateFileW(nativePending.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error(fileFailure("OSGB pending artifact cannot be created", GetLastError(), pending));
    DWORD writeError = ERROR_SUCCESS;
    size_t offset = 0;
    while (offset < contents.size())
    {
        DWORD count = static_cast<DWORD>(std::min<size_t>(contents.size() - offset, 1024 * 1024)), actual = 0;
        if (!WriteFile(file, contents.data() + offset, count, &actual, nullptr))
        { writeError = GetLastError(); break; }
        if (actual != count) { writeError = ERROR_WRITE_FAULT; break; }
        offset += actual;
    }
    if (durable && !FlushFileBuffers(file) && writeError == ERROR_SUCCESS) writeError = GetLastError();
    if (!CloseHandle(file) && writeError == ERROR_SUCCESS) writeError = GetLastError();
    if (writeError != ERROR_SUCCESS) throw std::runtime_error(fileFailure("OSGB artifact write failed; pending preserved", writeError, pending));
    const DWORD moveFlags = MOVEFILE_REPLACE_EXISTING | (durable ? MOVEFILE_WRITE_THROUGH : 0);
    if (!MoveFileExW(nativePending.c_str(), nativeTarget.c_str(), moveFlags))
        throw std::runtime_error(fileFailure("OSGB artifact publish failed; previous and pending preserved", GetLastError(), target));
#else
    (void)contents;
    (void)durable;
    throw std::runtime_error("OSGB atomic artifact publication currently requires Windows");
#endif
}
}
