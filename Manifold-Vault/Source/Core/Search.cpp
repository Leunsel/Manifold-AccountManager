#include "Core/Search.h"

#include <algorithm>

namespace Bunker::Vault
{
    namespace
    {
        // Latin-1 U+00C0..U+00FF, eight code points per row.
        constexpr const char* kLatin1Fold[64] = {
             "a", "a", "a", "a", "ae", "a", "ae", "c",
             "e", "e", "e", "e", "i", "i", "i", "i",
             "d", "n", "o", "o", "o", "o", "oe", nullptr,
             "o", "u", "u", "u", "ue", "y", "th", "ss",
             "a", "a", "a", "a", "ae", "a", "ae", "c",
             "e", "e", "e", "e", "i", "i", "i", "i",
             "d", "n", "o", "o", "o", "o", "oe", nullptr,
             "o", "u", "u", "u", "ue", "y", "th", "y",
        };

        class FoldCursor
        {
        public:
            explicit FoldCursor(std::string_view text) noexcept : m_text(text) {}

            std::size_t LastBegin() const noexcept { return m_lastBegin; }
            std::size_t LastEnd() const noexcept { return m_at; }

            bool Next(char& out) noexcept
            {
                if (m_pending && *m_pending)
                {
                    out = *m_pending++;
                    return true;
                }
                if (m_at >= m_text.size())
                    return false;

                m_lastBegin = m_at;
                const unsigned char c = static_cast<unsigned char>(m_text[m_at]);
                if (c >= 'A' && c <= 'Z')
                {
                    out = static_cast<char>(c - 'A' + 'a');
                    ++m_at;
                    return true;
                }
                if (c < 0x80)
                {
                    out = static_cast<char>(c);
                    ++m_at;
                    return true;
                }

                if (c == 0xC3 && m_at + 1 < m_text.size())
                {
                    const unsigned char second = static_cast<unsigned char>(m_text[m_at + 1]);
                    if (second >= 0x80 && second <= 0xBF)
                    {
                        if (const char* folded = kLatin1Fold[second - 0x80])
                        {
                            m_at += 2;
                            out = folded[0];
                            m_pending = folded + 1;
                            return true;
                        }
                    }
                }
                out = static_cast<char>(c);
                ++m_at;
                return true;
            }

        private:
            std::string_view m_text;
            std::size_t m_at = 0;
            std::size_t m_lastBegin = 0;
            const char* m_pending = nullptr;
        };
    }

    std::string FoldForSearch(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        FoldCursor cursor(text);
        char c = 0;
        while (cursor.Next(c))
            out.push_back(c);
        return out;
    }

    bool FoldedEquals(std::string_view a, std::string_view b) noexcept
    {
        FoldCursor left(a);
        FoldCursor right(b);
        char x = 0;
        char y = 0;
        for (;;)
        {
            const bool moreLeft = left.Next(x);
            const bool moreRight = right.Next(y);
            if (moreLeft != moreRight)
                return false;
            if (!moreLeft)
                return true;
            if (x != y)
                return false;
        }
    }

    bool FoldedContains(std::string_view text, std::string_view foldedNeedle) noexcept
    {
        if (foldedNeedle.empty())
            return true;
        FoldCursor start(text);
        for (;;)
        {
            FoldCursor probe = start;
            std::size_t matched = 0;
            char c = 0;
            while (matched < foldedNeedle.size() && probe.Next(c) &&
                   c == foldedNeedle[matched])
                ++matched;
            if (matched == foldedNeedle.size())
                return true;
            if (!start.Next(c))
                return false;
        }
    }

    bool FindFoldedMatch(std::string_view text, std::string_view foldedNeedle,
                         std::size_t& begin, std::size_t& end) noexcept
    {
        if (foldedNeedle.empty())
            return false;
        FoldCursor start(text);
        for (;;)
        {
            FoldCursor probe = start;
            std::size_t matched = 0;
            std::size_t first = 0;
            char c = 0;
            while (matched < foldedNeedle.size() && probe.Next(c) && c == foldedNeedle[matched])
            {
                if (matched == 0)
                    first = probe.LastBegin();
                ++matched;
            }
            if (matched == foldedNeedle.size())
            {
                begin = first;
                end = probe.LastEnd();
                return true;
            }
            if (!start.Next(c))
                return false;
        }
    }

    namespace
    {
        const Field* LoginOf(const Entry& e)
        {
            const Field* email = nullptr;
            for (const Field& f : e.Fields)
            {
                if (f.Value.Empty())
                    continue;
                if (f.Role == FieldRole::Username)
                    return &f;
                if (f.Role == FieldRole::Email && !email)
                    email = &f;
            }
            return email;
        }
    }

