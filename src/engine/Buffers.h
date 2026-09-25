// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace spm::engine
{

/** A non-owning view of a multi-channel block of samples. */
struct ChannelSpan
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;

    float* channel (int index) const noexcept { return channels[index]; }

    void clear() const noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill_n (channels[ch], numSamples, 0.0f);
    }
};

/** Owns preallocated storage for a fixed number of channels × a maximum block size.
    Allocates only in the constructor; channel pointers stay valid for its lifetime.
*/
class ChannelBuffer
{
public:
    ChannelBuffer (int numChannels, int maxBlockSize)
        : storage ((size_t) numChannels * (size_t) maxBlockSize, 0.0f),
          pointers ((size_t) numChannels, nullptr),
          channels (numChannels),
          capacity (maxBlockSize)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            pointers[(size_t) ch] = storage.data() + (size_t) ch * (size_t) maxBlockSize;
    }

    ChannelBuffer (const ChannelBuffer&) = delete;
    ChannelBuffer& operator= (const ChannelBuffer&) = delete;

    ChannelSpan span (int numSamples) const noexcept
    {
        return { pointers.data(), channels, std::min (numSamples, capacity) };
    }

    int getNumChannels() const noexcept { return channels; }
    int getCapacity() const noexcept { return capacity; }

private:
    std::vector<float> storage;
    std::vector<float*> pointers;
    int channels = 0;
    int capacity = 0;
};

} // namespace spm::engine
