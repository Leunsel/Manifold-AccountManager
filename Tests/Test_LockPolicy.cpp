#include "TestRunner.h"

#include "Core/LockPolicy.h"

#include <cstring>
#include <string>

using namespace Bunker;

namespace
{
    LockPolicy Fresh(std::uint32_t idleSeconds = 300)
    {
        LockPolicy p;
        LockSettings s;
        s.IdleSeconds = idleSeconds;
        p.SetSettings(s);
        p.Reset(0.0);
        return p;
    }
}

TEST_CASE(Lock_IdleFiresExactlyAtTheTimeout)
{
    LockPolicy p = Fresh(300);

    CHECK(p.Evaluate(299.0, true) == LockReason::None);
    CHECK(p.Evaluate(299.999, true) == LockReason::None);
    CHECK(p.Evaluate(300.0, true) == LockReason::Idle);
}

TEST_CASE(Lock_ActivityPostponesTheIdleLock)
{
    LockPolicy p = Fresh(300);

    p.NotifyActivity(250.0);
    CHECK(p.Evaluate(500.0, true) == LockReason::None);
    CHECK(p.Evaluate(550.0, true) == LockReason::Idle);
}

TEST_CASE(Lock_ZeroTimeoutDisablesIdleLocking)
{
    LockPolicy p = Fresh(0);
    CHECK(p.Evaluate(1.0e9, true) == LockReason::None);
    CHECK(p.SecondsUntilIdleLock(1.0e9) < 0.0);
}

TEST_CASE(Lock_NothingFiresWhileAlreadyLocked)
{
    LockPolicy p = Fresh(10);
    p.NotifyEvent(LockReason::Suspend, 0.0);

    CHECK(p.Evaluate(100.0, false) == LockReason::None);
    CHECK(p.Evaluate(200.0, true) == LockReason::Idle);
}

TEST_CASE(Lock_SessionEventsFireImmediately)
{
    for (LockReason r : { LockReason::SessionLocked, LockReason::Suspend,
                          LockReason::Shutdown, LockReason::Manual,
                          LockReason::Minimised })
    {
        LockPolicy p = Fresh(300);
        p.NotifyEvent(r, 10.0);
        CHECK_MSG(p.Evaluate(10.0, true) == r,
                  std::string("immediate event delayed: ") + ToString(r));
    }
}

TEST_CASE(Lock_FocusLossWaitsAndCanBeCancelled)
{
    LockPolicy p;
    LockSettings s;
    s.IdleSeconds = 0;
    s.LockOnFocusLost = true;
    s.FocusGraceSeconds = 20.0;
    p.SetSettings(s);
    p.Reset(0.0);

    p.NotifyEvent(LockReason::FocusLost, 100.0);
    CHECK(p.Evaluate(110.0, true) == LockReason::None);

    p.NotifyActivity(115.0);
    CHECK(p.Evaluate(200.0, true) == LockReason::None);

    p.NotifyEvent(LockReason::FocusLost, 300.0);
    CHECK(p.Evaluate(319.0, true) == LockReason::None);
    CHECK(p.Evaluate(320.0, true) == LockReason::FocusLost);
}

TEST_CASE(Lock_StrongerReasonWinsOverPending)
{
    LockPolicy p;
    LockSettings s;
    s.IdleSeconds = 0;
    s.LockOnFocusLost = true;
    p.SetSettings(s);
    p.Reset(0.0);

    p.NotifyEvent(LockReason::FocusLost, 10.0);
    p.NotifyEvent(LockReason::Suspend, 10.1);
    CHECK(p.Evaluate(10.2, true) == LockReason::Suspend);
}

TEST_CASE(Lock_WeakerReasonDoesNotDisplaceStronger)
{
    LockPolicy p = Fresh(0);
    p.NotifyEvent(LockReason::Shutdown, 5.0);
    p.NotifyEvent(LockReason::Minimised, 5.1);
    CHECK(p.Evaluate(5.2, true) == LockReason::Shutdown);
}

TEST_CASE(Lock_EventIsConsumedOnlyOnce)
{
    LockPolicy p = Fresh(0);
    p.NotifyEvent(LockReason::Manual, 1.0);
    CHECK(p.Evaluate(1.0, true) == LockReason::Manual);
    CHECK(p.Evaluate(1.0, true) == LockReason::None);
}

TEST_CASE(Lock_CountdownCountsDown)
{
    LockPolicy p = Fresh(60);
    CHECK(p.SecondsUntilIdleLock(0.0) > 59.9);
    CHECK(p.SecondsUntilIdleLock(30.0) > 29.9);
    CHECK(p.SecondsUntilIdleLock(30.0) < 30.1);
    CHECK_EQ(p.SecondsUntilIdleLock(120.0), 0.0);

    p.NotifyActivity(100.0);
    CHECK(p.SecondsUntilIdleLock(100.0) > 59.9);
}

TEST_CASE(Lock_ReasonsAllHaveText)
{
    for (LockReason r : { LockReason::Manual, LockReason::Idle,
                          LockReason::SessionLocked, LockReason::Suspend,
                          LockReason::Minimised, LockReason::FocusLost,
                          LockReason::Shutdown })
    {
        CHECK_MSG(std::strlen(ToString(r)) > 0, "no identifier");
        CHECK_MSG(std::strlen(ToUserMessage(r)) > 0,
                  std::string("no German message for ") + ToString(r));
        CHECK_MSG(std::strcmp(ToString(r), "Unknown") != 0,
                  "ToString does not know a reason");
    }
    CHECK_EQ(std::strlen(ToUserMessage(LockReason::None)), std::size_t{ 0 });
}
