// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "engine/GraphHandoff.h"

namespace spm::engine
{

GraphHandoff::~GraphHandoff()
{
    // Audio has stopped by now.
    collectGarbage();
    delete pending.exchange (nullptr);
    delete current;
}

void GraphHandoff::submit (std::unique_ptr<CompiledGraph> graph)
{
    // If the audio thread hadn't taken the previous graph yet, it never will: exchange
    // is atomic, so whichever side gets the pointer owns it.
    delete pending.exchange (graph.release(), std::memory_order_acq_rel);
    collectGarbage();
}

void GraphHandoff::collectGarbage()
{
    auto read = readIndex.load (std::memory_order_relaxed);

    while (read != writeIndex.load (std::memory_order_acquire))
    {
        delete retired[read % retireCapacity].exchange (nullptr, std::memory_order_relaxed);
        readIndex.store (++read, std::memory_order_release);
    }
}

bool GraphHandoff::retire (CompiledGraph* graph) noexcept
{
    const auto write = writeIndex.load (std::memory_order_relaxed);

    if (write - readIndex.load (std::memory_order_acquire) >= retireCapacity)
        return false;

    retired[write % retireCapacity].store (graph, std::memory_order_relaxed);
    writeIndex.store (write + 1, std::memory_order_release);
    return true;
}

CompiledGraph* GraphHandoff::acquire() noexcept
{
    // Only swap if there's room to retire the old graph; otherwise try next block.
    if (pending.load (std::memory_order_relaxed) != nullptr
        && (current == nullptr || writeIndex.load (std::memory_order_relaxed) - readIndex.load (std::memory_order_acquire) < retireCapacity))
    {
        if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (current != nullptr)
                retire (current);

            current = next;
        }
    }

    return current;
}

} // namespace spm::engine
