// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/DelayFinder.h"

#include <algorithm>
#include <cmath>

namespace spm
{

std::vector<float> makeProbeSignal (int numSamples, float amplitude, std::uint32_t seed)
{
    std::vector<float> out ((size_t) std::max (0, numSamples));
    auto state = seed != 0 ? seed : 1u;

    for (auto& s : out)
    {
        // xorshift32: tiny, deterministic across platforms and compilers.
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        s = amplitude * (((float) (state >> 8) / (float) (1u << 24) * 2.0f - 1.0f));
    }

    return out;
}

DelayEstimate findDelay (std::span<const float> probe, std::span<const float> captured, int maxLag)
{
    DelayEstimate result;
    const auto n = (int) probe.size();

    if (n == 0 || (int) captured.size() < n)
        return result;

    maxLag = std::min (maxLag, (int) captured.size() - n);

    if (maxLag < 0)
        return result;

    double probeEnergy = 0.0;
    for (auto s : probe)
        probeEnergy += (double) s * s;

    if (probeEnergy <= 0.0)
        return result;

    // Running energy of the capture window [lag, lag + n).
    double windowEnergy = 0.0;
    for (int i = 0; i < n; ++i)
        windowEnergy += (double) captured[(size_t) i] * captured[(size_t) i];

    std::vector<float> corr ((size_t) maxLag + 1, 0.0f);

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        if (lag > 0)
        {
            const double leaving = captured[(size_t) lag - 1];
            const double entering = captured[(size_t) (lag + n - 1)];
            windowEnergy = std::max (0.0, windowEnergy - leaving * leaving + entering * entering);
        }

        double dot = 0.0;
        const auto* c = captured.data() + lag;
        for (int i = 0; i < n; ++i)
            dot += (double) probe[(size_t) i] * c[i];

        const auto denom = std::sqrt (probeEnergy * windowEnergy);
        corr[(size_t) lag] = denom > 1.0e-20 ? (float) (dot / denom) : 0.0f;
    }

    int best = 0;
    for (int lag = 1; lag <= maxLag; ++lag)
        if (std::abs (corr[(size_t) lag]) > std::abs (corr[(size_t) best]))
            best = lag;

    // Strongest competing peak, ignoring the immediate neighbourhood of the best one
    // (band-limited converters smear the peak over a few samples).
    constexpr int guard = 16;
    float runnerUp = 0.0f;
    for (int lag = 0; lag <= maxLag; ++lag)
        if (std::abs (lag - best) > guard)
            runnerUp = std::max (runnerUp, std::abs (corr[(size_t) lag]));

    const auto peak = std::abs (corr[(size_t) best]);

    result.delaySamples = best;
    result.correlation = peak;
    result.peakRatio = runnerUp > 0.0f ? peak / runnerUp : (peak > 0.0f ? 1000.0f : 0.0f);
    result.inverted = corr[(size_t) best] < 0.0f;
    return result;
}

} // namespace spm
