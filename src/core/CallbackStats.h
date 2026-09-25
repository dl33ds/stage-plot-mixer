// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace spm
{

/** Collects timing and input-level statistics from inside an audio callback.

    process() is real-time safe (no allocation, no locks) and must only be called
    from the audio thread. snapshot(), takeInputPeak() and getInputPeakHold() may be
    called from any other thread while audio is running.
*/
class CallbackStats
{
public:
    static constexpr int maxChannels = 128;

    struct Snapshot
    {
        std::int64_t callbacks = 0;
        std::int64_t samples = 0;
        std::int64_t lateCallbacks = 0;     // gap between callbacks > lateThreshold x expected period
        double expectedPeriodMs = 0.0;
        double minIntervalMs = 0.0;
        double maxIntervalMs = 0.0;
        double meanIntervalMs = 0.0;
        int minBlockSize = 0;
        int maxBlockSize = 0;
    };

    /** Call before audio starts (not real-time safe with respect to process()). */
    void reset (double sampleRate, int expectedBlockSize, int numInputChannels) noexcept;

    /** Audio thread only. nowSeconds must come from a monotonic high-resolution clock. */
    void process (double nowSeconds, const float* const* inputs, int numInputs, int numSamples) noexcept;

    Snapshot snapshot() const noexcept;

    int getNumInputChannels() const noexcept { return numChannels; }

    /** Returns the peak since the previous call and resets it (for live meters). */
    float takeInputPeak (int channel) noexcept;

    /** Highest peak seen since reset(). */
    float getInputPeakHold (int channel) const noexcept;

    /** Number of callbacks in which this channel reached full scale. */
    std::int64_t getClipCount (int channel) const noexcept;

    static constexpr double lateThreshold = 1.5;
    static constexpr float clipLevel = 0.999f;

private:
    static void storeMax (std::atomic<float>& target, float value) noexcept;

    double sampleRate = 48000.0;
    int expectedBlock = 0;
    int numChannels = 0;

    double lastCallbackTime = -1.0;
    double firstCallbackTime = -1.0;

    std::atomic<std::int64_t> callbacks { 0 }, samples { 0 }, late { 0 };
    std::atomic<double> minInterval { 0.0 }, maxInterval { 0.0 }, spanSeconds { 0.0 };
    std::atomic<int> minBlock { 0 }, maxBlock { 0 };

    std::array<std::atomic<float>, maxChannels> peak {}, peakHold {};
    std::array<std::atomic<std::int64_t>, maxChannels> clips {};
};

} // namespace spm
