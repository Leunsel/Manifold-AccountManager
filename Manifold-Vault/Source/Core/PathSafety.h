#pragma once

#include <string>
#include <vector>

namespace Bunker
{
    enum class PathRisk
    {
        None = 0,

        CloudFolder,

        NetworkDrive,

        RemovableDrive,

        NextToTheVault,
    };

    struct CloudRoot
    {
        std::string Name;
        std::string Path;
    };

    struct PathFacts
    {
        bool IsNetworkDrive = false;
        bool IsRemovableDrive = false;
    };

    struct PathVerdict
    {
        PathRisk Risk = PathRisk::None;
        std::string What;
    };

    bool PathIsInside(const std::string& child, const std::string& parent);

    PathVerdict AssessExportPath(const std::string& target, const PathFacts& facts,
                                 const std::vector<CloudRoot>& cloudRoots,
                                 const std::string& vaultPath);

    const char* RiskSentence(PathRisk risk);
}
