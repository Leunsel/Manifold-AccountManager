#include "Core/Folder.h"

#include "Core/GermanText.h"
#include "Core/Search.h"

#include <algorithm>

namespace Bunker::Vault
{
    std::string FoldFolderName(const std::string& name)
    {
        return LowercaseGerman(name);
    }

    bool SameFolder(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        return FoldFolderName(a) == FoldFolderName(b);
    }

    std::vector<std::string> FolderNames(const Document& doc)
    {
        struct Row { std::string Name; std::string Folded; };
        std::vector<Row> rows;

        for (const Entry& e : doc.Entries)
        {
            if (e.Folder.empty())
                continue;

            const std::string folded = FoldFolderName(e.Folder);
            bool seen = false;
            for (const Row& r : rows)
                if (r.Folded == folded)
                    seen = true;
            if (!seen)
                rows.push_back(Row{ e.Folder, folded });
        }

        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.Folded != b.Folded)
                return a.Folded < b.Folded;
            return a.Name < b.Name;
        });

        std::vector<std::string> names;
        names.reserve(rows.size());
        for (const Row& r : rows)
            names.push_back(r.Name);
        return names;
    }

    FolderSurvey SurveyFolders(const Document& doc)
    {
        FolderSurvey survey;
        survey.Total = doc.Entries.size();
        for (const Entry& e : doc.Entries)
        {
            if (e.Folder.empty())
            {
                ++survey.Unfiled;
                continue;
            }
            const std::string folded = FoldFolderName(e.Folder);
            bool found = false;
            for (FolderTally& row : survey.Folders)
            {
                if (row.Folded != folded)
                    continue;
                ++row.Count;
                found = true;
                break;
            }
            if (!found)
                survey.Folders.push_back(FolderTally{ e.Folder, folded, 1 });
        }
        std::sort(survey.Folders.begin(), survey.Folders.end(),
                  [](const FolderTally& a, const FolderTally& b) {
                      if (a.Folded != b.Folded)
                          return a.Folded < b.Folded;
                      return a.Name < b.Name;
                  });
        return survey;
    }

    std::size_t CountEntriesInFolder(const Document& doc, const std::string& name)
    {
        if (name.empty())
            return 0;

        const std::string folded = FoldFolderName(name);
        std::size_t count = 0;
        for (const Entry& e : doc.Entries)
            if (!e.Folder.empty() && FoldFolderName(e.Folder) == folded)
                ++count;
        return count;
    }

    std::string ExistingSpelling(const Document& doc, const std::string& name)
    {
        if (name.empty())
            return {};

        const std::string folded = FoldFolderName(name);
        for (const Entry& e : doc.Entries)
            if (!e.Folder.empty() && FoldFolderName(e.Folder) == folded)
                return e.Folder;
        return {};
    }

    namespace
    {
        std::vector<std::vector<std::string>> LettersByWord(const std::string& name)
        {
            std::vector<std::vector<std::string>> words;
            bool atWordStart = true;
            for (std::size_t i = 0; i < name.size();)
            {
                const unsigned char ch = static_cast<unsigned char>(name[i]);
                const std::size_t seq = std::min(Utf8SequenceLength(ch), name.size() - i);
                const bool letter = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                                    (ch >= 'a' && ch <= 'z') || ch >= 0x80;
                if (!letter)
                {
                    atWordStart = true;
                }
                else
                {
                    if (atWordStart)
                        words.emplace_back();
                    atWordStart = false;
                    words.back().push_back(name.substr(i, seq));
                }
                i += seq;
            }
            return words;
        }

        std::string UpperLetter(const std::string& letter)
        {
            std::string upper = UppercaseGerman(letter);
            const bool oneCharacter =
                !upper.empty() &&
                Utf8SequenceLength(static_cast<unsigned char>(upper[0])) == upper.size();
            return oneCharacter ? upper : letter;
        }
    }

    std::vector<std::string> FolderMonograms(const std::vector<std::string>& names)
    {
        std::vector<std::string> out;
        out.reserve(names.size());
        std::vector<std::string> taken;

        for (const std::string& name : names)
        {
            const std::vector<std::vector<std::string>> words = LettersByWord(name);

            std::vector<std::string> candidates;
            if (words.empty())
            {
                if (!name.empty())
                    candidates.push_back(name.substr(
                        0, std::min(Utf8SequenceLength(static_cast<unsigned char>(name[0])),
                                    name.size())));
                else
                    candidates.emplace_back();
            }
            else
            {
                const std::string first = UpperLetter(words[0][0]);
                for (std::size_t w = 1; w < words.size(); ++w)
                    candidates.push_back(first + UpperLetter(words[w][0]));
                if (words.size() == 1 && words[0].size() == 1)
                    candidates.push_back(first);
                for (std::size_t w = 0; w < words.size(); ++w)
                    for (std::size_t k = (w == 0 ? 1 : 0); k < words[w].size(); ++k)
                        candidates.push_back(first + LowercaseGerman(words[w][k]));
            }

            std::string chosen = candidates.front();
            for (const std::string& c : candidates)
            {
                if (std::find(taken.begin(), taken.end(), LowercaseGerman(c)) == taken.end())
                {
                    chosen = c;
                    break;
                }
            }
            taken.push_back(LowercaseGerman(chosen));
            out.push_back(std::move(chosen));
        }
        return out;
    }

    std::vector<std::size_t> MatchFolders(const FolderSurvey& survey, std::string_view query)
    {
        while (!query.empty() && (query.front() == ' ' || query.front() == '\t'))
            query.remove_prefix(1);
        while (!query.empty() && (query.back() == ' ' || query.back() == '\t'))
            query.remove_suffix(1);
        const std::string needle = FoldForSearch(query);

        std::vector<std::size_t> starting;
        std::vector<std::size_t> containing;
        for (std::size_t i = 0; i < survey.Folders.size(); ++i)
        {
            if (needle.empty())
            {
                starting.push_back(i);
                continue;
            }
            const std::size_t at = FoldForSearch(survey.Folders[i].Name).find(needle);
            if (at == 0)
                starting.push_back(i);
            else if (at != std::string::npos)
                containing.push_back(i);
        }
        starting.insert(starting.end(), containing.begin(), containing.end());
        return starting;
    }
}
