#include "Core/SettingsPolicy.h"

#include "Core/Language.h"

#include <cstring>

namespace Bunker::Vault
{
    namespace
    {
        std::uint32_t Clamp(std::uint32_t value, std::uint32_t low, std::uint32_t high)
        {
            if (value < low)
                return low;
            if (value > high)
                return high;
            return value;
        }
    }

    void ClampSettings(Settings& settings)
    {
        if (settings.AutoLockSeconds != 0)
            settings.AutoLockSeconds =
                Clamp(settings.AutoLockSeconds, kMinAutoLockSeconds, kMaxAutoLockSeconds);

        settings.ClipboardClearSeconds = Clamp(settings.ClipboardClearSeconds,
                                               kMinClipboardClearSeconds,
                                               kMaxClipboardClearSeconds);
        settings.FocusGraceSeconds = Clamp(settings.FocusGraceSeconds,
                                           kMinFocusGraceSeconds, kMaxFocusGraceSeconds);
        settings.BackupKeep = Clamp(settings.BackupKeep, kMinBackupKeep, kMaxBackupKeep);
        settings.BackupMinIntervalSeconds = Clamp(settings.BackupMinIntervalSeconds,
                                                  kMinBackupIntervalSeconds,
                                                  kMaxBackupIntervalSeconds);
    }

    LockSettings ToLockSettings(const Settings& settings)
    {
        LockSettings out;
        out.IdleSeconds = settings.AutoLockSeconds == 0
                              ? 0u
                              : Clamp(settings.AutoLockSeconds, kMinAutoLockSeconds,
                                      kMaxAutoLockSeconds);
        out.LockOnMinimise = settings.LockOnMinimise;
        out.LockOnFocusLost = settings.LockOnFocusLost;
        out.FocusGraceSeconds = static_cast<double>(
            Clamp(settings.FocusGraceSeconds, kMinFocusGraceSeconds, kMaxFocusGraceSeconds));
        return out;
    }

    std::uint32_t BackupKeep(const Settings& settings)
    {
        return Clamp(settings.BackupKeep, kMinBackupKeep, kMaxBackupKeep);
    }

    std::uint32_t BackupIntervalSeconds(const Settings& settings)
    {
        return Clamp(settings.BackupMinIntervalSeconds, kMinBackupIntervalSeconds,
                     kMaxBackupIntervalSeconds);
    }

    const char* LanguageCode(const Settings& settings)
    {
        if (settings.Language.empty())
            return "en";
        for (std::size_t i = 0; i < Lang::kLanguageCount; ++i)
            if (std::strcmp(Lang::kLanguages[i].Code, settings.Language.c_str()) == 0)
                return Lang::kLanguages[i].Code;
        return "en";
    }

    std::uint32_t ClipboardSeconds(const Settings& settings)
    {
        return Clamp(settings.ClipboardClearSeconds, kMinClipboardClearSeconds,
                     kMaxClipboardClearSeconds);
    }
}
