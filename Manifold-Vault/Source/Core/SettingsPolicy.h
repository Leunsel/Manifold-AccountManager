#pragma once

#include "Core/LockPolicy.h"
#include "Core/Model.h"

#include <cstdint>

namespace Bunker::Vault
{
    inline constexpr std::uint32_t kMinAutoLockSeconds = 60;
    inline constexpr std::uint32_t kMaxAutoLockSeconds = 3600;

    inline constexpr std::uint32_t kMinClipboardClearSeconds = 5;
    inline constexpr std::uint32_t kMaxClipboardClearSeconds = 300;

    inline constexpr std::uint32_t kMinFocusGraceSeconds = 5;
    inline constexpr std::uint32_t kMaxFocusGraceSeconds = 300;

    inline constexpr std::uint32_t kMinBackupKeep = 3;
    inline constexpr std::uint32_t kMaxBackupKeep = 20;

    inline constexpr std::uint32_t kMinBackupIntervalSeconds = 300;
    inline constexpr std::uint32_t kMaxBackupIntervalSeconds = 86400;

    void ClampSettings(Settings& settings);

    const char* LanguageCode(const Settings& settings);

    LockSettings ToLockSettings(const Settings& settings);

    std::uint32_t BackupKeep(const Settings& settings);
    std::uint32_t BackupIntervalSeconds(const Settings& settings);

    std::uint32_t ClipboardSeconds(const Settings& settings);
}
