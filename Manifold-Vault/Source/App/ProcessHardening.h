#pragma once

#include <cstddef>

namespace Bunker
{
#if defined(NDEBUG)
    inline constexpr bool kHardenedBuild = true;
#elif defined(_DEBUG)
    inline constexpr bool kHardenedBuild = false;
#else
#   error "Neither NDEBUG nor _DEBUG set -- look at kHardenedBuild."
#endif

    constexpr bool ShouldBlockCapture(bool vaultUnlocked, bool screenshotsAllowed,
                                      bool hardenedBuild)
    {
        return hardenedBuild && vaultUnlocked && !screenshotsAllowed;
    }

    struct HardeningReport
    {
        bool DumpsExcluded = false;
        std::size_t ExcludedRegions = 0;
        std::size_t ExclusionsRefused = 0;

        bool AccessRestricted = false;

        bool LockedMemoryRaised = false;
        bool LockedMemoryVerified = false;
        std::size_t LockedMemoryBudget = 0;
        std::size_t WorkingSetMinBytes = 0;

        bool HandlersInstalled = false;
        bool SodiumMisuseHooked = false;
        bool DynamicCodeDisabled = false;
        bool ExtensionPointsDisabled = false;
        bool StrictHandleChecks = false;
    };

    constexpr bool ShouldRestrictAccess(bool vaultUnlocked, bool hardenedBuild)
    {
        return hardenedBuild && vaultUnlocked;
    }

    bool RestrictProcessAccess();
    void RestoreProcessAccess();

    bool ProcessAccessIsRestricted();

    inline constexpr std::size_t kLockedVaultBytes = 8u * 1024 * 1024;

    constexpr std::size_t RaisedWorkingSetMinimum(std::size_t current,
                                                  std::size_t wanted) noexcept
    {
        return wanted > current ? wanted : current;
    }

    bool RegionIsLocked(const void* address, std::size_t size);

    HardeningReport ApplyProcessHardening();

    void ExcludeFromCrashDumps(const void* address, std::size_t size);
    void IncludeInCrashDumpsAgain(const void* address);

    const HardeningReport& LastHardeningReport();
}
