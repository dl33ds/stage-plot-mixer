// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace spm
{

/** Generates a deterministic white-noise burst in [-amplitude, amplitude].
    Used as the probe signal for round-trip latency measurement: noise has a
    sharp autocorrelation peak, so its delay can be found to the exact sample.
*/
std::vector<float> makeProbeSignal (int numSamples, float amplitude, std::uint32_t seed = 0x5eed1234u);

struct DelayEstimate
{
    /** Lag in samples at which the probe best matches the capture, or -1 if not found. */
    int delaySamples = -1;

    /** Normalised correlation at the best lag, 0..1. Values above ~0.5 indicate a clean match. */
    float correlation = 0.0f;

    /** Best peak divided by the strongest peak elsewhere. Above ~3 means the result is unambiguous. */
    float peakRatio = 0.0f;

    /** True if the captured signal is polarity-inverted relative to the probe. */
    bool inverted = false;

    bool isReliable() const noexcept { return delaySamples >= 0 && correlation > 0.3f && peakRatio > 2.0f; }
};

/** Finds the lag L in [0, maxLag] that maximises the normalised cross-correlation
    between probe[i] and captured[L + i].
*/
DelayEstimate findDelay (std::span<const float> probe, std::span<const float> captured, int maxLag);

} // namespace spm
