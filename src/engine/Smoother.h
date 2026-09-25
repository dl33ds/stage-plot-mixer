// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cmath>

namespace spm::engine
{

/** Linear ramp towards a target value, so gain changes don't click.
    Audio thread only. A new target restarts the ramp from the current value.
*/
class Smoother
{
public:
    /** rampSamples: how long a full change takes. Resets to the target immediately. */
    void reset (float value, int rampSamples) noexcept
    {
        current = target = value;
        length = rampSamples > 0 ? rampSamples : 1;
        remaining = 0;
        step = 0.0f;
    }

    void setTarget (float newTarget) noexcept
    {
        if (! (newTarget < target || newTarget > target))  // unchanged (exact compare is intended)
            return;

        target = newTarget;
        remaining = length;
        step = (target - current) / (float) length;
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            current = --remaining == 0 ? target : current + step;
        }

        return current;
    }

    /** Advances by n samples without producing values. */
    void skip (int n) noexcept
    {
        if (remaining <= 0)
            return;

        if (n >= remaining)
        {
            current = target;
            remaining = 0;
        }
        else
        {
            current += step * (float) n;
            remaining -= n;
        }
    }

    bool isRamping() const noexcept { return remaining > 0; }
    float getCurrent() const noexcept { return current; }
    float getTarget() const noexcept { return target; }

    /** Multiplies data by the ramp; cheap when not ramping. Returns the value at the end. */
    float applyTo (float* data, int numSamples) noexcept
    {
        if (! isRamping())
        {
            if (current != 1.0f)
                for (int i = 0; i < numSamples; ++i)
                    data[i] *= current;

            return current;
        }

        for (int i = 0; i < numSamples; ++i)
            data[i] *= next();

        return current;
    }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int length = 1, remaining = 0;
};

inline float decibelsToGain (float db, float minusInfinityDb = -100.0f) noexcept
{
    return db <= minusInfinityDb ? 0.0f : std::pow (10.0f, db * 0.05f);
}

inline float gainToDecibels (float gain, float minusInfinityDb = -100.0f) noexcept
{
    return gain > 0.0f ? std::fmax (minusInfinityDb, 20.0f * std::log10 (gain)) : minusInfinityDb;
}

} // namespace spm::engine
