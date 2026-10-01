#include "TestRunner.h"

#include "Core/Backup.h"
#include "Core/BackupRun.h"
#include "Core/FileSystem.h"
#include "Core/Model.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    const char kVault[] = "D:\\Vault\\Bunker.vault";
    const std::uint64_t kNoon = 1758224580ull;

    Settings Enabled()
    {
        Settings s;
        s.BackupEnabled = true;
        s.BackupKeep = 3;
        s.BackupMinIntervalSeconds = 3600;
        return s;
    }

    std::vector<unsigned char> Bytes(unsigned char fill, std::size_t n = 64)
    {
        return std::vector<unsigned char>(n, fill);
    }

    std::vector<std::string> Copies(MemoryFileSystem& fs)
    {
        std::vector<std::string> all;
        fs.ListDirectory(BackupDirectory(kVault), all);
        std::vector<std::string> names;
        for (const std::string& n : all)
        {
            BackupFile parsed;
            if (ParseBackupFileName(kVault, n, parsed))
                names.push_back(n);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    void Seed(MemoryFileSystem& fs, std::uint64_t from, std::uint64_t to)
    {
        for (std::uint64_t g = from; g <= to; ++g)
        {
            fs.Put(BackupDirectory(kVault) + "\\" +
                       BackupFileName(kVault, g, kNoon + (g - 1) * 3601),
                   Bytes(static_cast<unsigned char>(g)));
        }
    }
}

TEST_CASE(BackupRun_WritesTheFirstCopyAndVerifiesIt)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(0xAB));

    const BackupReport report = RunBackup(fs, kVault, Enabled(), 1, kNoon);
    CHECK(report.Problem.IsOk());
    CHECK_MSG(report.Wrote, "the first backup was not written");
    CHECK(!report.Removed);

    const std::vector<std::string> names = Copies(fs);
    CHECK_EQ(names.size(), std::size_t{ 1 });
    if (names.empty())
        return;
    CHECK(names[0] == BackupFileName(kVault, 1, kNoon));

    std::vector<unsigned char> copy;
    CHECK(fs.Read(BackupDirectory(kVault) + "\\" + names[0], copy).IsOk());
    CHECK_MSG(copy == Bytes(0xAB), "the copy is not identical to the vault");
}

TEST_CASE(BackupRun_DoesNothingWhileTheIntervalHasNotPassed)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(1));
    CHECK(RunBackup(fs, kVault, Enabled(), 1, kNoon).Wrote);

    fs.Put(kVault, Bytes(2));
    CHECK(!RunBackup(fs, kVault, Enabled(), 2, kNoon + 60).Wrote);
    CHECK_EQ(Copies(fs).size(), std::size_t{ 1 });

    CHECK(RunBackup(fs, kVault, Enabled(), 2, kNoon + 3601).Wrote);
    CHECK_EQ(Copies(fs).size(), std::size_t{ 2 });
}

TEST_CASE(BackupRun_TrimsAtMostOneFilePerSave)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(9));
    Seed(fs, 1, 6);
    CHECK_EQ(Copies(fs).size(), std::size_t{ 6 });

    const BackupReport report = RunBackup(fs, kVault, Enabled(), 7, kNoon + 6 * 3601);
    CHECK(report.Wrote);
    CHECK(report.Removed);
    CHECK_MSG(Copies(fs).size() == 6,
              "more than one copy was removed in a single run");
    CHECK_MSG(report.RemovedName == BackupFileName(kVault, 1, kNoon),
              "the oldest copy was not removed");
}

TEST_CASE(BackupRun_KeepsTheConfiguredNumberOverTime)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(9));
    for (std::uint64_t g = 1; g <= 8; ++g)
        CHECK(RunBackup(fs, kVault, Enabled(), g, kNoon + (g - 1) * 3601).Wrote);
    CHECK_EQ(Copies(fs).size(), std::size_t{ 3 });

    const std::vector<std::string> left = Copies(fs);
    CHECK(left.back() == BackupFileName(kVault, 8, kNoon + 7 * 3601));
}

TEST_CASE(BackupRun_NeverDeletesWithoutHavingWrittenFirst)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(9));
    Seed(fs, 1, 5);

    const BackupReport report = RunBackup(fs, kVault, Enabled(), 6,
                                          kNoon + 4 * 3601 + 10);
    CHECK(!report.Wrote);
    CHECK_MSG(!report.Removed, "something was deleted although nothing was written");
    CHECK_EQ(Copies(fs).size(), std::size_t{ 5 });
}

TEST_CASE(BackupRun_LeavesStrangersAlone)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(3));
    const std::string dir = BackupDirectory(kVault);
    fs.Put(dir + "\\important.txt", Bytes(0x11));
    fs.Put(dir + "\\Bunker.vault.bak", Bytes(0x22));
    fs.Put(dir + "\\Other-g0000000001-20260918-100000.vault", Bytes(0x33));

    for (std::uint64_t g = 1; g <= 6; ++g)
        RunBackup(fs, kVault, Enabled(), g, kNoon + (g - 1) * 3601);

    CHECK_MSG(fs.Exists(dir + "\\important.txt"), "a foreign file was deleted");
    CHECK_MSG(fs.Exists(dir + "\\Bunker.vault.bak"), "a .bak was deleted");
    CHECK_MSG(fs.Exists(dir + "\\Other-g0000000001-20260918-100000.vault"),
              "the backup of ANOTHER vault was deleted");
}

