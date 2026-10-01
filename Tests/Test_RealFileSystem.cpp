#include "TestRunner.h"

#include "Core/Crypto.h"
#include "Core/FileSystem.h"
#include "Core/VaultFile.h"
#include "Core/VaultFormat.h"

#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    void Ready()
    {
        static bool done = false;
        if (!done) { CHECK(Crypto::Initialise().IsOk()); done = true; }
    }

    std::string TempPath(const char* leaf)
    {
        wchar_t dir[MAX_PATH] = {};
        const DWORD n = ::GetTempPathW(MAX_PATH, dir);
        CHECK(n > 0 && n < MAX_PATH);

        char narrow[MAX_PATH * 2] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, dir, -1, narrow, sizeof(narrow), nullptr, nullptr);
        return std::string(narrow) + "BunkerTest_" + leaf;
    }

    struct Cleanup
    {
        std::string Base;
        explicit Cleanup(std::string base) : Base(std::move(base)) {}
        ~Cleanup()
        {
            for (const char* suffix : { "", ".tmp", ".bak" })
            {
                wchar_t wide[MAX_PATH * 2] = {};
                const std::string p = Base + suffix;
                ::MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wide, MAX_PATH * 2);
                ::DeleteFileW(wide);
            }
        }
    };

    const char kPw[] = "password for the real disk";
}

TEST_CASE(RealFs_VaultRoundTripOnDisk)
{
    Ready();
    const std::string path = TempPath("roundtrip.vault");
    Cleanup cleanup(path);
    IFileSystem& fs = RealFileSystem();

    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, kPw, std::strlen(kPw),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        Entry e;
        e.Id = 1;
        e.Title = "On disk, with Ümlauts";
        e.PlatformId = "steam";
        v.Doc().Entries.push_back(e);
        CHECK(v.Save(fs, path).IsOk());
    }

    CHECK(fs.Exists(path));

    VaultFile back;
    CHECK(VaultFile::Unlock(fs, path, kPw, std::strlen(kPw), back).IsOk());
    CHECK_EQ(back.Doc().Entries.size(), std::size_t{ 1 });
    CHECK(back.Doc().Entries[0].Title == "On disk, with Ümlauts");

    std::vector<unsigned char> raw;
    CHECK(fs.Read(path, raw).IsOk());
    CHECK_BYTES_EQ(raw.data(), kMagic, sizeof(kMagic));
}

TEST_CASE(RealFs_ReplaceLeavesNoTempFileBehind)
{
    Ready();
    const std::string path = TempPath("replace.vault");
    Cleanup cleanup(path);
    IFileSystem& fs = RealFileSystem();

    VaultFile v;
    CHECK(VaultFile::Create(fs, path, kPw, std::strlen(kPw),
                            Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
    CHECK(v.Save(fs, path).IsOk());
    CHECK(v.Save(fs, path).IsOk());

    CHECK_MSG(!fs.Exists(path + ".tmp"), "a .tmp file was left behind");
    CHECK(fs.Exists(path));
}

TEST_CASE(RealFs_OverwritingAnExistingVaultKeepsItReadable)
{
    Ready();
    const std::string path = TempPath("overwrite.vault");
    Cleanup cleanup(path);
    IFileSystem& fs = RealFileSystem();

    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, kPw, std::strlen(kPw),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        Entry e; e.Id = 1; e.Title = "erste Fassung";
        v.Doc().Entries.push_back(e);
        CHECK(v.Save(fs, path).IsOk());
    }
    {
        VaultFile v;
        CHECK(VaultFile::Unlock(fs, path, kPw, std::strlen(kPw), v).IsOk());
        v.Doc().Entries[0].Title = "zweite Fassung";
        CHECK(v.Save(fs, path).IsOk());
    }

    VaultFile back;
    CHECK(VaultFile::Unlock(fs, path, kPw, std::strlen(kPw), back).IsOk());
    CHECK(back.Doc().Entries[0].Title == "zweite Fassung");
}

TEST_CASE(RealFs_MissingFileFailsCleanly)
{
    Ready();
    IFileSystem& fs = RealFileSystem();
    const std::string path = TempPath("gibtesnicht.vault");

    CHECK(!fs.Exists(path));
    std::vector<unsigned char> raw;
    CHECK(!fs.Read(path, raw).IsOk());

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, path, kPw, std::strlen(kPw), out);
    CHECK(!st.IsOk());
    CHECK(!out.IsUnlocked());
}
