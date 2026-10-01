#include "Core/Backup.h"

#include "Core/SettingsPolicy.h"

#include <cstdio>

namespace Bunker::Vault
{
    namespace
    {
        constexpr char kExtension[] = ".vault";
        constexpr std::size_t kGenerationDigits = 10;

        std::size_t FileNameStart(const std::string& path)
        {
            const std::size_t slash = path.find_last_of("\\/");
            return slash == std::string::npos ? 0 : slash + 1;
        }

        std::string StemOf(const std::string& vaultPath)
        {
            const std::string name = vaultPath.substr(FileNameStart(vaultPath));
            const std::size_t dot = name.find_last_of('.');
            return dot == std::string::npos ? name : name.substr(0, dot);
        }

        bool AllDigits(const std::string& s, std::size_t from, std::size_t count)
        {
            if (from + count > s.size())
                return false;
            for (std::size_t i = 0; i < count; ++i)
                if (s[from + i] < '0' || s[from + i] > '9')
                    return false;
            return true;
        }

        std::uint64_t DigitsToU64(const std::string& s, std::size_t from, std::size_t count)
        {
            std::uint64_t value = 0;
            for (std::size_t i = 0; i < count; ++i)
                value = value * 10 + static_cast<std::uint64_t>(s[from + i] - '0');
            return value;
        }

        void CivilFromDays(std::int64_t z, int& year, unsigned& month, unsigned& day)
        {
            z += 719468;
            const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
            const std::uint64_t doe = static_cast<std::uint64_t>(z - era * 146097);
            const std::uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
            const std::uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const std::uint64_t mp = (5 * doy + 2) / 153;
            day = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
            month = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
            year = static_cast<int>(y + (month <= 2 ? 1 : 0));
        }

        std::int64_t DaysFromCivil(int year, unsigned month, unsigned day)
        {
            std::int64_t y = year;
            y -= month <= 2;
            const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
            const std::uint64_t yoe = static_cast<std::uint64_t>(y - era * 400);
            const std::uint64_t doy =
                (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
            const std::uint64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
        }

        const BackupFile* Newest(const std::vector<BackupFile>& present)
        {
            const BackupFile* best = nullptr;
            for (const BackupFile& f : present)
            {
                if (!best || f.Generation > best->Generation ||
                    (f.Generation == best->Generation && f.Stamp > best->Stamp))
                {
                    best = &f;
                }
            }
            return best;
        }

        const BackupFile* Oldest(const std::vector<BackupFile>& present)
        {
            const BackupFile* best = nullptr;
            for (const BackupFile& f : present)
            {
                if (!best || f.Generation < best->Generation ||
                    (f.Generation == best->Generation && f.Stamp < best->Stamp))
                {
                    best = &f;
                }
            }
            return best;
        }
    }

    std::string BackupDirectory(const std::string& vaultPath)
    {
        return vaultPath + ".backups";
    }

    std::string StampFromUtc(std::uint64_t secondsSinceEpoch)
    {
        const std::int64_t total = static_cast<std::int64_t>(secondsSinceEpoch);
        std::int64_t days = total / 86400;
        std::int64_t rest = total % 86400;
        if (rest < 0)
        {
            rest += 86400;
            --days;
        }

        int year = 0;
        unsigned month = 0;
        unsigned day = 0;
        CivilFromDays(days, year, month, day);

        char out[kStampChars + 1];
        std::snprintf(out, sizeof(out), "%04d%02u%02u-%02d%02d%02d", year, month, day,
                      static_cast<int>(rest / 3600), static_cast<int>((rest / 60) % 60),
                      static_cast<int>(rest % 60));
        return std::string(out);
    }

