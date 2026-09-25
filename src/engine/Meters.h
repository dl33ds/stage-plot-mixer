// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/Buffers.h"

#include <atomic>
#include <cmath>
#include <memory>

namespace spm::engine
{

/** Lock-free level measurement for one channel.
    The audio thread writes; any other thread reads. Peaks are "take and reset" so the
    UI sees every peak between two frames.
*/
class MeterChannel
{
public:
    void update (const float* data, int numSamples, float rmsCoefficient) noexcept
    {
        float blockPeak = 0.0f;
        double sumSquares = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto v = data[i];
            blockPeak = std::fmax (blockPeak, std::fabs (v));
            sumSquares += (double) v * v;
        }

        auto current = peak.load (std::memory_order_relaxed);
        while (blockPeak > current && ! peak.compare_exchange_weak (current, blockPeak, std::memory_order_relaxed))
        {
        }

        // One-pole smoothing of the mean square, updated once per block.
        const auto meanSquare = numSamples > 0 ? (float) (sumSquares / numSamples) : 0.0f;
        meanSquareState += (meanSquare - meanSquareState) * rmsCoefficient;
        rms.store (std::sqrt (meanSquareState), std::memory_order_relaxed);

        if (blockPeak >= clipLevel)
            clipped.store (true, std::memory_order_relaxed);
    }

    /** Peak since the previous call (linear gain). */
    float takePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }

    float getRms() const noexcept { return rms.load (std::memory_order_relaxed); }

    bool hasClipped() const noexcept { return clipped.load (std::memory_order_relaxed); }
    void clearClip() noexcept { clipped.store (false, std::memory_order_relaxed); }

    static constexpr float clipLevel = 0.999f;

private:
    std::atomic<float> peak { 0.0f }, rms { 0.0f };
    std::atomic<bool> clipped { false };
    float meanSquareState = 0.0f; // audio thread only
};

/** Meters for every channel of one port. Allocated once, on the message thread. */
class PortMeter
{
public:
    explicit PortMeter (int numChannels)
        : channels (std::make_unique<MeterChannel[]> ((size_t) numChannels)), count (numChannels)
    {
    }

    void update (const ChannelSpan& block, float rmsCoefficient) noexcept
    {
        const auto n = std::min (count, block.numChannels);
        for (int ch = 0; ch < n; ++ch)
            channels[(size_t) ch].update (block.channel (ch), block.numSamples, rmsCoefficient);
    }

    int getNumChannels() const noexcept { return count; }
    MeterChannel& channel (int index) noexcept { return channels[(size_t) index]; }

    /** Highest peak across all channels since the last call; for activity indicators. */
    float takeMaxPeak() noexcept
    {
        float result = 0.0f;
        for (int ch = 0; ch < count; ++ch)
            result = std::fmax (result, channels[(size_t) ch].takePeak());
        return result;
    }

private:
    std::unique_ptr<MeterChannel[]> channels;
    int count = 0;
};

/** Coefficient for a one-pole RMS follower updated once per block. */
inline float rmsCoefficientFor (double sampleRate, int blockSize, double timeConstantSeconds = 0.3) noexcept
{
    if (sampleRate <= 0.0 || blockSize <= 0)
        return 1.0f;

    return (float) (1.0 - std::exp (-(double) blockSize / (timeConstantSeconds * sampleRate)));
}

} // namespace spm::engine
