// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// spm-soak: runs the engine faster than real time with N channels passing straight through
// while another thread keeps editing the graph, and checks every output sample is
// bit-exact. The simulated-audio length is set with --minutes (default 60).

#include "engine/EngineCore.h"
#include "graph/GraphBuilder.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace spm;
namespace types = nodes::types;

namespace
{

struct Options
{
    double minutes = 60.0;
    int channels = 32;
    int block = 128;
    double rate = 48000.0;
};

/** Deterministic test signal: a different noise sequence per channel. */
float inputSample (int channel, std::int64_t index) noexcept
{
    auto x = (std::uint64_t) index * 0x9E3779B97F4A7C15ull + (std::uint64_t) channel * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 31;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 29;
    return (float) (x >> 40) / (float) (1u << 24) - 0.5f;
}

graph::NodeDesc makeNode (graph::NodeId id, std::string_view type, nodes::ParamValues params)
{
    const auto* t = nodes::NodeRegistry::builtIn().find (type);
    auto values = t->defaults();
    for (size_t i = 0; i < params.size() && i < values.size(); ++i)
        values[i] = params[i];
    return { id, std::string (type), values };
}

/** The pass-through part that must stay bit-exact: in k → gain (0 dB) → out k. */
graph::GraphDesc passthrough (int channels)
{
    graph::GraphDesc d;
    graph::WireId nextWire = 1;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto base = (graph::NodeId) (ch + 1) * 10;
        d.nodes.push_back (makeNode (base, types::hardwareInput, { (float) ch + 1, 1 }));
        d.nodes.push_back (makeNode (base + 1, types::gain, { 0, 0, 0, 1 }));
        d.nodes.push_back (makeNode (base + 2, types::hardwareOutput, { (float) ch + 1, 1 }));
        d.wires.push_back ({ nextWire++, base, 0, base + 1, 0, 1.0f });
        d.wires.push_back ({ nextWire++, base + 1, 0, base + 2, 0, 1.0f });
    }

    return d;
}

/** Random edits that must not affect the pass-through: side nodes tapping the inputs,
    buses, meters and generators coming and going, and structural changes to them.
*/
void randomEdit (graph::GraphDesc& d, const graph::GraphDesc& base, int channels, std::mt19937& rng)
{
    auto pick = [&rng] (int n) { return std::uniform_int_distribution<int> (0, n - 1) (rng); };
    static graph::NodeId nextNode = 100000;
    static graph::WireId nextWire = 100000;

    const auto sideNodes = (int) (d.nodes.size() - base.nodes.size());

    // Keep the graph a realistic size: past 64 side nodes, remove one instead.
    switch (sideNodes >= 64 ? 2 : pick (5))
    {
        case 0:
        case 1:
        {
            // Tap an input into a new bus or meter.
            const auto id = nextNode++;
            const auto source = (graph::NodeId) (pick (channels) + 1) * 10;
            d.nodes.push_back (pick (2) == 0 ? makeNode (id, types::bus, { 0, 0, (float) (1 + pick (4)) })
                                             : makeNode (id, types::meter, { (float) (1 + pick (2)) }));
            d.wires.push_back ({ nextWire++, source, 0, id, 0, 1.0f });

            if (pick (3) == 0)
            {
                const auto gen = nextNode++;
                d.nodes.push_back (makeNode (gen, types::testGenerator, { (float) pick (3), 440, -20, 1, 1 }));
                d.wires.push_back ({ nextWire++, gen, 0, id, 0, 0.5f });
            }
            break;
        }

        case 2:
            // Remove a side node (its wires go with it).
            if (sideNodes > 0)
            {
                const auto index = base.nodes.size() + (size_t) pick (sideNodes);
                const auto id = d.nodes[index].id;
                d.nodes.erase (d.nodes.begin() + (std::ptrdiff_t) index);
                std::erase_if (d.wires, [id] (const auto& w) { return w.sourceNode == id || w.destNode == id; });
            }
            break;

        case 3:
            // Structural change to a side node.
            if (sideNodes > 0)
            {
                auto& n = d.nodes[base.nodes.size() + (size_t) pick (sideNodes)];
                if (n.type == types::bus)
                    n.params[2] = (float) (1 + pick (4));
                else if (n.type == types::meter)
                    n.params[0] = (float) (1 + pick (2));
            }
            break;

        default:
            // Non-structural parameter changes on the pass-through gains (still 0 dB).
            for (auto& n : d.nodes)
                if (n.type == types::gain)
                    n.params[0] = 0.0f;
            break;
    }
}

