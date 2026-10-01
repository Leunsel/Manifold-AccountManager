#include "Core/BackupRun.h"

#include "Core/Backup.h"
#include "Core/SettingsPolicy.h"

#include <vector>

namespace Bunker::Vault
{
    namespace
    {
        std::string Join(const std::string& directory, const std::string& name)
        {
            return directory + "\\" + name;
        }
    }

    Status RemoveOwnBackup(IFileSystem& fs, const std::string& vaultPath,
                           const std::string& name)
    {
        BackupFile parsed;
        if (!ParseBackupFileName(vaultPath, name, parsed))
            return Error{ ErrorCode::InvalidArgument, "not a backup name of ours" };

        if (name.find_first_of("\\/") != std::string::npos ||
            name.find("..") != std::string::npos ||
            name.find('*') != std::string::npos ||
            name.find('?') != std::string::npos)
        {
            return Error{ ErrorCode::InvalidArgument, "invalid file name" };
        }

        const std::string full = Join(BackupDirectory(vaultPath), name);
        if (!fs.Exists(full))
            return Error{ ErrorCode::InvalidArgument, "not found" };
        return fs.Remove(full);
    }

    BackupReport RunBackup(IFileSystem& fs, const std::string& vaultPath,
                           const Settings& settings, std::uint64_t liveGeneration,
                           std::uint64_t nowUtc)
    {
        BackupReport report;
        if (!settings.BackupEnabled)
            return report;

        const std::string directory = BackupDirectory(vaultPath);

        std::vector<std::string> names;
        if (const Status st = fs.ListDirectory(directory, names); !st.IsOk())
        {
            report.Problem = st;
            return report;
        }

        std::vector<BackupFile> present;
        for (const std::string& name : names)
        {
            BackupFile parsed;
            if (ParseBackupFileName(vaultPath, name, parsed))
                present.push_back(parsed);
        }

        const BackupPlan plan = PlanBackup(settings, present, vaultPath, liveGeneration,
                                           nowUtc);
        if (!plan.Write)
            return report;

        std::vector<unsigned char> source;
        if (const Status st = fs.Read(vaultPath, source); !st.IsOk())
        {
            report.Problem = st;
            return report;
        }

        if (const Status st = fs.EnsureDirectory(directory); !st.IsOk())
        {
            report.Problem = st;
            return report;
        }

        const std::string full = Join(directory, plan.Name);
        if (const Status st = fs.WriteAtomic(full, source.data(), source.size()); !st.IsOk())
        {
            report.Problem = st;
            return report;
        }

        std::vector<unsigned char> written;
        if (const Status st = fs.Read(full, written); !st.IsOk() || written != source)
        {
            (void)RemoveOwnBackup(fs, vaultPath, plan.Name);
            report.Problem = st.IsOk()
                                 ? Status{ Error{ ErrorCode::FileWriteFailed,
                                                  "backup does not match" } }
                                 : st;
            return report;
        }

        report.Wrote = true;
        report.WrittenName = plan.Name;
        present.push_back(BackupFile{ plan.Name, liveGeneration,
                                      StampFromUtc(nowUtc) });

        const TrimPlan trim = PlanTrim(present, liveGeneration, BackupKeep(settings));
        if (!trim.Remove)
            return report;

        if (const Status st = RemoveOwnBackup(fs, vaultPath, trim.Name); !st.IsOk())
        {
            return report;
        }
        report.Removed = true;
        report.RemovedName = trim.Name;
        return report;
    }
}
