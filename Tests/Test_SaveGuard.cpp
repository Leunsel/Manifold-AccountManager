#include "TestRunner.h"

#include "Core/SaveGuard.h"

using namespace Bunker;

namespace
{
    Status Broke()
    {
        return Error{ ErrorCode::InvalidArgument, "Schreibfehler" };
    }
}

TEST_CASE(SaveGuard_AFailureBecomesAState)
{
    SaveGuard g;
    CHECK(!g.Failing());
    CHECK(!g.HoldsAutoLock(0.0));

    g.Record(Broke(), 100.0);
    CHECK_MSG(g.Failing(), "a failed save left no state");
    CHECK(g.Code() == ErrorCode::InvalidArgument);
    CHECK_EQ(g.ConsecutiveFailures(), 1);
    CHECK_MSG(g.HoldsAutoLock(100.0),
              "the automatic lock was not held back");

    g.Record(Status{}, 150.0);
    CHECK(!g.Failing());
    CHECK(!g.HoldsAutoLock(150.0));
    CHECK_EQ(g.ConsecutiveFailures(), 0);
}

TEST_CASE(SaveGuard_TheClockStartsAtTheFirstFailureNotTheLast)
{
    SaveGuard g;
    g.Record(Broke(), 100.0);
    for (double t = 130.0; t < 700.0; t += 30.0)
        g.Record(Broke(), t);

    CHECK_MSG(g.SecondsFailing(700.0) >= 599.0,
              "the clock was reset by every retry");
    CHECK_MSG(!g.HoldsAutoLock(100.0 + SaveGuard::kHoldSeconds + 1.0),
              "the lock was held back beyond the grace period");
    CHECK(g.HoldsAutoLock(100.0 + SaveGuard::kHoldSeconds - 1.0));
    CHECK(g.ConsecutiveFailures() > 1);
}

TEST_CASE(SaveGuard_AcknowledgingStopsTheHoldWithoutClaimingSuccess)
{
    SaveGuard g;
    g.Record(Broke(), 100.0);
    CHECK(g.HoldsAutoLock(120.0));

    g.Acknowledge();
    CHECK_MSG(!g.HoldsAutoLock(120.0), "acknowledging did not release the lock");
    CHECK_MSG(g.Failing(), "acknowledging pretended that a save happened");
    CHECK(g.Code() == ErrorCode::InvalidArgument);
}

TEST_CASE(SaveGuard_ASuccessAfterAnAcknowledgementIsACleanSlate)
{
    SaveGuard g;
    g.Record(Broke(), 100.0);
    g.Acknowledge();
    g.Record(Status{}, 200.0);

    CHECK(!g.Failing());
    CHECK(g.Code() == ErrorCode::Ok);

    g.Record(Broke(), 300.0);
    CHECK_MSG(g.HoldsAutoLock(310.0),
              "an old acknowledgement still applied to a new failure");
}

TEST_CASE(SaveGuard_AClockThatWentBackwardsIsNotTreatedAsFresh)
{
    SaveGuard g;
    g.Record(Broke(), 1000.0);
    CHECK_EQ(g.SecondsFailing(900.0), 0.0);
    CHECK(g.HoldsAutoLock(900.0));
}