bool parse (int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&] { return i + 1 < argc ? std::atof (argv[++i]) : 0.0; };

        if (arg == "--minutes")       o.minutes = next();
        else if (arg == "--channels") o.channels = (int) next();
        else if (arg == "--block")    o.block = (int) next();
        else if (arg == "--rate")     o.rate = next();
        else
        {
            std::printf ("Usage: spm-soak [--minutes 60] [--channels 32] [--block 128] [--rate 48000]\n");
            return false;
        }
    }

    return o.minutes > 0 && o.channels > 0 && o.channels <= 64 && o.block > 0 && o.rate > 0;
}

} // namespace

int main (int argc, char** argv)
{
    Options options;
    if (! parse (argc, argv, options))
        return 2;

    const auto totalSamples = (std::int64_t) (options.minutes * 60.0 * options.rate);
    const auto checkFrom = (std::int64_t) (options.rate * 0.1);  // after the safe-start fade-in

    std::printf ("spm-soak: %d channels, %d-sample blocks, %.0f Hz, %.1f minutes of audio\n",
                 options.channels, options.block, options.rate, options.minutes);

    engine::EngineCore core;
    graph::GraphBuilder builder;

    const auto base = passthrough (options.channels);
    auto desc = base;

    core.prepare (options.rate, options.block, options.channels, options.channels);
    core.submit (builder.build (desc));
    core.setOutputsMuted (false);

    std::atomic<std::int64_t> position { 0 };
    std::atomic<bool> finished { false };
    std::atomic<std::int64_t> mismatches { 0 }, firstMismatch { -1 };

    std::thread audio ([&]
    {
        std::vector<std::vector<float>> in ((size_t) options.channels, std::vector<float> ((size_t) options.block));
        std::vector<std::vector<float>> out ((size_t) options.channels, std::vector<float> ((size_t) options.block));
        std::vector<const float*> inPtrs;
        std::vector<float*> outPtrs;
        for (auto& c : in)  inPtrs.push_back (c.data());
        for (auto& c : out) outPtrs.push_back (c.data());

        double clock = 0.0;

        for (std::int64_t done = 0; done < totalSamples; done += options.block)
        {
            for (int ch = 0; ch < options.channels; ++ch)
                for (int i = 0; i < options.block; ++i)
                    in[(size_t) ch][(size_t) i] = inputSample (ch, done + i);

            // Pretend callbacks arrive exactly on time.
            core.process (inPtrs.data(), options.channels, outPtrs.data(), options.channels, options.block, clock);
            clock += options.block / options.rate;

            for (int ch = 0; ch < options.channels; ++ch)
                for (int i = 0; i < options.block; ++i)
                    if (done + i >= checkFrom && out[(size_t) ch][(size_t) i] != in[(size_t) ch][(size_t) i])
                    {
                        if (mismatches.fetch_add (1) == 0)
                            firstMismatch = done + i;
                    }

            position.store (done + options.block, std::memory_order_relaxed);
        }

        finished = true;
    });

    // Editing thread (this one, acting as the message thread).
    std::mt19937 rng (12345);
    std::int64_t edits = 0, rebuilds = 0, nextEditAt = 0, nextReportAt = 0;
    int maxFading = 0;
    const auto editInterval = (std::int64_t) (options.rate * 0.05);  // an edit every 50 ms of audio
    const auto started = std::chrono::steady_clock::now();

    while (! finished)
    {
        const auto now = position.load (std::memory_order_relaxed);

        if (now >= nextEditAt)
        {
            randomEdit (desc, base, options.channels, rng);
            core.submit (builder.build (desc));
            ++edits;
            nextEditAt = now + editInterval;
        }

        if (builder.pruneFinishedFades (true))
        {
            core.submit (builder.rebuild());
            ++rebuilds;
        }

        maxFading = std::max (maxFading, builder.getNumFadingWires());
        core.collectGarbage();

        if (now >= nextReportAt)
        {
            std::printf ("  %6.1f min  edits %lld  mismatches %lld\n", (double) now / options.rate / 60.0,
                         (long long) edits, (long long) mismatches.load());
            std::fflush (stdout);
            nextReportAt = now + (std::int64_t) (options.rate * 60.0 * 5.0);
        }

        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }

    audio.join();
    core.collectGarbage();

    const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    const auto stats = core.takeStats();

    std::printf ("\nAudio processed:  %.1f minutes in %.1f s (%.0fx real time)\n", options.minutes, seconds,
                 options.minutes * 60.0 / seconds);
    std::printf ("Graph edits:      %lld (+%lld rebuilds after fades), up to %d wires fading at once\n",
                 (long long) edits, (long long) rebuilds, maxFading);
    std::printf ("Callbacks:        %lld, late %lld\n", (long long) stats.callbacks, (long long) stats.lateCallbacks);
    std::printf ("Mismatched samples: %lld", (long long) mismatches.load());

    if (mismatches > 0)
        std::printf (" (first at sample %lld)\n\nFAIL\n", (long long) firstMismatch.load());
    else
        std::printf ("\n\nPASS: output was bit-exact throughout\n");

    return mismatches > 0 ? 1 : 0;
}
