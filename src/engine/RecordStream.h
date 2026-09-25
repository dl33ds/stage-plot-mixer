// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/Buffers.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace spm::engine
{

/** A change of record transport, as seen by one recorder. take is 0 when recording stopped. */
struct RecordEvent
{
    std::uint32_t take = 0;
    std::int64_t position = 0;  // stream position of the first sample it applies to
};

/** The audio thread's side of a recorder: one ring buffer per channel, written every block
    whether or not a take is running, plus a queue of transport changes.

    Single producer (audio thread), single consumer (disk writer). Positions count samples
    since the stream was created and never wrap; the ring index is position & mask. The
    writer detects that it fell behind by comparing positions (see isIntact()).
*/
class RecordStream
{
public:
    explicit RecordStream (int numChannels, int capacityLog2 = defaultCapacityLog2)
        : channels (numChannels),
          capacity ((std::int64_t) 1 << capacityLog2),
          mask (capacity - 1),
          rings ((size_t) numChannels, std::vector<float> ((size_t) capacity, 0.0f))
    {
    }

    RecordStream (const RecordStream&) = delete;
    RecordStream& operator= (const RecordStream&) = delete;

    int getNumChannels() const noexcept { return channels; }
    std::int64_t getCapacity() const noexcept { return capacity; }

    // Audio thread ---------------------------------------------------------------------
    void write (const ChannelSpan& input) noexcept
    {
        const auto start = writePosition.load (std::memory_order_relaxed);
        const auto n = (std::int64_t) input.numSamples;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& ring = rings[(size_t) ch];
            const auto* src = input.channel (ch);

            for (std::int64_t done = 0; done < n;)
            {
                const auto index = (start + done) & mask;
                const auto run = std::min (n - done, capacity - index);
                std::copy_n (src + done, run, ring.data() + index);
                done += run;
            }
        }

        writePosition.store (start + n, std::memory_order_release);
    }

    /** Returns false if the queue is full (the writer has stalled for a long time). */
    bool pushEvent (RecordEvent event) noexcept
    {
        const auto head = eventHead.load (std::memory_order_relaxed);
        if (head - eventTail.load (std::memory_order_acquire) >= (std::uint32_t) events.size())
        {
            lostEvents.store (true, std::memory_order_relaxed);
            return false;
        }

        events[head % events.size()] = event;
        eventHead.store (head + 1, std::memory_order_release);
        return true;
    }

    // Disk writer ----------------------------------------------------------------------
    std::int64_t getWritePosition() const noexcept { return writePosition.load (std::memory_order_acquire); }

    bool popEvent (RecordEvent& event) noexcept
    {
        const auto tail = eventTail.load (std::memory_order_relaxed);
        if (tail == eventHead.load (std::memory_order_acquire))
            return false;

        event = events[tail % events.size()];
        eventTail.store (tail + 1, std::memory_order_release);
        return true;
    }

    /** Copies samples [from, from + n) of one channel. Check isIntact() afterwards. */
    void read (int channel, std::int64_t from, int n, float* dest) const noexcept
    {
        const auto& ring = rings[(size_t) channel];

        for (std::int64_t done = 0; done < n;)
        {
            const auto index = (from + done) & mask;
            const auto run = std::min ((std::int64_t) n - done, capacity - index);
            std::copy_n (ring.data() + index, run, dest + done);
            done += run;
        }
    }

    /** The oldest position that is certainly still in the ring, reading the write position
        now. Call after read(): anything copied from before this may have been overwritten
        while it was being copied. Allows for a block being written at the same time.
    */
    std::int64_t oldestIntactPosition() const noexcept
    {
        return getWritePosition() + maxBlockInFlight - capacity;
    }

    bool hasLostEvents() const noexcept { return lostEvents.load (std::memory_order_relaxed); }

    static constexpr int defaultCapacityLog2 = 19;  // 524288 samples, ~10.9 s at 48 kHz
    static constexpr std::int64_t maxBlockInFlight = 4096;

private:
    int channels;
    std::int64_t capacity, mask;
    std::vector<std::vector<float>> rings;
    std::atomic<std::int64_t> writePosition { 0 };

    std::array<RecordEvent, 64> events {};
    std::atomic<std::uint32_t> eventHead { 0 }, eventTail { 0 };
    std::atomic<bool> lostEvents { false };
};

} // namespace spm::engine
