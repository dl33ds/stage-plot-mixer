// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/GraphHandoff.h"
#include "engine/Smoother.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace spm::engine
{

/** Live engine health figures. Written by the audio thread, read anywhere. */
struct EngineStats
{
    std::int64_t callbacks = 0;
    std::int64_t lateCallbacks = 0;  // gap between callbacks well over one buffer period
    std::int64_t overloads = 0;      // processing took longer than one buffer period
    float cpuLoad = 0.0f;            // smoothed fraction of the buffer period spent processing
    float peakCpuLoad = 0.0f;        // highest since the last takeStats()
    int blockSize = 0;
    double sampleRate = 0.0;
};

/** Device-independent heart of the audio engine.

    The device layer (AudioEngine, or a test harness) calls prepare() before audio starts
    and process() for each block. Graphs arrive through submit(). Outputs start muted
    (safe start) and fade in when unmuted.
*/
class EngineCore
{
public:
    EngineCore();

    // Message thread ----------------------------------------------------------
    void submit (std::unique_ptr<CompiledGraph> graph) { handoff.submit (std::move (graph)); }
    void collectGarbage() { handoff.collectGarbage(); }

    void setOutputsMuted (bool shouldMute) noexcept { outputsMuted.store (shouldMute, std::memory_order_relaxed); }
    bool areOutputsMuted() const noexcept { return outputsMuted.load (std::memory_order_relaxed); }

    /** Snapshot of the counters; resets the peak CPU figure. */
    EngineStats takeStats() noexcept;

    /** Clears counters (e.g. when the user presses "reset" in the status bar). */
    void resetStats() noexcept;

    // Device layer, while audio is stopped -----------------------------------
    void prepare (double sampleRate, int blockSize, int numInputs, int numOutputs);
    void released() noexcept {}

    // Audio thread ----------------------------------------------------------------
    /** nowSeconds: monotonic clock reading at the start of the callback. */
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                  int numSamples, double nowSeconds) noexcept;

    static constexpr int maxDeviceChannels = 256;
    static constexpr double outputFadeSeconds = 0.05;

private:
    GraphHandoff handoff;
    std::atomic<bool> outputsMuted { true };

    double sampleRate = 48000.0;
    int expectedBlock = 0;

    // Audio thread state.
    Smoother outputGain;
    std::int64_t samplePosition = 0;
    double lastCallbackTime = -1.0;
    std::vector<float*> inputPointers, outputPointers;
    std::vector<float> silence;

    std::atomic<std::int64_t> callbacks { 0 }, late { 0 }, overloads { 0 };
    std::atomic<float> cpuLoad { 0.0f }, peakCpuLoad { 0.0f };
    std::atomic<int> currentBlock { 0 };
};

/** Seconds from a monotonic high-resolution clock. */
double monotonicSeconds() noexcept;

} // namespace spm::engine
