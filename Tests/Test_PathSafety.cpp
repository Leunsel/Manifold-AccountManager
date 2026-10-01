#include "TestRunner.h"

#include "Core/PathSafety.h"

#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    std::vector<CloudRoot> Clouds()
    {
        return {
            CloudRoot{ "OneDrive", "C:\\Users\\testuser\\OneDrive" },
            CloudRoot{ "Dropbox", "C:\\Users\\testuser\\Dropbox" },
        };
    }

    const char kVault[] = "D:\\Vault\\Bunker.vault";

    PathRisk Risk(const std::string& target, PathFacts facts = {})
    {
        return AssessExportPath(target, facts, Clouds(), kVault).Risk;
    }
}

TEST_CASE(Path_InsideComparesWholeComponents)
{
    CHECK(PathIsInside("C:\\a\\b\\c.csv", "C:\\a"));
    CHECK(PathIsInside("C:\\a", "C:\\a"));
    CHECK_MSG(!PathIsInside("C:\\abc\\d.csv", "C:\\a"),
              "a plain prefix comparison matched");
    CHECK(!PathIsInside("C:\\a", "C:\\a\\b"));
    CHECK(!PathIsInside("", "C:\\a"));
    CHECK(!PathIsInside("C:\\a", ""));
}

TEST_CASE(Path_InsideIgnoresCaseAndSeparatorStyle)
{
    CHECK(PathIsInside("C:/Users/TESTUSER/OneDrive/x.csv", "c:\\users\\testuser\\onedrive"));
    CHECK(PathIsInside("C:\\a\\\\b\\c", "C:\\a\\b"));
    CHECK(PathIsInside("C:\\a\\b\\", "C:\\a\\b"));
}

TEST_CASE(Path_ASyncedFolderIsTheWorstAnswerAndWinsOverTheRest)
{
    const PathVerdict verdict =
        AssessExportPath("C:\\Users\\testuser\\OneDrive\\plaintext.csv",
                         PathFacts{ true, true }, Clouds(), kVault);
    CHECK_MSG(verdict.Risk == PathRisk::CloudFolder,
              "the cloud folder was hidden by another warning");
    CHECK(verdict.What == "OneDrive");
}

TEST_CASE(Path_NetworkIsRecognisedFromTheNameAlone)
{
    CHECK(Risk("\\\\server\\freigabe\\x.csv") == PathRisk::NetworkDrive);
    CHECK(Risk("//server/freigabe/x.csv") == PathRisk::NetworkDrive);

    PathFacts mapped;
    mapped.IsNetworkDrive = true;
    CHECK(Risk("Z:\\x.csv", mapped) == PathRisk::NetworkDrive);
}

TEST_CASE(Path_NextToTheVaultIsItsOwnWarning)
{
    CHECK_MSG(Risk("D:\\Vault\\plaintext.csv") == PathRisk::NextToTheVault,
              "a plain-text file next to the vault went unflagged");
    CHECK(Risk("D:\\Vault\\subfolder\\plaintext.csv") == PathRisk::NextToTheVault);
    CHECK(Risk("D:\\Woother\\plaintext.csv") == PathRisk::None);
}

TEST_CASE(Path_ARemovableDriveIsMentionedButNotScolded)
{
    PathFacts stick;
    stick.IsRemovableDrive = true;
    CHECK(Risk("E:\\plaintext.csv", stick) == PathRisk::RemovableDrive);
    CHECK(RiskSentence(PathRisk::RemovableDrive) != nullptr);
}

TEST_CASE(Path_AnOrdinaryFolderSaysNothing)
{
    CHECK(Risk("C:\\Users\\testuser\\Desktop\\plaintext.csv") == PathRisk::None);
    CHECK_MSG(RiskSentence(PathRisk::None) == nullptr,
              "a harmless folder gets a sentence -- the dialog would always talk");
    CHECK(Risk("") == PathRisk::None);
}

TEST_CASE(Path_EveryRiskHasSomethingToSay)
{
    for (const PathRisk risk : { PathRisk::CloudFolder, PathRisk::NetworkDrive,
                                 PathRisk::RemovableDrive, PathRisk::NextToTheVault })
    {
        CHECK(RiskSentence(risk) != nullptr);
    }
}
