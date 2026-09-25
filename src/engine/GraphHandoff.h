// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/CompiledGraph.h"

#include <array>
#include <atomic>
#include <memory>

namespace spm::engine
{

/** Passes compiled graphs from the message thread to the audio thread without locks,
    and passes retired graphs back so they are never deleted on the audio thread.

    - submit() and collectGarbage(): message thread only.
    - acquire(): audio thread only.
*/
class GraphHandoff
{
public:
    GraphHandoff() = default;
    ~GraphHandoff();

    GraphHandoff (const GraphHandoff&) = delete;
    GraphHandoff& operator= (const GraphHandoff&) = delete;

    /** Queues a graph for the audio thread. A previously queued graph that the audio
        thread hasn't picked up yet is discarded.
    */
    void submit (std::unique_ptr<CompiledGraph> graph);

    /** Deletes graphs the audio thread has finished with. */
    void collectGarbage();

    /** Audio thread: switches to the newest submitted graph, if any, and returns the
        graph to run (may be null).
    */
    CompiledGraph* acquire() noexcept;

    /** Audio must be stopped. Makes any pending graph current and returns the current graph. */
    CompiledGraph* acquireWhileStopped() noexcept { return acquire(); }

private:
    bool retire (CompiledGraph* graph) noexcept;

    std::atomic<CompiledGraph*> pending { nullptr };
    CompiledGraph* current = nullptr; // audio thread

    // Single-producer (audio) / single-consumer (message) ring of retired graphs.
    static constexpr size_t retireCapacity = 64;
    std::array<std::atomic<CompiledGraph*>, retireCapacity> retired {};
    std::atomic<size_t> writeIndex { 0 }, readIndex { 0 };
};

} // namespace spm::engine
