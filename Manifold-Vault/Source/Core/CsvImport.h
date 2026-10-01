#pragma once

#include "Core/Model.h"
#include "Core/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Bunker::Vault
{
    enum class RowVerdict : std::uint8_t
    {
        Ready = 0,
        Blank,
        NoTitle,
        Invalid,
        Duplicate,
    };

    const char* RowVerdictName(RowVerdict verdict);

    struct ImportRow
    {
        std::size_t Line = 0;
        RowVerdict Verdict = RowVerdict::Blank;

        std::string Detail;

        Entry Draft;

        bool TitleDerived = false;

        bool SecondFactorDropped = false;
        std::string SecondFactorProblem;
    };

    static_assert(std::is_nothrow_move_constructible_v<ImportRow> &&
                      std::is_nothrow_move_assignable_v<ImportRow>,
                  "ImportRow must stay nothrow-movable, or vector growth copies drafts");

    struct ImportReport
    {
        std::vector<std::string> Columns;
        std::vector<std::string> UnmappedColumns;

        std::vector<ImportRow> Rows;

        std::size_t Ready = 0;
        std::size_t Skipped = 0;
        std::size_t Problems = 0;
        std::size_t Duplicates = 0;
    };

    inline constexpr std::size_t kMaxImportRows = 20000;

    Status ParseCsv(const char* text, std::size_t length, ImportReport& out);

    std::size_t MarkDuplicates(const Document& doc, ImportReport& report);

    struct ImportCommit
    {
        std::size_t Added = 0;
        std::size_t Refused = 0;
    };
    ImportCommit CommitImport(Document& doc, ImportReport& report, std::uint64_t nowUtc);

    void WipeEntrySecrets(Entry& entry);

    struct CsvField
    {
        std::string_view Raw;

        std::size_t DoubledQuotes = 0;

        bool Malformed = false;
    };

    bool ReadCsvField(const char* text, std::size_t length, std::size_t& at,
                      CsvField& out, bool& endOfRecord);

    std::size_t UnescapeCsvField(std::string_view raw, char* dest);
}
