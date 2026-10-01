#pragma once

#include "Core/FileSystem.h"
#include "Core/Model.h"
#include "Core/Result.h"

#include <cstdint>
#include <string>

namespace Bunker::Vault
{
    struct BackupReport
    {
        bool Wrote = false;
        std::string WrittenName;

        bool Removed = false;
        std::string RemovedName;

        Status Problem;
    };

    Status RemoveOwnBackup(IFileSystem& fs, const std::string& vaultPath,
                           const std::string& name);

    BackupReport RunBackup(IFileSystem& fs, const std::string& vaultPath,
                           const Settings& settings, std::uint64_t liveGeneration,
                           std::uint64_t nowUtc);
}
