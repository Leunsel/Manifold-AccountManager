#include "TestRunner.h"

#include "UI/MotionCurve.h"

using namespace Bunker::UI;

namespace
{
    bool Near(float a, float b, float tolerance = 0.001f)
    {
        const float d = a - b;
        return (d < 0 ? -d : d) <= tolerance;
    }
}

TEST_CASE(Motion_EaseOutEndsAndClamps)
{
    CHECK(Curve::EaseOut(0.0f) == 0.0f);
    CHECK(Curve::EaseOut(1.0f) == 1.0f);
    CHECK(Curve::EaseOut(-3.0f) == 0.0f);
    CHECK(Curve::EaseOut(250.0f) == 1.0f);
    CHECK(Curve::EaseOut(std::nanf("")) == 0.0f);
}

TEST_CASE(Motion_EaseOutRisesAndIsFrontLoaded)
{
    float last = 0.0f;
    for (int i = 1; i <= 100; ++i)
    {
        const float v = Curve::EaseOut(i / 100.0f);
        CHECK(v >= last);
        last = v;
    }
    CHECK(Curve::EaseOut(0.25f) > 0.5f);
}

TEST_CASE(Motion_StepTowardArrivesWithoutOvershoot)
{
    float v = 0.0f;
    int frames = 0;
    while (v < 1.0f && frames < 1000)
    {
        const float next = Curve::StepToward(v, 1.0f, 1.0f / 60.0f, 0.12f);
        CHECK(next >= v);
        CHECK(next <= 1.0f);
        v = next;
        ++frames;
    }
    CHECK(v == 1.0f);
    CHECK(frames <= 8);

    while (v > 0.0f && frames < 2000)
    {
        v = Curve::StepToward(v, 0.0f, 1.0f / 60.0f, 0.12f);
        CHECK(v >= 0.0f);
        ++frames;
    }
    CHECK(v == 0.0f);
}

TEST_CASE(Motion_StepTowardWithoutTimeIsAtTarget)
{
    CHECK(Curve::StepToward(0.3f, 1.0f, 0.016f, 0.0f) == 1.0f);
    CHECK(Curve::StepToward(0.3f, 0.0f, -1.0f, 0.12f) == 0.0f);
    CHECK(Curve::StepToward(0.1f, 1.0f, 5.0f, 0.12f) == 1.0f);
}

TEST_CASE(Motion_FollowArrivesAndNeverOvershoots)
{
    float x = 0.0f;
    int frames = 0;
    while (x != 200.0f && frames < 1000)
    {
        const float next = Curve::StepFollow(x, 200.0f, 1.0f / 60.0f, 0.14f);
        CHECK(next >= x);
        CHECK(next <= 200.0f);
        x = next;
        ++frames;
    }
    CHECK(x == 200.0f);
    CHECK(frames < 60);

    float y = 0.0f;
    for (int i = 0; i < 60; ++i)
        y = Curve::StepFollow(y, 100.0f, 0.14f / 60.0f, 0.14f);
    CHECK(Near(y, 95.0f, 0.05f));
}

TEST_CASE(Motion_FollowIsFrameRateIndependent)
{
    const float whole = Curve::StepFollow(0.0f, 300.0f, 0.02f, 0.14f);
    const float half = Curve::StepFollow(Curve::StepFollow(0.0f, 300.0f, 0.01f, 0.14f),
                                         300.0f, 0.01f, 0.14f);
    CHECK(Near(whole, half, 0.01f));
}
