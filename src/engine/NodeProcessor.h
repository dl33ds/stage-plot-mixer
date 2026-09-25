// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/Buffers.h"
#include "engine/Meters.h"
#include "engine/Smoother.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace spm::engine
{

/** Everything a node may need during one block. */
struct ProcessContext
{
    double sampleRate = 48000.0;
    int numSamples = 0;
    ChannelSpan deviceInputs;   // read only
    ChannelSpan deviceOutputs;  // hardware output nodes add into these
    std::int64_t samplePosition = 0;
};

/** The audio-thread half of a node.

    A processor lives as long as its node keeps the same type and port layout, and is
    shared between successive compiled graphs, so its state (filters, smoothers, meters)
    carries across wiring changes. It is created and destroyed on the message thread.

    Parameters are atomics: the message thread writes them at any time and process()
    reads (and smooths) them.
*/
class NodeProcessor
{
public:
    NodeProcessor (std::vector<int> inputPortChannels, std::vector<int> outputPortChannels, int numParameters)
        : inputChannels (std::move (inputPortChannels)),
          outputChannels (std::move (outputPortChannels)),
          params (std::make_unique<std::atomic<float>[]> ((size_t) std::max (1, numParameters))),
          numParams (numParameters)
    {
        for (auto n : inputChannels)
            inputMeters.push_back (std::make_unique<PortMeter> (n));

        for (auto n : outputChannels)
            outputMeters.push_back (std::make_unique<PortMeter> (n));
    }

    virtual ~NodeProcessor() = default;

    NodeProcessor (const NodeProcessor&) = delete;
    NodeProcessor& operator= (const NodeProcessor&) = delete;

    int getNumInputPorts() const noexcept { return (int) inputChannels.size(); }
    int getNumOutputPorts() const noexcept { return (int) outputChannels.size(); }
    int getInputChannels (int port) const noexcept { return inputChannels[(size_t) port]; }
    int getOutputChannels (int port) const noexcept { return outputChannels[(size_t) port]; }

    /** Any thread. */
    void setParameter (int index, float value) noexcept
    {
        if (index >= 0 && index < numParams)
            params[(size_t) index].store (value, std::memory_order_relaxed);
    }

    float getParameter (int index) const noexcept
    {
        return index >= 0 && index < numParams ? params[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
    }

    /** Audio thread. inputs/outputs have one span per port, sized to ctx.numSamples.
        Output buffers hold garbage on entry and must be fully written.
    */
    virtual void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept = 0;

    /** Clears internal state. Called while audio is stopped (device restart). */
    virtual void reset() noexcept {}

    /** Samples of delay this node adds (for path delay compensation, later). */
    virtual int getLatencySamples() const noexcept { return 0; }

    PortMeter& getInputMeter (int port) noexcept { return *inputMeters[(size_t) port]; }
    PortMeter& getOutputMeter (int port) noexcept { return *outputMeters[(size_t) port]; }

protected:
    float param (int index) const noexcept { return getParameter (index); }

private:
    std::vector<int> inputChannels, outputChannels;
    std::unique_ptr<std::atomic<float>[]> params;
    int numParams = 0;
    std::vector<std::unique_ptr<PortMeter>> inputMeters, outputMeters;
};

/** Persistent state of one wire, shared between compiled graphs.

    A new wire fades in; a disconnected wire fades out (it stays in the compiled graph
    until the audio thread reports it silent), so wiring changes never click.
*/
class WireState
{
public:
    explicit WireState (float initialGain = 1.0f) : gain (initialGain) {}

    /** Message thread. */
    void setGain (float linearGain) noexcept { gain.store (linearGain, std::memory_order_relaxed); }
    float getGain() const noexcept { return gain.load (std::memory_order_relaxed); }

    /** Message thread. false starts a fade-out. */
    void setConnected (bool shouldBeConnected) noexcept
    {
        faded.store (false, std::memory_order_relaxed);
        connected.store (shouldBeConnected, std::memory_order_relaxed);
    }

    bool isConnected() const noexcept { return connected.load (std::memory_order_relaxed); }

    /** True once a disconnected wire has fully faded out. */
    bool hasFadedOut() const noexcept { return faded.load (std::memory_order_acquire); }

    /** Audio thread: updates the ramp for this block and returns false if the wire is silent. */
    bool beginBlock (double sampleRate) noexcept
    {
        const auto target = connected.load (std::memory_order_relaxed) ? gain.load (std::memory_order_relaxed) : 0.0f;

        if (! started)
        {
            smoother.reset (0.0f, rampLength (sampleRate));
            started = true;
        }

        smoother.setTarget (target);

        const auto silent = ! smoother.isRamping() && smoother.getCurrent() == 0.0f;

        if (silent && ! connected.load (std::memory_order_relaxed))
            faded.store (true, std::memory_order_release);

        return ! silent;
    }

    Smoother& getSmoother() noexcept { return smoother; }

    static constexpr double fadeSeconds = 0.010;

private:
    static int rampLength (double sampleRate) noexcept { return std::max (1, (int) (sampleRate * fadeSeconds)); }

    std::atomic<float> gain;
    std::atomic<bool> connected { true }, faded { false };

    // Audio thread only.
    Smoother smoother;
    bool started = false;
};

} // namespace spm::engine