    bool UtcFromStamp(const std::string& stamp, std::uint64_t& out)
    {
        out = 0;
        if (stamp.size() != kStampChars || stamp[8] != '-')
            return false;
        if (!AllDigits(stamp, 0, 8) || !AllDigits(stamp, 9, 6))
            return false;

        const int year = static_cast<int>(DigitsToU64(stamp, 0, 4));
        const unsigned month = static_cast<unsigned>(DigitsToU64(stamp, 4, 2));
        const unsigned day = static_cast<unsigned>(DigitsToU64(stamp, 6, 2));
        const std::uint64_t hour = DigitsToU64(stamp, 9, 2);
        const std::uint64_t minute = DigitsToU64(stamp, 11, 2);
        const std::uint64_t second = DigitsToU64(stamp, 13, 2);

        if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
            hour > 23 || minute > 59 || second > 59)
        {
            return false;
        }

        const std::int64_t days = DaysFromCivil(year, month, day);
        if (days < 0)
            return false;
        out = static_cast<std::uint64_t>(days) * 86400 + hour * 3600 + minute * 60 + second;
        return true;
    }

    std::string BackupFileName(const std::string& vaultPath, std::uint64_t generation,
                               std::uint64_t whenUtc)
    {
        char digits[kGenerationDigits + 1];
        std::snprintf(digits, sizeof(digits), "%010llu",
                      static_cast<unsigned long long>(generation));
        return StemOf(vaultPath) + "-g" + digits + "-" + StampFromUtc(whenUtc) + kExtension;
    }

    bool ParseBackupFileName(const std::string& vaultPath, const std::string& name,
                             BackupFile& out)
    {
        out = BackupFile{};

        const std::string stem = StemOf(vaultPath);
        const std::size_t extLen = sizeof(kExtension) - 1;
        const std::size_t expected = stem.size() + 2 + kGenerationDigits + 1 +
                                     kStampChars + extLen;
        if (name.size() != expected)
            return false;
        if (name.compare(0, stem.size(), stem) != 0)
            return false;
        if (name.compare(stem.size(), 2, "-g") != 0)
            return false;

        const std::size_t genAt = stem.size() + 2;
        if (!AllDigits(name, genAt, kGenerationDigits))
            return false;
        const std::size_t dashAt = genAt + kGenerationDigits;
        if (name[dashAt] != '-')
            return false;

        const std::string stamp = name.substr(dashAt + 1, kStampChars);
        std::uint64_t ignored = 0;
        if (!UtcFromStamp(stamp, ignored))
            return false;
        if (name.compare(dashAt + 1 + kStampChars, extLen, kExtension) != 0)
            return false;

        out.Name = name;
        out.Generation = DigitsToU64(name, genAt, kGenerationDigits);
        out.Stamp = stamp;
        return true;
    }

    BackupPlan PlanBackup(const Settings& settings, const std::vector<BackupFile>& present,
                          const std::string& vaultPath, std::uint64_t liveGeneration,
                          std::uint64_t nowUtc)
    {
        BackupPlan plan;
        if (!settings.BackupEnabled)
            return plan;

        for (const BackupFile& f : present)
            if (f.Generation == liveGeneration)
                return plan;

        if (const BackupFile* newest = Newest(present))
        {
            std::uint64_t last = 0;
            if (UtcFromStamp(newest->Stamp, last) && nowUtc >= last)
            {
                if (nowUtc - last < BackupIntervalSeconds(settings))
                    return plan;
            }
        }

        plan.Write = true;
        plan.Name = BackupFileName(vaultPath, liveGeneration, nowUtc);
        return plan;
    }

    TrimPlan PlanTrim(const std::vector<BackupFile>& present, std::uint64_t liveGeneration,
                      std::uint32_t keep)
    {
        TrimPlan plan;
        if (present.size() <= keep || present.size() <= 1)
            return plan;

        for (const BackupFile& f : present)
            if (f.Generation > liveGeneration)
                return plan;

        const BackupFile* oldest = Oldest(present);
        const BackupFile* newest = Newest(present);
        if (!oldest || !newest || oldest->Name == newest->Name)
            return plan;

        plan.Remove = true;
        plan.Name = oldest->Name;
        return plan;
    }
}
