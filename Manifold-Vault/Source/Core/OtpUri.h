#pragma once

#include "Core/Otp.h"
#include "Core/Result.h"

#include <string_view>

namespace Bunker
{
    bool LooksLikeOtpAuthUri(std::string_view text);

    Result<TotpConfig> ParseOtpAuthUri(std::string_view text);

    Result<TotpConfig> ParseBareSecret(std::string_view text);

    Result<TotpConfig> ParsePastedSecret(std::string_view text);
}
