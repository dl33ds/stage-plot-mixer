// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/NodeProcessor.h"
#include "engine/RecordStream.h"

#include <memory>

namespace spm::nodes
{

/** Recorder node parameters, by index. */
namespace recorder
{
    enum Param { armed, format, files, channels };
    enum Format { formatAuto, formatPcm24, formatFloat };
    enum Files { filesAuto, filesMono, filesSingle };
}

/** Feeds its input into a RecordStream; the disk writer does the rest. The stream is
    shared so the writer can finish a take even if the node is removed meanwhile.
*/
class RecorderProcessor final : public engine::NodeProcessor
{
public:
    RecorderProcessor (int numChannels, int numParams, int capacityLog2 = engine::RecordStream::defaultCapacityLog2)
        : NodeProcessor ({ numChannels }, {}, numParams),
          stream (std::make_shared<engine::RecordStream> (numChannels, capacityLog2))
    {
    }

    void process (const engine::ProcessContext& ctx, const engine::ChannelSpan* inputs, const engine::ChannelSpan*) noexcept override
    {
        if (ctx.recordTake != lastTake)
        {
            if (stream->pushEvent ({ ctx.recordTake, stream->getWritePosition() }))
                lastTake = ctx.recordTake;
        }

        stream->write (inputs[0]);
    }

    const std::shared_ptr<engine::RecordStream>& getStream() const noexcept { return stream; }

private:
    std::shared_ptr<engine::RecordStream> stream;
    std::uint32_t lastTake = 0;  // audio thread
};

} // namespace spm::nodes
