#include "TestRunner.h"

#include "Core/Backup.h"
#include "Core/Model.h"
#include "Core/SettingsPolicy.h"

#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    const char kVault[] = "D:\\Vault\\Bunker.vault";

    BackupFile Made(std::uint64_t generation, const char* stamp)
    {
        BackupFile f;
        f.Generation = generation;
        f.Stamp = stamp;
        f.Name = BackupFileName(kVault, generation, 0);
        std::uint64_t seconds = 0;
        CHECK(UtcFromStamp(stamp, seconds));
        f.Name = BackupFileName(kVault, generation, seconds);
        return f;
    }

    Settings Enabled()
    {
        Settings s;
        s.BackupEnabled = true;
        s.BackupKeep = 3;
        s.BackupMinIntervalSeconds = 3600;
        return s;
    }
}

TEST_CASE(Backup_DirectoryIsDerivedFromTheVaultPath)
{
    CHECK(BackupDirectory(kVault) == "D:\\Vault\\Bunker.vault.backups");
    CHECK(BackupDirectory("Bunker.vault") == "Bunker.vault.backups");
}

TEST_CASE(Backup_StampRoundTripsThroughTheCalendar)
{
    CHECK(StampFromUtc(0) == "19700101-000000");
    CHECK(StampFromUtc(86399) == "19700101-235959");
    CHECK(StampFromUtc(86400) == "19700102-000000");
    CHECK(StampFromUtc(951868800ull) == "20000301-000000");
    CHECK(StampFromUtc(1709164800ull) == "20240229-000000");

    for (std::uint64_t t : { 0ull, 1ull, 86399ull, 951868800ull, 1709164800ull,
                             1758224580ull, 4102444800ull })
    {
        std::uint64_t back = 0;
        CHECK_MSG(UtcFromStamp(StampFromUtc(t), back), "our own timestamp was unreadable");
        CHECK_EQ(back, t);
    }
}

TEST_CASE(Backup_StampRejectsNonsense)
{
    std::uint64_t out = 0;
    CHECK(!UtcFromStamp("", out));
    CHECK(!UtcFromStamp("20260918-2043", out));
    CHECK(!UtcFromStamp("20260918_204300", out));
    CHECK(!UtcFromStamp("2026091x-204300", out));
    CHECK(!UtcFromStamp("20261318-204300", out));
    CHECK(!UtcFromStamp("20260918-244300", out));
    CHECK(!UtcFromStamp("19690918-204300", out));
}

TEST_CASE(Backup_OnlyOurOwnNamesAreRecognised)
{
    const std::string good = BackupFileName(kVault, 9, 1758224580ull);
    BackupFile parsed;
    CHECK_MSG(ParseBackupFileName(kVault, good, parsed),
              "our own name was not recognised");
    CHECK_EQ(parsed.Generation, std::uint64_t{ 9 });
    CHECK(parsed.Name == good);

    const char* strangers[] = {
        "Bunker.vault",
        "Bunker.vault.bak",
        "Bunker.vault.tmp",
        "Bunker-g0000000009-20260918-204300.vault.tmp",
        "Bunker-g0000000009-20260918-204300.txt",
        "Bunker-g0000000009-20260918-204300.vaulx",
        "Bunker-g0000000009-20260918-204300vvault",
        "Bunker-g000000009-20260918-204300.vault",
        "Bunker-g0000000009-20260918.vault",
        "Bunker-x0000000009-20260918-204300.vault",
        "Other-g0000000009-20260918-204300.vault",
        "Bunker-g00000000o9-20260918-204300.vault",
        "*.vault",
        "",
    };
    for (const char* name : strangers)
    {
        BackupFile ignored;
        CHECK_MSG(!ParseBackupFileName(kVault, name, ignored), name);
    }
}

TEST_CASE(Backup_ANameFromAnotherVaultIsNeverOurs)
{
    const std::string mine = BackupFileName("D:\\a\\Bunker.vault", 1, 0);
    BackupFile out;
    CHECK(!ParseBackupFileName("D:\\a\\Bunker2.vault", mine, out));
    CHECK(!ParseBackupFileName("D:\\a\\Bunk.vault", mine, out));
    CHECK(ParseBackupFileName("D:\\otherwo\\Bunker.vault", mine, out));
}

