#pragma once

#include "Core/Model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Bunker::Vault
{
    inline constexpr std::size_t kStampChars = 15;

    struct BackupFile
    {
        std::string Name;
        std::uint64_t Generation = 0;
        std::string Stamp;
    };

    std::string BackupDirectory(const std::string& vaultPath);

    std::string BackupFileName(const std::string& vaultPath, std::uint64_t generation,
                               std::uint64_t whenUtc);

    bool ParseBackupFileName(const std::string& vaultPath, const std::string& name,
                             BackupFile& out);

    std::string StampFromUtc(std::uint64_t secondsSinceEpoch);
    bool UtcFromStamp(const std::string& stamp, std::uint64_t& out);

    struct BackupPlan
    {
        bool Write = false;
        std::string Name;
    };

    BackupPlan PlanBackup(const Settings& settings, const std::vector<BackupFile>& present,
                          const std::string& vaultPath, std::uint64_t liveGeneration,
                          std::uint64_t nowUtc);

    struct TrimPlan
    {
        bool Remove = false;
        std::string Name;
    };

    TrimPlan PlanTrim(const std::vector<BackupFile>& present, std::uint64_t liveGeneration,
                      std::uint32_t keep);
}
