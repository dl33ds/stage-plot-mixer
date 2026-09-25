// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/NodeProcessor.h"

#include <memory>
#include <vector>

namespace spm::engine
{

/** An immutable, ready-to-run processing plan: nodes in dependency order, with every
    buffer preallocated. Built on the message thread, run on the audio thread, and
    deleted on the message thread (see GraphHandoff).
*/
class CompiledGraph
{
public:
    struct WireInput
    {
        std::shared_ptr<WireState> state;
        int sourceBuffer = -1;   // index into the buffer pool
    };

    struct Node
    {
        std::shared_ptr<NodeProcessor> processor;
        std::vector<int> inputBuffers, outputBuffers;       // one buffer per port
        std::vector<std::vector<WireInput>> wiresPerInput;  // wires summed into each input port
    };

    /** maxBlockSize: the largest block process() accepts; callers split larger ones. */
    explicit CompiledGraph (int maxBlockSize = defaultMaxBlockSize);

    /** Building (message thread). Returns the buffer index. */
    int addBuffer (int numChannels);
    void addNode (Node node);

    /** Audio thread. ctx.numSamples must be <= getMaxBlockSize(). */
    void process (const ProcessContext& ctx) noexcept;

    /** Called while audio is stopped. */
    void resetProcessors() noexcept;

    int getMaxBlockSize() const noexcept { return maxBlock; }
    int getNumNodes() const noexcept { return (int) nodes.size(); }
    const std::vector<Node>& getNodes() const noexcept { return nodes; }

    static constexpr int defaultMaxBlockSize = 1024;

private:
    void mixWire (WireInput& wire, const ChannelSpan& destination, double sampleRate) noexcept;

    int maxBlock;
    std::vector<std::unique_ptr<ChannelBuffer>> buffers;
    std::vector<Node> nodes;

    // Per-node span arrays, rebuilt in size only at build time.
    std::vector<std::vector<ChannelSpan>> inputSpans, outputSpans;
    std::vector<float> rampScratch;
};

} // namespace spm::engine