TEST_CASE(Backup_FirstCopyIsDueImmediately)
{
    const BackupPlan plan = PlanBackup(Enabled(), {}, kVault, 1, 1758224580ull);
    CHECK(plan.Write);
    CHECK(plan.Name == BackupFileName(kVault, 1, 1758224580ull));
}

TEST_CASE(Backup_NotMoreOftenThanTheInterval)
{
    const std::uint64_t t0 = 1758224580ull;
    std::vector<BackupFile> present{ Made(1, StampFromUtc(t0).c_str()) };

    CHECK(!PlanBackup(Enabled(), present, kVault, 2, t0 + 1800).Write);
    CHECK(PlanBackup(Enabled(), present, kVault, 2, t0 + 3601).Write);
}

TEST_CASE(Backup_NoSecondCopyOfTheSameState)
{
    std::vector<BackupFile> present{ Made(7, "20260918-204300") };
    CHECK_MSG(!PlanBackup(Enabled(), present, kVault, 7, 4102444800ull).Write,
              "the same state was copied a second time");
}

TEST_CASE(Backup_DisabledMeansNothingHappens)
{
    Settings off = Enabled();
    off.BackupEnabled = false;
    CHECK(!PlanBackup(off, {}, kVault, 1, 1758224580ull).Write);
}

TEST_CASE(Backup_TrimRemovesTheOldestAndOnlyOne)
{
    std::vector<BackupFile> present{
        Made(5, "20260918-100000"),
        Made(7, "20260918-120000"),
        Made(6, "20260918-110000"),
        Made(8, "20260918-130000"),
    };

    const TrimPlan plan = PlanTrim(present, 8, 3);
    CHECK(plan.Remove);
    CHECK_MSG(plan.Name == present[0].Name, "the oldest copy was not chosen");
}

TEST_CASE(Backup_TrimDoesNothingWhileThereIsRoom)
{
    std::vector<BackupFile> present{
        Made(5, "20260918-100000"),
        Made(6, "20260918-110000"),
        Made(7, "20260918-120000"),
    };
    CHECK_MSG(!PlanTrim(present, 7, 3).Remove, "something was deleted although there was room");
    CHECK(!PlanTrim({}, 1, 3).Remove);
    CHECK(!PlanTrim({ Made(1, "20260918-100000") }, 1, 0).Remove);
}

TEST_CASE(Backup_TrimNeverTakesTheOnlyOrTheNewest)
{
    std::vector<BackupFile> single{ Made(4, "20260918-100000") };
    CHECK_MSG(!PlanTrim(single, 4, 0).Remove, "the only copy was deleted");

    std::vector<BackupFile> two{ Made(4, "20260918-100000"), Made(5, "20260918-110000") };
    const TrimPlan plan = PlanTrim(two, 5, 0);
    CHECK(plan.Remove);
    CHECK_MSG(plan.Name == two[0].Name, "the newest copy was chosen for deletion");
}

TEST_CASE(Backup_TrimStopsWhenTheHistoryIsConfusing)
{
    std::vector<BackupFile> present{
        Made(5, "20260918-100000"),
        Made(6, "20260918-110000"),
        Made(9, "20260918-120000"),
        Made(10, "20260918-130000"),
    };
    CHECK_MSG(!PlanTrim(present, 6, 1).Remove,
              "deleted despite a contradictory history");

    std::vector<BackupFile> fine{
        Made(5, "20260918-100000"),
        Made(6, "20260918-110000"),
        Made(7, "20260918-120000"),
    };
    CHECK(PlanTrim(fine, 7, 1).Remove);
}

TEST_CASE(Backup_HostileSettingsAreClampedNotObeyed)
{
    Settings hostile = Enabled();
    hostile.BackupKeep = 0;
    hostile.BackupMinIntervalSeconds = 0;
    CHECK_EQ(BackupKeep(hostile), kMinBackupKeep);
    CHECK_EQ(BackupIntervalSeconds(hostile), kMinBackupIntervalSeconds);

    const std::uint64_t t0 = 1758224580ull;
    std::vector<BackupFile> present{ Made(1, StampFromUtc(t0).c_str()) };
    CHECK_MSG(!PlanBackup(hostile, present, kVault, 2, t0 + 10).Write,
              "an interval of 0 was taken literally");
}