    std::uint64_t FindLikelyDuplicate(const Document& doc, std::uint64_t except,
                                      std::string_view platformId, std::string_view title,
                                      std::string_view login)
    {
        if (login.empty())
            return 0;
        for (const Entry& e : doc.Entries)
        {
            if (e.Id == except)
                continue;
            const bool samePlace = platformId.empty()
                                       ? (e.PlatformId.empty() && !title.empty() &&
                                          FoldedEquals(e.Title, title))
                                       : (!e.PlatformId.empty() &&
                                          FoldedEquals(e.PlatformId, platformId));
            if (!samePlace)
                continue;
            const Field* theirs = LoginOf(e);
            if (theirs && FoldedEquals(theirs->Value.View(), login))
                return e.Id;
        }
        return 0;
    }

    namespace
    {
        constexpr int kScoreTitleExact = 100;
        constexpr int kScoreTitlePrefix = 80;
        constexpr int kScoreTitleContains = 60;
        constexpr int kScorePlatform = 50;
        constexpr int kScoreFolder = 40;
        constexpr int kScoreFieldName = 30;
        constexpr int kScoreFieldValue = 20;
        constexpr int kScoreListLabel = 25;

        bool Contains(const std::string& haystack, const std::string& needle)
        {
            return haystack.find(needle) != std::string::npos;
        }
    }

    SearchHit ScoreEntry(const Entry& entry, const std::string& foldedQuery)
    {
        SearchHit hit;
        hit.EntryId = entry.Id;
        if (foldedQuery.empty())
            return hit;

        const std::string title = FoldForSearch(entry.Title);
        if (title == foldedQuery)
        {
            hit.Score = kScoreTitleExact;
            hit.Where = MatchKind::Title;
            return hit;
        }
        if (title.rfind(foldedQuery, 0) == 0)
        {
            hit.Score = kScoreTitlePrefix;
            hit.Where = MatchKind::Title;
            return hit;
        }
        if (Contains(title, foldedQuery))
        {
            hit.Score = kScoreTitleContains;
            hit.Where = MatchKind::Title;
            return hit;
        }
        if (Contains(FoldForSearch(entry.PlatformId), foldedQuery))
        {
            hit.Score = kScorePlatform;
            hit.Where = MatchKind::Platform;
            return hit;
        }

        if (!entry.Folder.empty() &&
            Contains(FoldForSearch(entry.Folder), foldedQuery))
        {
            hit.Score = kScoreFolder;
            hit.Where = MatchKind::Folder;
            return hit;
        }

        for (const Field& f : entry.Fields)
            if (Contains(FoldForSearch(f.Key), foldedQuery))
            {
                hit.Score = kScoreFieldName;
                hit.Where = MatchKind::FieldName;
                return hit;
            }

        for (const ListItem& item : entry.ListItems)
            if (FoldedContains(item.Label, foldedQuery))
            {
                hit.Score = kScoreListLabel;
                hit.Where = MatchKind::ListLabel;
                return hit;
            }

        for (const Field& f : entry.Fields)
        {
            if (f.IsSecret())
                continue;
            if (FoldedContains(f.Value.View(), foldedQuery))
            {
                hit.Score = kScoreFieldValue;
                hit.Where = MatchKind::FieldValue;
                return hit;
            }
        }

        return hit;
    }

    SortOrder SortOrderFrom(std::uint32_t stored)
    {
        return stored == static_cast<std::uint32_t>(SortOrder::Modified) ? SortOrder::Modified
                                                                         : SortOrder::Title;
    }

    std::vector<SearchHit> Search(const Document& doc, const std::string& query,
                                  SortOrder order)
    {
        const std::string folded = FoldForSearch(query);

        struct Row
        {
            SearchHit Hit;
            std::string FoldedTitle;
            std::uint64_t Modified = 0;
        };
        std::vector<Row> rows;
        rows.reserve(doc.Entries.size());

        for (const Entry& e : doc.Entries)
        {
            SearchHit hit = ScoreEntry(e, folded);
            if (!folded.empty() && hit.Score == 0)
                continue;
            rows.push_back(Row{ hit, FoldForSearch(e.Title), e.ModifiedUtc });
        }

        std::sort(rows.begin(), rows.end(), [order](const Row& a, const Row& b) {
            if (a.Hit.Score != b.Hit.Score)
                return a.Hit.Score > b.Hit.Score;
            if (order == SortOrder::Modified && a.Modified != b.Modified)
                return a.Modified > b.Modified;
            if (a.FoldedTitle != b.FoldedTitle)
                return a.FoldedTitle < b.FoldedTitle;
            return a.Hit.EntryId < b.Hit.EntryId;
        });

        std::vector<SearchHit> hits;
        hits.reserve(rows.size());
        for (const Row& r : rows)
            hits.push_back(r.Hit);
        return hits;
    }
}
