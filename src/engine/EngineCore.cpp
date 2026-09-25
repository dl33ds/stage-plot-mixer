// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/EngineCore.h"

#include "core/CallbackStats.h"

#include <chrono>

namespace spm::engine
{

double monotonicSeconds() noexcept
{
    using namespace std::chrono;
    return duration<double> (steady_clock::now().time_since_epoch()).count();
}

EngineCore::EngineCore()
    : inputPointers ((size_t) maxDeviceChannels, nullptr),
      outputPointers ((size_t) maxDeviceChannels, nullptr),
      silence ((size_t) CompiledGraph::defaultMaxBlockSize, 0.0f)
{
    outputGain.reset (0.0f, 1);
}

void EngineCore::prepare (double newSampleRate, int blockSize, int, int)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    expectedBlock = blockSize;
    samplePosition = 0;
    lastCallbackTime = -1.0;

    // Always start muted-silent and fade in, even after a device restart.
    outputGain.reset (0.0f, std::max (1, (int) (sampleRate * outputFadeSeconds)));

    if (auto* graph = handoff.acquireWhileStopped())
        graph->resetProcessors();

    currentBlock.store (blockSize, std::memory_order_relaxed);
}

EngineStats EngineCore::takeStats() noexcept
{
    EngineStats s;
    s.callbacks = callbacks.load (std::memory_order_relaxed);
    s.lateCallbacks = late.load (std::memory_order_relaxed);
    s.overloads = overloads.load (std::memory_order_relaxed);
    s.cpuLoad = cpuLoad.load (std::memory_order_relaxed);
    s.peakCpuLoad = peakCpuLoad.exchange (0.0f, std::memory_order_relaxed);
    s.blockSize = currentBlock.load (std::memory_order_relaxed);
    s.sampleRate = sampleRate;
    return s;
}

void EngineCore::resetStats() noexcept
{
    late = 0;
    overloads = 0;
    peakCpuLoad = 0.0f;
}

void EngineCore::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                          int numSamples, double now) noexcept
{
    numInputs = std::min (numInputs, maxDeviceChannels);
    numOutputs = std::min (numOutputs, maxDeviceChannels);

    const auto period = (double) (expectedBlock > 0 ? expectedBlock : numSamples) / sampleRate;

    if (lastCallbackTime >= 0.0 && now - lastCallbackTime > CallbackStats::lateLimitSeconds (period))
        late.fetch_add (1, std::memory_order_relaxed);

    lastCallbackTime = now;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            std::fill_n (outputs[ch], numSamples, 0.0f);

    auto* graph = handoff.acquire();

    const auto muted = outputsMuted.load (std::memory_order_relaxed);
    outputGain.setTarget (muted ? 0.0f : 1.0f);

    // Split into chunks the graph can take; the device block can be any size.
    const auto maxChunk = graph != nullptr ? std::min (graph->getMaxBlockSize(), (int) silence.size()) : numSamples;

    for (int offset = 0; offset < numSamples;)
    {
        const auto n = std::min (maxChunk, numSamples - offset);

        for (int ch = 0; ch < numInputs; ++ch)
            inputPointers[(size_t) ch] = inputs[ch] != nullptr ? const_cast<float*> (inputs[ch]) + offset : silence.data();

        for (int ch = 0; ch < numOutputs; ++ch)
            outputPointers[(size_t) ch] = outputs[ch] != nullptr ? outputs[ch] + offset : nullptr;

        if (graph != nullptr)
        {
            ProcessContext ctx;
            ctx.sampleRate = sampleRate;
            ctx.numSamples = n;
            ctx.deviceInputs = { inputPointers.data(), numInputs, n };
            ctx.deviceOutputs = { outputPointers.data(), numOutputs, n };
            ctx.samplePosition = samplePosition;

            graph->process (ctx);
        }

        // Safe start / mute: one ramp across all outputs.
        if (outputGain.isRamping() || outputGain.getCurrent() != 1.0f)
        {
            const auto startState = outputGain;

            for (int ch = 0; ch < numOutputs; ++ch)
            {
                if (outputPointers[(size_t) ch] == nullptr)
                    continue;

                outputGain = startState;
                outputGain.applyTo (outputPointers[(size_t) ch], n);
            }

            if (numOutputs == 0)
                outputGain.skip (n);
        }

        samplePosition += n;
        offset += n;
    }

    const auto elapsed = monotonicSeconds() - now;
    const auto load = (float) (elapsed / std::max (1.0e-6, (double) numSamples / sampleRate));

    if (elapsed > (double) numSamples / sampleRate)
        overloads.fetch_add (1, std::memory_order_relaxed);

    const auto smoothed = cpuLoad.load (std::memory_order_relaxed);
    cpuLoad.store (smoothed + (load - smoothed) * 0.05f, std::memory_order_relaxed);

    if (load > peakCpuLoad.load (std::memory_order_relaxed))
        peakCpuLoad.store (load, std::memory_order_relaxed);

    currentBlock.store (numSamples, std::memory_order_relaxed);
    callbacks.fetch_add (1, std::memory_order_relaxed);
}

} // namespace spm::engine
