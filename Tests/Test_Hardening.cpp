#include "TestRunner.h"

#include "App/ProcessHardening.h"

#include <windows.h>

#include <cstring>

using namespace Bunker;

namespace
{
    bool Blocked(bool unlocked, bool allowed, bool hardened)
    {
        return ShouldBlockCapture(unlocked, allowed, hardened);
    }

    std::size_t Raised(std::size_t current, std::size_t wanted)
    {
        return RaisedWorkingSetMinimum(current, wanted);
    }

    bool Restrict(bool unlocked, bool hardened)
    {
        return ShouldRestrictAccess(unlocked, hardened);
    }
}

TEST_CASE(Hardening_CaptureIsBlockedInExactlyOneCase)
{
    std::size_t blocked = 0;
    for (int bits = 0; bits < 8; ++bits)
    {
        const bool unlocked = (bits & 1) != 0;
        const bool allowed = (bits & 2) != 0;
        const bool hardened = (bits & 4) != 0;
        if (Blocked(unlocked, allowed, hardened))
            ++blocked;
    }
    CHECK_EQ(blocked, std::size_t{ 1 });

    CHECK_MSG(Blocked(true, false, true),
              "hardened, unlocked, no override -- capture MUST be blocked");
}

TEST_CASE(Hardening_ALockedVaultIsNeverBlocked)
{
    CHECK(!Blocked(false, false, true));
    CHECK(!Blocked(false, true, true));
}

TEST_CASE(Hardening_TheFlagLiftsTheBlock)
{
    CHECK(!Blocked(true, true, true));
}

TEST_CASE(Hardening_ADebugBuildNeverBlocks)
{
    CHECK(!Blocked(true, false, false));
    CHECK(!Blocked(true, true, false));
}

TEST_CASE(Hardening_TheBuildDecidesAsAdvertised)
{
    bool hardened = kHardenedBuild;
#if defined(NDEBUG)
    CHECK_MSG(hardened, "the release build does not count as hardened");
    CHECK_MSG(Blocked(true, false, hardened),
              "the release build does NOT block capture");
#else
    CHECK_MSG(!hardened, "the debug build counts as hardened");
    CHECK_MSG(!Blocked(true, false, hardened),
              "the debug build blocks capture anyway");
#endif
}

TEST_CASE(Hardening_RaisingTheMinimumNeverLowersIt)
{
    CHECK_EQ(Raised(1000, 500), std::size_t{ 1000 });
    CHECK_EQ(Raised(500, 1000), std::size_t{ 1000 });
    CHECK_EQ(Raised(777, 777), std::size_t{ 777 });
    CHECK_EQ(Raised(0, 4096), std::size_t{ 4096 });
}

TEST_CASE(Hardening_ALockedRegionIsSeenAsLocked)
{
    constexpr std::size_t kBytes = 64u * 1024;
    void* p = ::VirtualAlloc(nullptr, kBytes, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
    CHECK(p != nullptr);
    if (!p)
        return;

    const bool locked = ::VirtualLock(p, kBytes) != 0;
    CHECK_MSG(locked, "VirtualLock already failed at 64 KiB -- so "
                      "the case below measures nothing");
    if (locked)
        CHECK_MSG(RegionIsLocked(p, kBytes),
                  "locked, but RegionIsLocked says no");

    if (locked)
        ::VirtualUnlock(p, kBytes);
    ::VirtualFree(p, 0, MEM_RELEASE);
}

TEST_CASE(Hardening_AnUnlockedRegionIsNotSeenAsLocked)
{
    constexpr std::size_t kBytes = 64u * 1024;
    void* p = ::VirtualAlloc(nullptr, kBytes, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
    CHECK(p != nullptr);
    if (!p)
        return;

    std::memset(p, 0x5A, kBytes);

    CHECK_MSG(!RegionIsLocked(p, kBytes),
              "a region that is NOT locked is reported as locked");

    ::VirtualFree(p, 0, MEM_RELEASE);
}

TEST_CASE(Hardening_RegionIsLockedRefusesNonsense)
{
    CHECK(!RegionIsLocked(nullptr, 4096));
    int onTheStack = 0;
    CHECK(!RegionIsLocked(&onTheStack, 0));
}

TEST_CASE(Hardening_TheAccessRuleNeedsBothConditions)
{
    CHECK(Restrict(true, true));
    CHECK_MSG(!Restrict(false, true),
              "the locked vault is isolated anyway");
    CHECK_MSG(!Restrict(true, false),
              "the debug build isolates the process -- no debugger can attach");
    CHECK(!Restrict(false, false));
}

TEST_CASE(Hardening_RestrictingRemovesReadAccess)
{
    CHECK_MSG(!ProcessAccessIsRestricted(),
              "the process counts as isolated before isolation");

    const bool applied = RestrictProcessAccess();
    CHECK_MSG(applied, "RestrictProcessAccess failed");

    if (applied)
    {
        CHECK_MSG(ProcessAccessIsRestricted(),
                  "isolated, but the measurement says no");
        RestoreProcessAccess();
        CHECK_MSG(!ProcessAccessIsRestricted(),
                  "after reverting, the process still counts as "
                  "isolated -- the original descriptor did not come "
                  "back");
    }
}

TEST_CASE(Hardening_RestoringWithoutRestrictingIsHarmless)
{
    RestoreProcessAccess();
    RestoreProcessAccess();
    CHECK(!ProcessAccessIsRestricted());
}
