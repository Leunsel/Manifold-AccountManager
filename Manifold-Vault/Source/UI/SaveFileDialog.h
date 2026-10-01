#pragma once

#include "Core/PathSafety.h"
#include "Core/Result.h"
#include "Core/Secret.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Bunker::UI
{
    bool AskForExportPath(void* parentWindow, const std::string& suggestedName,
                          std::string& chosenPath);

    bool AskForImportPath(void* parentWindow, std::string& chosenPath);

    inline constexpr std::size_t kMaxImportBytes = 16u * 1024 * 1024;
    Status ReadIntoSecret(const std::string& path, SecureBuffer& out,
                          std::size_t& outLength);

    PathFacts FactsFor(const std::string& path);

    std::vector<CloudRoot> KnownCloudRoots();

    std::string SuggestedExportName(std::uint64_t utcSeconds);
}
