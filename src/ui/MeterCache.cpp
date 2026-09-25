// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/MeterCache.h"

#include "engine/Smoother.h"

#include <juce_core/juce_core.h>

namespace spm::ui
{

float PortLevels::maxPeakDb() const noexcept
{
    auto result = MeterCache::floorDb;
    for (auto p : peakDb)
        result = std::max (result, p);
    return result;
}

void MeterCache::poll (graph::GraphBuilder& builder, const juce::Array<graph::NodeId>& nodes, double elapsed)
{
    std::map<std::tuple<graph::NodeId, bool, int>, PortLevels> next;
    const auto fall = (float) (24.0 * elapsed);

    for (auto id : nodes)
    {
        auto* processor = builder.getProcessor (id);
        if (processor == nullptr)
            continue;

        auto readPort = [&] (bool input, int port, engine::PortMeter& meter)
        {
            const auto key = std::make_tuple (id, input, port);
            auto& out = next[key];
            const auto previous = levels.find (key);
            const auto n = meter.getNumChannels();
            out.peakDb.resize ((size_t) n, floorDb);
            out.rmsDb.resize ((size_t) n, floorDb);

            for (int ch = 0; ch < n; ++ch)
            {
                auto& channel = meter.channel (ch);
                const auto peak = engine::gainToDecibels (channel.takePeak(), floorDb);
                const auto old = previous != levels.end() && ch < (int) previous->second.peakDb.size()
                                     ? previous->second.peakDb[(size_t) ch] : floorDb;
                out.peakDb[(size_t) ch] = std::max (peak, old - fall);
                out.rmsDb[(size_t) ch] = engine::gainToDecibels (channel.getRms(), floorDb);
                out.clipped = out.clipped || channel.hasClipped();
            }
        };

        for (int p = 0; p < processor->getNumInputPorts(); ++p)
            readPort (true, p, processor->getInputMeter (p));

        for (int p = 0; p < processor->getNumOutputPorts(); ++p)
            readPort (false, p, processor->getOutputMeter (p));
    }

    levels = std::move (next);
}

const PortLevels* MeterCache::get (graph::NodeId node, bool input, int port) const
{
    const auto it = levels.find (std::make_tuple (node, input, port));
    return it != levels.end() ? &it->second : nullptr;
}

void MeterCache::alias (graph::NodeId node, bool input, int port, graph::NodeId fromNode, bool fromInput, int fromPort)
{
    if (const auto* from = get (fromNode, fromInput, fromPort))
    {
        auto copy = *from;
        levels[std::make_tuple (node, input, port)] = std::move (copy);
    }
}

void MeterCache::clearClips (graph::GraphBuilder& builder, graph::NodeId node)
{
    if (auto* processor = builder.getProcessor (node))
    {
        auto clear = [] (engine::PortMeter& meter)
        {
            for (int ch = 0; ch < meter.getNumChannels(); ++ch)
                meter.channel (ch).clearClip();
        };

        for (int p = 0; p < processor->getNumInputPorts(); ++p)
            clear (processor->getInputMeter (p));
        for (int p = 0; p < processor->getNumOutputPorts(); ++p)
            clear (processor->getOutputMeter (p));
    }
}

} // namespace spm::ui
