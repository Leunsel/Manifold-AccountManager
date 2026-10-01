#pragma once

#include "Core/Model.h"
#include "Core/Result.h"
#include "Core/Secret.h"

#include <cstddef>
#include <string>

namespace Bunker::Vault
{
    struct CsvOptions
    {
        bool IncludeSecondFactor = false;

        bool WriteBom = true;
    };

    Status WriteCsv(const Document& doc, const CsvOptions& options, SecureBuffer& out,
                    std::size_t& outLength);

    std::string QuoteCsvField(const std::string& value);
}
