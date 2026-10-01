#pragma once

#include "Core/Model.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Bunker::Vault
{
    std::string FoldFolderName(const std::string& name);

    bool SameFolder(const std::string& a, const std::string& b);

    std::vector<std::string> FolderNames(const Document& doc);

    struct FolderTally
    {
        std::string Name;
        std::string Folded;
        std::size_t Count = 0;
    };

    struct FolderSurvey
    {
        std::vector<FolderTally> Folders;
        std::size_t Total = 0;
        std::size_t Unfiled = 0;
    };

    FolderSurvey SurveyFolders(const Document& doc);

    std::size_t CountEntriesInFolder(const Document& doc, const std::string& name);

    std::string ExistingSpelling(const Document& doc, const std::string& name);

    std::vector<std::string> FolderMonograms(const std::vector<std::string>& names);

    std::vector<std::size_t> MatchFolders(const FolderSurvey& survey, std::string_view query);
}
