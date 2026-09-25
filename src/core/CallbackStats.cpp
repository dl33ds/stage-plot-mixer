// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/CallbackStats.h"

#include <algorithm>
#include <cmath>

namespace spm
{

void CallbackStats::reset (double newSampleRate, int expectedBlockSize, int numInputChannels) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    expectedBlock = std::max (0, expectedBlockSize);
    numChannels = std::clamp (numInputChannels, 0, maxChannels);

    lastCallbackTime = -1.0;
    firstCallbackTime = -1.0;

    callbacks = 0;
    samples = 0;
    late = 0;
    minInterval = 0.0;
    maxInterval = 0.0;
    spanSeconds = 0.0;
    minBlock = 0;
    maxBlock = 0;

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        peak[(size_t) ch] = 0.0f;
        peakHold[(size_t) ch] = 0.0f;
        clips[(size_t) ch] = 0;
    }
}

void CallbackStats::storeMax (std::atomic<float>& target, float value) noexcept
{
    auto current = target.load (std::memory_order_relaxed);
    while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed))
    {
    }
}

void CallbackStats::process (double now, const float* const* inputs, int numInputs, int numSamples) noexcept
{
    const auto count = callbacks.load (std::memory_order_relaxed);

    if (count == 0)
    {
        minBlock.store (numSamples, std::memory_order_relaxed);
        maxBlock.store (numSamples, std::memory_order_relaxed);
        firstCallbackTime = now;
    }
    else
    {
        minBlock.store (std::min (minBlock.load (std::memory_order_relaxed), numSamples), std::memory_order_relaxed);
        maxBlock.store (std::max (maxBlock.load (std::memory_order_relaxed), numSamples), std::memory_order_relaxed);
    }

    if (lastCallbackTime >= 0.0)
    {
        const auto interval = now - lastCallbackTime;
        const auto expectedPeriod = (double) (expectedBlock > 0 ? expectedBlock : numSamples) / sampleRate;

        if (count == 1)
        {
            minInterval.store (interval, std::memory_order_relaxed);
            maxInterval.store (interval, std::memory_order_relaxed);
        }
        else
        {
            minInterval.store (std::min (minInterval.load (std::memory_order_relaxed), interval), std::memory_order_relaxed);
            maxInterval.store (std::max (maxInterval.load (std::memory_order_relaxed), interval), std::memory_order_relaxed);
        }

        if (interval > expectedPeriod * lateThreshold)
            late.fetch_add (1, std::memory_order_relaxed);

        spanSeconds.store (now - firstCallbackTime, std::memory_order_relaxed);
    }

    lastCallbackTime = now;

    const auto channels = std::min (numInputs, numChannels);
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* data = inputs[ch];
        if (data == nullptr)
            continue;

        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            blockPeak = std::max (blockPeak, std::abs (data[i]));

        storeMax (peak[(size_t) ch], blockPeak);
        storeMax (peakHold[(size_t) ch], blockPeak);

        if (blockPeak >= clipLevel)
            clips[(size_t) ch].fetch_add (1, std::memory_order_relaxed);
    }

    samples.fetch_add (numSamples, std::memory_order_relaxed);
    callbacks.store (count + 1, std::memory_order_release);
}

CallbackStats::Snapshot CallbackStats::snapshot() const noexcept
{
    Snapshot s;
    s.callbacks = callbacks.load (std::memory_order_acquire);
    s.samples = samples.load (std::memory_order_relaxed);
    s.lateCallbacks = late.load (std::memory_order_relaxed);
    s.expectedPeriodMs = expectedBlock > 0 ? 1000.0 * expectedBlock / sampleRate : 0.0;
    s.minIntervalMs = 1000.0 * minInterval.load (std::memory_order_relaxed);
    s.maxIntervalMs = 1000.0 * maxInterval.load (std::memory_order_relaxed);
    s.meanIntervalMs = s.callbacks > 1 ? 1000.0 * spanSeconds.load (std::memory_order_relaxed) / (double) (s.callbacks - 1) : 0.0;
    s.minBlockSize = minBlock.load (std::memory_order_relaxed);
    s.maxBlockSize = maxBlock.load (std::memory_order_relaxed);
    return s;
}

float CallbackStats::takeInputPeak (int channel) noexcept
{
    if (channel < 0 || channel >= maxChannels)
        return 0.0f;

    return peak[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
}

float CallbackStats::getInputPeakHold (int channel) const noexcept
{
    if (channel < 0 || channel >= maxChannels)
        return 0.0f;

    return peakHold[(size_t) channel].load (std::memory_order_relaxed);
}

std::int64_t CallbackStats::getClipCount (int channel) const noexcept
{
    if (channel < 0 || channel >= maxChannels)
        return 0;

    return clips[(size_t) channel].load (std::memory_order_relaxed);
}

} // namespace spm
