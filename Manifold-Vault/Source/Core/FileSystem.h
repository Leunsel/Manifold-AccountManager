#pragma once

#include "Core/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bunker
{
    enum class FaultPoint
    {
        None,
        AfterCreateTemp,
        AfterWrite,
        AfterFlush,
        DuringReplace,

        SilentCorruption,
    };

    class IFileSystem
    {
    public:
        virtual ~IFileSystem() = default;

        virtual bool Exists(const std::string& path) const = 0;
        virtual Status Read(const std::string& path, std::vector<unsigned char>& out) = 0;

        virtual Status WriteAtomic(const std::string& path,
                                   const unsigned char* data, std::size_t len) = 0;

        virtual Status Remove(const std::string& path) = 0;

        virtual Status ListDirectory(const std::string& directory,
                                     std::vector<std::string>& out) = 0;

        virtual Status EnsureDirectory(const std::string& directory) = 0;

        virtual void SetFaultPoint(FaultPoint) {}
    };

    IFileSystem& RealFileSystem();

    class MemoryFileSystem : public IFileSystem
    {
    public:
        bool Exists(const std::string& path) const override;
        Status Read(const std::string& path, std::vector<unsigned char>& out) override;
        Status WriteAtomic(const std::string& path,
                           const unsigned char* data, std::size_t len) override;
        Status Remove(const std::string& path) override;
        Status ListDirectory(const std::string& directory,
                             std::vector<std::string>& out) override;
        Status EnsureDirectory(const std::string& directory) override;
        void SetFaultPoint(FaultPoint p) override { m_fault = p; }

        std::size_t FileCount() const { return m_files.size(); }
        std::vector<std::string> Paths() const;
        void Put(const std::string& path, std::vector<unsigned char> data);

    private:
        struct File { std::string Path; std::vector<unsigned char> Data; };
        std::vector<File> m_files;
        std::vector<std::string> m_directories;
        FaultPoint m_fault = FaultPoint::None;
    };
}
