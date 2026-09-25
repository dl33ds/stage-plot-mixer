// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/EngineCore.h"
#include "graph/GraphBuilder.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace spm::test
{

/** Runs EngineCore offline with fake device buffers, as the audio device would. */
struct EngineRig
{
    EngineRig (int inputs, int outputs, double rate = 48000.0, int block = 128)
        : numInputs (inputs), numOutputs (outputs), sampleRate (rate), blockSize (block)
    {
        core.prepare (sampleRate, blockSize, numInputs, numOutputs);
    }

    void submit (const graph::GraphDesc& desc) { core.submit (builder.build (desc)); }

    /** Input generator: (channel, absolute sample index) → value. */
    std::function<float (int, std::int64_t)> input = [] (int ch, std::int64_t i)
    {
        return (float) std::sin ((double) i * 0.01 * (ch + 1)) * 0.5f;
    };

    /** Processes numSamples, appending every output channel to `recorded`. */
    void run (int numSamples)
    {
        std::vector<std::vector<float>> in ((size_t) numInputs, std::vector<float> ((size_t) blockSize));
        std::vector<std::vector<float>> out ((size_t) numOutputs, std::vector<float> ((size_t) blockSize));
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& c : in)  inPtrs.push_back (c.data());
        for (auto& c : out) outPtrs.push_back (c.data());

        recorded.resize ((size_t) numOutputs);

        for (int done = 0; done < numSamples; done += blockSize)
        {
            const auto n = std::min (blockSize, numSamples - done);

            for (int ch = 0; ch < numInputs; ++ch)
                for (int i = 0; i < n; ++i)
                    in[(size_t) ch][(size_t) i] = input (ch, position + i);

            core.process (inPtrs.data(), numInputs, outPtrs.data(), numOutputs, n, clock);
            clock += (double) n / sampleRate;
            position += n;

            for (int ch = 0; ch < numOutputs; ++ch)
                recorded[(size_t) ch].insert (recorded[(size_t) ch].end(), out[(size_t) ch].begin(), out[(size_t) ch].begin() + n);
        }

        core.collectGarbage();
    }

    /** Lets fades finish and removes them from the running graph. */
    void settle()
    {
        run (std::max (blockSize * 8, (int) (sampleRate * 0.1)));

        if (builder.pruneFinishedFades (true))
            core.submit (builder.rebuild());

        run (blockSize);
    }

    void clearRecording() { recorded.assign ((size_t) numOutputs, {}); }

    int numInputs, numOutputs;
    double sampleRate;
    int blockSize;
    engine::EngineCore core;
    graph::GraphBuilder builder;
    std::vector<std::vector<float>> recorded;
    std::int64_t position = 0;
    double clock = 1000.0;
};

/** Largest sample-to-sample step: a click shows up as a jump. */
inline float largestStep (const std::vector<float>& signal, size_t from = 1)
{
    float largest = 0.0f;
    for (auto i = std::max<size_t> (1, from); i < signal.size(); ++i)
        largest = std::max (largest, std::abs (signal[i] - signal[i - 1]));
    return largest;
}

inline graph::NodeDesc node (graph::NodeId id, std::string_view type, nodes::ParamValues params = {})
{
    const auto* t = nodes::NodeRegistry::builtIn().find (type);
    auto values = t->defaults();
    for (size_t i = 0; i < params.size() && i < values.size(); ++i)
        values[i] = params[i];
    return { id, std::string (type), values };
}

inline graph::WireDesc wire (graph::WireId id, graph::NodeId src, int srcPort, graph::NodeId dst, int dstPort, float gain = 1.0f)
{
    return { id, src, srcPort, dst, dstPort, gain };
}

} // namespace spm::test