TEST_CASE(BackupRun_RemovesACopyThatDidNotVerify)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(0x7E));
    fs.SetFaultPoint(FaultPoint::SilentCorruption);

    const BackupReport report = RunBackup(fs, kVault, Enabled(), 1, kNoon);
    CHECK_MSG(!report.Wrote, "a mismatching copy counted as written");
    CHECK_MSG(!report.Problem.IsOk(), "a failed copy was reported as success");
    CHECK_MSG(Copies(fs).empty(), "an unusable copy was left behind");

    fs.SetFaultPoint(FaultPoint::None);
}

TEST_CASE(BackupRun_AFailedWriteLeavesNoUsableCopy)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(0x7E));
    fs.SetFaultPoint(FaultPoint::AfterCreateTemp);

    const BackupReport report = RunBackup(fs, kVault, Enabled(), 1, kNoon);
    CHECK(!report.Wrote);
    CHECK(!report.Problem.IsOk());
    CHECK_MSG(Copies(fs).empty(), "a failure left a seemingly valid copy");

    fs.SetFaultPoint(FaultPoint::None);
}

TEST_CASE(BackupRun_ABrokenBackupDoesNotBreakAnything)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(0x5C));
    fs.SetFaultPoint(FaultPoint::DuringReplace);

    const BackupReport report = RunBackup(fs, kVault, Enabled(), 1, kNoon);
    CHECK(!report.Wrote);

    std::vector<unsigned char> vault;
    CHECK(fs.Read(kVault, vault).IsOk());
    CHECK_MSG(vault == Bytes(0x5C), "the backup changed the vault");

    fs.SetFaultPoint(FaultPoint::None);
}

TEST_CASE(BackupRun_DisabledTouchesNothing)
{
    MemoryFileSystem fs;
    fs.Put(kVault, Bytes(4));
    Settings off = Enabled();
    off.BackupEnabled = false;

    const BackupReport report = RunBackup(fs, kVault, off, 1, kNoon);
    CHECK(!report.Wrote);
    CHECK(!report.Removed);
    CHECK(report.Problem.IsOk());
    CHECK(Copies(fs).empty());
}

TEST_CASE(FileSystem_RemoveRefusesAWildcard)
{
    MemoryFileSystem fs;
    fs.Put("D:\\x\\Bunker.vault", Bytes(1));
    fs.Put("D:\\x\\Bunker.vault.bak", Bytes(2));
    fs.Put("D:\\x\\Bunker.vault*", Bytes(3));
    fs.Put("D:\\x\\Bunker.vault?", Bytes(4));

    CHECK_MSG(!fs.Remove("D:\\x\\Bunker.vault*").IsOk(),
              "a delete with a wildcard was accepted");
    CHECK_MSG(!fs.Remove("D:\\x\\Bunker.vault?").IsOk(),
              "a delete with a question mark was accepted");
    CHECK(fs.Exists("D:\\x\\Bunker.vault*"));
    CHECK(fs.Exists("D:\\x\\Bunker.vault"));
    CHECK(fs.Exists("D:\\x\\Bunker.vault.bak"));

    CHECK(fs.Remove("D:\\x\\Bunker.vault").IsOk());
}

TEST_CASE(BackupRun_TheDeleteGateRefusesEverythingButItsOwn)
{
    MemoryFileSystem fs;
    const std::string dir = BackupDirectory(kVault);
    const std::string mine = BackupFileName(kVault, 3, kNoon);

    fs.Put(dir + "\\" + mine, Bytes(1));
    fs.Put(dir + "\\important.txt", Bytes(2));
    fs.Put(kVault, Bytes(3));

    const char* refused[] = {
        "important.txt",
        "Bunker.vault",
        "Bunker.vault.bak",
        "..\\Bunker.vault",
        "sub\\Bunker-g0000000003-20260918-204300.vault",
        "Bunker-g000000000*-20260918-204300.vault",
        "Other-g0000000003-20260918-204300.vault",
        "",
    };
    for (const char* name : refused)
        CHECK_MSG(!RemoveOwnBackup(fs, kVault, name).IsOk(), name);

    CHECK_MSG(fs.Exists(dir + "\\important.txt"), "a foreign file was deleted after all");
    CHECK_MSG(fs.Exists(kVault), "the vault itself was deleted");

    CHECK(!RemoveOwnBackup(fs, kVault, BackupFileName(kVault, 99, kNoon)).IsOk());

    CHECK_MSG(RemoveOwnBackup(fs, kVault, mine).IsOk(),
              "our own copy could not be removed");
    CHECK(!fs.Exists(dir + "\\" + mine));
}
