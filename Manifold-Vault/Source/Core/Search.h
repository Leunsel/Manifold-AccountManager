#pragma once

#include "Core/Model.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Bunker::Vault
{
    enum class MatchKind : std::uint8_t
    {
        None = 0,
        Title,
        Platform,
        Folder,
        FieldName,
        FieldValue,
        ListLabel,
    };

    struct SearchHit
    {
        std::uint64_t EntryId = 0;
        int Score = 0;
        MatchKind Where = MatchKind::None;
    };

    std::string FoldForSearch(std::string_view text);

    bool FoldedContains(std::string_view text, std::string_view foldedNeedle) noexcept;
    bool FoldedEquals(std::string_view a, std::string_view b) noexcept;

    bool FindFoldedMatch(std::string_view text, std::string_view foldedNeedle,
                         std::size_t& begin, std::size_t& end) noexcept;

    std::uint64_t FindLikelyDuplicate(const Document& doc, std::uint64_t except,
                                      std::string_view platformId, std::string_view title,
                                      std::string_view login);

    enum class SortOrder : std::uint32_t
    {
        Title = 0,
        Modified = 1,
    };

    SortOrder SortOrderFrom(std::uint32_t stored);

    std::vector<SearchHit> Search(const Document& doc, const std::string& query,
                                  SortOrder order = SortOrder::Title);

    SearchHit ScoreEntry(const Entry& entry, const std::string& foldedQuery);
}
