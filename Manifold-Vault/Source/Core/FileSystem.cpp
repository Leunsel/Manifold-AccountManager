#include "Core/FileSystem.h"

#include <algorithm>

#include <windows.h>

namespace Bunker
{
    namespace
    {
        bool HasWildcard(const std::string& s)
        {
            return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
        }

        std::string Narrow(const wchar_t* w)
        {
            if (!w || !*w)
                return {};
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                                nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string s(static_cast<std::size_t>(n - 1), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        std::wstring Widen(const std::string& s)
        {
            if (s.empty())
                return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                                static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  w.data(), n);
            return w;
        }

        class Win32FileSystem : public IFileSystem
        {
        public:
            bool Exists(const std::string& path) const override
            {
                const DWORD attr = ::GetFileAttributesW(Widen(path).c_str());
                return attr != INVALID_FILE_ATTRIBUTES &&
                       (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
            }

            Status Read(const std::string& path, std::vector<unsigned char>& out) override
            {
                out.clear();
                HANDLE h = ::CreateFileW(Widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE)
                    return Fail(ErrorCode::InvalidArgument, "file not readable");

                LARGE_INTEGER size = {};
                if (!::GetFileSizeEx(h, &size) || size.QuadPart < 0)
                {
                    ::CloseHandle(h);
                    return Fail(ErrorCode::InvalidArgument, "file size not readable");
                }
                if (size.QuadPart > 512ll * 1024 * 1024)
                {
                    ::CloseHandle(h);
                    return Fail(ErrorCode::FileTooLarge, "file implausibly large");
                }

                out.resize(static_cast<std::size_t>(size.QuadPart));
                std::size_t done = 0;
                while (done < out.size())
                {
                    const DWORD chunk = static_cast<DWORD>(
                        std::min<std::size_t>(out.size() - done, 1u << 20));
                    DWORD got = 0;
                    if (!::ReadFile(h, out.data() + done, chunk, &got, nullptr) || got == 0)
                    {
                        ::CloseHandle(h);
                        out.clear();
                        return Fail(ErrorCode::InvalidArgument, "read failed");
                    }
                    done += got;
                }
                ::CloseHandle(h);
                return Ok();
            }

            Status WriteAtomic(const std::string& path,
                               const unsigned char* data, std::size_t len) override
            {
                const std::wstring target = Widen(path);
                const std::wstring temp = target + L".tmp";

                HANDLE h = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h == INVALID_HANDLE_VALUE)
                    return Fail(ErrorCode::FileWriteFailed, "temp file not creatable");

                std::size_t done = 0;
                while (done < len)
                {
                    const DWORD chunk = static_cast<DWORD>(
                        std::min<std::size_t>(len - done, 1u << 20));
                    DWORD written = 0;
                    if (!::WriteFile(h, data + done, chunk, &written, nullptr) || written == 0)
                    {
                        ::CloseHandle(h);
                        ::DeleteFileW(temp.c_str());
                        return Fail(ErrorCode::FileWriteFailed, "write failed");
                    }
                    done += written;
                }

                if (!::FlushFileBuffers(h))
                {
                    ::CloseHandle(h);
                    ::DeleteFileW(temp.c_str());
                    return Fail(ErrorCode::FileWriteFailed, "flush failed");
                }
                ::CloseHandle(h);

                if (::GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    if (!::MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
                    {
                        ::DeleteFileW(temp.c_str());
                        return Fail(ErrorCode::FileWriteFailed, "rename failed");
                    }
                    return Ok();
                }

                if (!::ReplaceFileW(target.c_str(), temp.c_str(), (target + L".bak").c_str(),
                                    REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
                {
                    ::DeleteFileW(temp.c_str());
                    return Fail(ErrorCode::FileWriteFailed, "replace failed");
                }
                return Ok();
            }

            Status Remove(const std::string& path) override
            {
                if (HasWildcard(path))
                    return Fail(ErrorCode::InvalidArgument, "wildcard in the delete path");
                if (!::DeleteFileW(Widen(path).c_str()))
                    return Fail(ErrorCode::FileWriteFailed, "delete failed");
                return Ok();
            }

            Status ListDirectory(const std::string& directory,
                                 std::vector<std::string>& out) override
            {
                out.clear();
                WIN32_FIND_DATAW found = {};
                const std::wstring pattern = Widen(directory) + L"\\*";
                HANDLE h = ::FindFirstFileW(pattern.c_str(), &found);
                if (h == INVALID_HANDLE_VALUE)
                {
                    const DWORD err = ::GetLastError();
                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                        return Ok();
                    return Fail(ErrorCode::InvalidArgument, "directory not readable");
                }

                do
                {
                    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                        continue;
                    out.push_back(Narrow(found.cFileName));
                } while (::FindNextFileW(h, &found));
                ::FindClose(h);
                return Ok();
            }

            Status EnsureDirectory(const std::string& directory) override
            {
                if (::CreateDirectoryW(Widen(directory).c_str(), nullptr))
                    return Ok();
                if (::GetLastError() == ERROR_ALREADY_EXISTS)
                    return Ok();
                return Fail(ErrorCode::FileWriteFailed, "directory not creatable");
            }
        };
    }

    IFileSystem& RealFileSystem()
    {
        static Win32FileSystem fs;
        return fs;
    }

    bool MemoryFileSystem::Exists(const std::string& path) const
    {
        return std::any_of(m_files.begin(), m_files.end(),
                           [&](const File& f) { return f.Path == path; });
    }

    Status MemoryFileSystem::Read(const std::string& path, std::vector<unsigned char>& out)
    {
        for (const File& f : m_files)
        {
            if (f.Path == path)
            {
                out = f.Data;
                return Ok();
            }
        }
        out.clear();
        return Fail(ErrorCode::InvalidArgument, "not found");
    }

    Status MemoryFileSystem::WriteAtomic(const std::string& path,
                                         const unsigned char* data, std::size_t len)
    {
        const std::string temp = path + ".tmp";

        if (m_fault == FaultPoint::AfterCreateTemp)
        {
            Put(temp, {});
            return Fail(ErrorCode::FileWriteFailed, "failure injected: after creating the temp file");
        }

        std::vector<unsigned char> payload(data, data + len);
        if (m_fault == FaultPoint::AfterWrite)
        {
            Put(temp, std::move(payload));
            return Fail(ErrorCode::FileWriteFailed, "failure injected: after the write");
        }
        if (m_fault == FaultPoint::AfterFlush)
        {
            Put(temp, std::move(payload));
            return Fail(ErrorCode::FileWriteFailed, "failure injected: after the flush");
        }
        if (m_fault == FaultPoint::DuringReplace)
        {
            Put(temp, std::move(payload));
            return Fail(ErrorCode::FileWriteFailed, "failure injected: during the replace");
        }

        if (m_fault == FaultPoint::SilentCorruption && !payload.empty())
            payload[0] = static_cast<unsigned char>(payload[0] ^ 0xFF);

        Put(path, std::move(payload));
        m_files.erase(std::remove_if(m_files.begin(), m_files.end(),
                                     [&](const File& f) { return f.Path == temp; }),
                      m_files.end());
        return Ok();
    }

    Status MemoryFileSystem::ListDirectory(const std::string& directory,
                                           std::vector<std::string>& out)
    {
        out.clear();
        const std::string prefix = directory + "\\";
        for (const File& f : m_files)
        {
            if (f.Path.compare(0, prefix.size(), prefix) != 0)
                continue;
            const std::string rest = f.Path.substr(prefix.size());
            if (rest.find_first_of("\\/") != std::string::npos)
                continue;
            out.push_back(rest);
        }
        return Ok();
    }

    Status MemoryFileSystem::EnsureDirectory(const std::string& directory)
    {
        for (const std::string& d : m_directories)
            if (d == directory)
                return Ok();
        m_directories.push_back(directory);
        return Ok();
    }

    Status MemoryFileSystem::Remove(const std::string& path)
    {
        if (path.find('*') != std::string::npos || path.find('?') != std::string::npos)
            return Fail(ErrorCode::InvalidArgument, "wildcard in the delete path");

        const std::size_t before = m_files.size();
        m_files.erase(std::remove_if(m_files.begin(), m_files.end(),
                                     [&](const File& f) { return f.Path == path; }),
                      m_files.end());
        return (m_files.size() != before) ? Ok()
                                          : Fail(ErrorCode::InvalidArgument, "not found");
    }

    void MemoryFileSystem::Put(const std::string& path, std::vector<unsigned char> data)
    {
        for (File& f : m_files)
        {
            if (f.Path == path)
            {
                f.Data = std::move(data);
                return;
            }
        }
        m_files.push_back(File{ path, std::move(data) });
    }

    std::vector<std::string> MemoryFileSystem::Paths() const
    {
        std::vector<std::string> out;
        out.reserve(m_files.size());
        for (const File& f : m_files)
            out.push_back(f.Path);
        std::sort(out.begin(), out.end());
        return out;
    }
}
