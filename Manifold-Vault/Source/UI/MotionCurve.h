#pragma once

#include <cmath>

namespace Bunker::UI::Curve
{
    inline float EaseOut(float t)
    {
        if (!(t > 0.0f))
            return 0.0f;
        if (t >= 1.0f)
            return 1.0f;
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }

    inline float StepToward(float value, float target, float dt, float seconds)
    {
        if (!(seconds > 0.0f) || !(dt >= 0.0f))
            return target;
        const float step = dt / seconds;
        if (value < target)
            return (value + step < target) ? value + step : target;
        return (value - step > target) ? value - step : target;
    }

    inline float StepFollow(float value, float target, float dt, float seconds)
    {
        if (!(seconds > 0.0f) || !(dt >= 0.0f))
            return target;
        const float keep = std::exp(-2.9957323f * dt / seconds);
        const float next = target + (value - target) * keep;
        const float gap = next - target;
        return (gap < 0.1f && gap > -0.1f) ? target : next;
    }
}
