// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/CompiledGraph.h"

namespace spm::engine
{

CompiledGraph::CompiledGraph (int maxBlockSize)
    : maxBlock (std::max (16, maxBlockSize)),
      rampScratch ((size_t) maxBlock, 0.0f)
{
}

int CompiledGraph::addBuffer (int numChannels)
{
    buffers.push_back (std::make_unique<ChannelBuffer> (std::max (0, numChannels), maxBlock));
    return (int) buffers.size() - 1;
}

void CompiledGraph::addNode (Node node)
{
    std::vector<ChannelSpan> ins, outs;

    for (auto index : node.inputBuffers)
        ins.push_back (buffers[(size_t) index]->span (maxBlock));

    for (auto index : node.outputBuffers)
        outs.push_back (buffers[(size_t) index]->span (maxBlock));

    // Spans need at least one element so data() is valid for nodes without ports.
    if (ins.empty())  ins.push_back ({});
    if (outs.empty()) outs.push_back ({});

    inputSpans.push_back (std::move (ins));
    outputSpans.push_back (std::move (outs));
    nodes.push_back (std::move (node));
}

void CompiledGraph::resetProcessors() noexcept
{
    for (auto& node : nodes)
        node.processor->reset();
}

void CompiledGraph::mixWire (WireInput& wire, const ChannelSpan& destination, double sampleRate) noexcept
{
    auto source = buffers[(size_t) wire.sourceBuffer]->span (destination.numSamples);

    // The delay line runs even while the wire is silent, so it never replays old audio.
    if (wire.state->getDelaySamples() > 0)
        source = wire.state->delayBlock (source);

    if (! wire.state->beginBlock (sampleRate))
    {
        wire.state->getSmoother().skip (destination.numSamples);
        return;
    }

    const auto n = destination.numSamples;
    auto& smoother = wire.state->getSmoother();

    // Gains for this block: one shared ramp for all channels.
    const auto ramping = smoother.isRamping();
    float constantGain = smoother.getCurrent();

    if (ramping)
    {
        for (int i = 0; i < n; ++i)
            rampScratch[(size_t) i] = smoother.next();
    }

    auto addChannel = [&] (const float* src, float* dst, float scale)
    {
        if (ramping)
        {
            for (int i = 0; i < n; ++i)
                dst[i] += src[i] * rampScratch[(size_t) i] * scale;
        }
        else
        {
            const auto g = constantGain * scale;
            for (int i = 0; i < n; ++i)
                dst[i] += src[i] * g;
        }
    };

    const auto srcCh = source.numChannels;
    const auto dstCh = destination.numChannels;

    if (srcCh == 0 || dstCh == 0)
        return;

    if (srcCh == 1 && dstCh > 1)
    {
        // Mono into a wider port: copy to every channel.
        for (int ch = 0; ch < dstCh; ++ch)
            addChannel (source.channel (0), destination.channel (ch), 1.0f);
    }
    else if (dstCh == 1 && srcCh > 1)
    {
        // Wider into mono: average, so a stereo signal keeps its level.
        const auto scale = 1.0f / (float) srcCh;
        for (int ch = 0; ch < srcCh; ++ch)
            addChannel (source.channel (ch), destination.channel (0), scale);
    }
    else
    {
        // Channel by channel; extra channels on either side are left out.
        const auto common = std::min (srcCh, dstCh);
        for (int ch = 0; ch < common; ++ch)
            addChannel (source.channel (ch), destination.channel (ch), 1.0f);
    }
}

void CompiledGraph::process (const ProcessContext& ctx) noexcept
{
    const auto n = std::min (ctx.numSamples, maxBlock);
    const auto rmsCoefficient = rmsCoefficientFor (ctx.sampleRate, n);

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        auto& node = nodes[i];
        auto& ins = inputSpans[i];
        auto& outs = outputSpans[i];

        for (size_t port = 0; port < node.inputBuffers.size(); ++port)
        {
            auto& span = ins[port];
            span.numSamples = n;
            span.clear();

            for (auto& wire : node.wiresPerInput[port])
                mixWire (wire, span, ctx.sampleRate);

            node.processor->getInputMeter ((int) port).update (span, rmsCoefficient);
        }

        for (auto& span : outs)
            span.numSamples = n;

        node.processor->process (ctx, ins.data(), outs.data());

        for (size_t port = 0; port < node.outputBuffers.size(); ++port)
            node.processor->getOutputMeter ((int) port).update (outs[port], rmsCoefficient);
    }
}

} // namespace spm::engine
