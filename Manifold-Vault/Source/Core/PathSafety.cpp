#include "Core/Language.h"
#include "Core/PathSafety.h"

namespace Bunker
{
    namespace
    {
        char Fold(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c - 'A' + 'a');
            if (c == '/')
                return '\\';
            return c;
        }

        std::string Normalise(const std::string& path)
        {
            std::string out;
            out.reserve(path.size());
            for (const char c : path)
            {
                const char f = Fold(c);
                if (f == '\\' && !out.empty() && out.back() == '\\' && out.size() > 1)
                    continue;
                out.push_back(f);
            }
            while (out.size() > 1 && out.back() == '\\')
                out.pop_back();
            return out;
        }

        std::string DirectoryOf(const std::string& path)
        {
            const std::size_t slash = path.find_last_of("\\/");
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }
    }

    bool PathIsInside(const std::string& child, const std::string& parent)
    {
        if (parent.empty() || child.empty())
            return false;

        const std::string c = Normalise(child);
        const std::string p = Normalise(parent);
        if (c.size() < p.size())
            return false;
        if (c.compare(0, p.size(), p) != 0)
            return false;
        return c.size() == p.size() || c[p.size()] == '\\' ||
               (!p.empty() && p.back() == '\\');
    }

    PathVerdict AssessExportPath(const std::string& target, const PathFacts& facts,
                                 const std::vector<CloudRoot>& cloudRoots,
                                 const std::string& vaultPath)
    {
        PathVerdict verdict;
        if (target.empty())
            return verdict;

        for (const CloudRoot& root : cloudRoots)
        {
            if (!root.Path.empty() && PathIsInside(target, root.Path))
            {
                verdict.Risk = PathRisk::CloudFolder;
                verdict.What = root.Name;
                return verdict;
            }
        }

        const std::string normalised = Normalise(target);
        if (facts.IsNetworkDrive || normalised.compare(0, 2, "\\\\") == 0)
        {
            verdict.Risk = PathRisk::NetworkDrive;
            return verdict;
        }

        if (!vaultPath.empty())
        {
            const std::string folder = DirectoryOf(vaultPath);
            if (!folder.empty() && PathIsInside(target, folder))
            {
                verdict.Risk = PathRisk::NextToTheVault;
                return verdict;
            }
        }

        if (facts.IsRemovableDrive)
        {
            verdict.Risk = PathRisk::RemovableDrive;
            return verdict;
        }
        return verdict;
    }

    const char* RiskSentence(PathRisk risk)
    {
        switch (risk)
        {
        case PathRisk::CloudFolder:
            return T(Str::Path_CloudFolder);
        case PathRisk::NetworkDrive:
            return T(Str::Path_NetworkDrive);
        case PathRisk::RemovableDrive:
            return T(Str::Path_RemovableDrive);
        case PathRisk::NextToTheVault:
            return T(Str::Path_NextToTheVault);
        case PathRisk::None:
        default:
            return nullptr;
        }
    }
}
