// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "record/DiskWriter.h"

#include <algorithm>

namespace spm::record
{

DiskWriter::DiskWriter() = default;

DiskWriter::~DiskWriter()
{
    stopThread();
    runCommands();

    // Close anything still open so the files are at least valid up to here.
    for (auto& [id, take] : takes)
        for (auto& r : take->recordings)
            if (r->state != State::done)
                finishRecording (*r);
}

void DiskWriter::startThread()
{
    if (thread.joinable())
        return;

    quit = false;
    thread = std::thread ([this]
    {
        std::unique_lock lock (threadLock);
        while (! quit)
        {
            lock.unlock();
            service();
            lock.lock();
            wake.wait_for (lock, std::chrono::milliseconds (10), [this] { return quit; });
        }
    });
}

void DiskWriter::stopThread()
{
    if (! thread.joinable())
        return;

    {
        std::lock_guard lock (threadLock);
        quit = true;
    }
    wake.notify_all();
    thread.join();
}

//==============================================================================
void DiskWriter::post (std::function<void()> command)
{
    std::lock_guard lock (commandLock);
    commands.push_back (std::move (command));
}

void DiskWriter::runCommands()
{
    std::vector<std::function<void()>> toRun;
    {
        std::lock_guard lock (commandLock);
        toRun.swap (commands);
    }

    for (auto& c : toRun)
        c();
}

void DiskWriter::setWatchedStreams (std::vector<std::shared_ptr<engine::RecordStream>> list)
{
    post ([this, list = std::move (list)]
    {
        for (auto& [key, w] : streams)
            w.watched = false;

        for (auto& s : list)
            if (s != nullptr)
                watch (s, true);

        // Forget streams nobody needs any more (their pre-roll goes with them).
        for (auto it = streams.begin(); it != streams.end();)
        {
            const auto used = it->second.watched || it->second.recording != nullptr
                              || std::any_of (takes.begin(), takes.end(), [&] (auto& t)
                                 {
                                     return std::any_of (t.second->recordings.begin(), t.second->recordings.end(), [&] (auto& r)
                                            { return r->state == State::waiting && r->plan.stream.get() == it->first; });
                                 });
            it = used ? std::next (it) : streams.erase (it);
        }
    });
}

void DiskWriter::setPreRoll (std::int64_t samples)
{
    post ([this, samples]
    {
        preRoll = std::max<std::int64_t> (0, samples);
        for (auto& [key, w] : streams)
            resizeHistory (w);
    });
}

void DiskWriter::setRf64Threshold (std::uint64_t bytes)
{
    post ([this, bytes] { rf64Threshold = bytes; });
}

void DiskWriter::beginTake (TakePlan plan)
{
    post ([this, plan = std::move (plan)]
    {
        auto take = std::make_unique<Take>();
        take->plan = plan;
        take->status.known = true;
        take->lastHeaderUpdate = Clock::now();

        for (const auto& streamPlan : plan.streams)
        {
            if (streamPlan.stream == nullptr)
                continue;

            auto r = std::make_unique<Recording>();
            r->take = take.get();
            r->plan = streamPlan;

            for (const auto& spec : streamPlan.files)
            {
                FileOut out;
                out.spec = spec;
                out.writer = std::make_unique<WavWriter>();
                out.writer->setRf64Threshold (rf64Threshold);

                WavSpec wav;
                wav.channels = (int) spec.channels.size();
                wav.sampleRate = plan.sampleRate;
                wav.format = spec.format;
                wav.pcmScale = spec.pcmScale;
                wav.description = streamPlan.name;
                wav.originationDate = plan.date;
                wav.originationTime = plan.time;
                wav.timeReference = plan.timeReference;

                if (! out.writer->open (spec.path, wav))
                {
                    out.failed = true;
                    addError (*take, out.writer->getError());
                }

                r->files.push_back (std::move (out));
            }

            watch (streamPlan.stream, false);
            take->recordings.push_back (std::move (r));
        }

        latestTake = std::max (latestTake, plan.takeId);
        takes[plan.takeId] = std::move (take);
    });
}

void DiskWriter::endTake (std::uint32_t takeId, double timeoutSeconds)
{
    post ([this, takeId, timeoutSeconds]
    {
        if (auto it = takes.find (takeId); it != takes.end())
        {
            it->second->ending = true;
            it->second->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration> (std::chrono::duration<double> (timeoutSeconds));
        }
    });
}

void DiskWriter::addMarker (std::uint32_t takeId, std::int64_t position, std::string name)
{
    post ([this, takeId, position, name = std::move (name)]
    {
        if (auto it = takes.find (takeId); it != takes.end())
            it->second->markers.push_back ({ (std::uint64_t) std::max<std::int64_t> (0, position), name });
    });
}

TakeStatus DiskWriter::getStatus (std::uint32_t takeId) const
{
    std::lock_guard lock (statusLock);
    const auto it = statuses.find (takeId);
    return it != statuses.end() ? it->second : TakeStatus();
}

void DiskWriter::forgetTake (std::uint32_t takeId)
{
    post ([this, takeId]
    {
        if (auto it = takes.find (takeId); it != takes.end() && it->second->status.finished)
            takes.erase (it);

        std::lock_guard lock (statusLock);
        statuses.erase (takeId);
    });
}

//==============================================================================
DiskWriter::Watched& DiskWriter::watch (const std::shared_ptr<engine::RecordStream>& stream, bool explicitly)
{
    auto [it, added] = streams.try_emplace (stream.get());
    auto& w = it->second;

    if (added)
    {
        w.stream = stream;
        w.watched = explicitly;
        resizeHistory (w);
    }
    else if (explicitly)
    {
        w.watched = true;
    }

    return w;
}

void DiskWriter::resizeHistory (Watched& w)
{
    if (w.historySize == preRoll)
        return;

    w.historySize = preRoll;
    w.historyWrite = w.historyFilled = 0;
    w.history.assign (preRoll > 0 ? (size_t) w.stream->getNumChannels() : 0, std::vector<float> ((size_t) preRoll, 0.0f));
}

DiskWriter::Recording* DiskWriter::findRecording (std::uint32_t takeId, const engine::RecordStream* stream)
{
    const auto it = takes.find (takeId);
    if (it == takes.end())
        return nullptr;

    for (auto& r : it->second->recordings)
        if (r->plan.stream.get() == stream)
            return r.get();

    return nullptr;
}

void DiskWriter::addError (Take& take, const std::string& message)
{
    if (std::find (take.status.errors.begin(), take.status.errors.end(), message) == take.status.errors.end())
        take.status.errors.push_back (message);
}

//==============================================================================
void DiskWriter::service()
{
    runCommands();

    for (auto& [key, w] : streams)
        drain (w);

    // Commands queued before a stop was seen (markers, the end of the take) must land
    // before the files are finished.
    runCommands();

    const auto now = Clock::now();

    for (auto& [id, take] : takes)
    {
        if (take->status.finished)
            continue;

        if (take->ending && now >= take->deadline)
        {
            for (auto& r : take->recordings)
                if (r->state == State::waiting || r->state == State::recording)
                    r->state = State::stopping;
        }

        auto allDone = true;

        for (auto& r : take->recordings)
        {
            if (r->state == State::stopping)
                finishRecording (*r);

            allDone = allDone && r->state == State::done;
        }

        if (allDone && take->ending)
            take->status.finished = true;

        // Crash safety: keep the headers current.
        if (now - take->lastHeaderUpdate >= std::chrono::duration<double> (headerInterval.load()))
        {
            take->lastHeaderUpdate = now;
            for (auto& r : take->recordings)
                if (r->state == State::recording)
                    for (auto& f : r->files)
                        if (! f.failed && ! f.writer->updateHeader())
                        {
                            f.failed = true;
                            addError (*take, f.writer->getError());
                        }
        }
    }

    // Streams that were only kept for a finished take.
    for (auto it = streams.begin(); it != streams.end();)
    {
        const auto& w = it->second;
        const auto waiting = std::any_of (takes.begin(), takes.end(), [&] (auto& t)
        {
            return ! t.second->status.finished
                   && std::any_of (t.second->recordings.begin(), t.second->recordings.end(), [&] (auto& r)
                      { return r->state == State::waiting && r->plan.stream.get() == it->first; });
        });
        it = (w.watched || w.recording != nullptr || waiting) ? std::next (it) : streams.erase (it);
    }

    publish();
}

void DiskWriter::publish()
{
    std::lock_guard lock (statusLock);

    for (auto& [id, take] : takes)
    {
        auto& s = take->status;
        s.frames = 0;
        for (auto& r : take->recordings)
        {
            s.frames = std::max (s.frames, r->frames);
            if (r->state != State::waiting)
                s.started = true;
        }
        statuses[id] = s;
    }
}

void DiskWriter::drain (Watched& w)
{
    auto& stream = *w.stream;
    const auto end = stream.getWritePosition();

    for (engine::RecordEvent e; stream.popEvent (e);)
        w.pending.push_back (e);

    if (w.drained < 0)
    {
        // New: start at the write position, or earlier if an event is still in the ring.
        auto start = end;
        if (! w.pending.empty())
            start = std::min (start, w.pending.front().position);
        w.drained = std::max (start, end - stream.getCapacity() + engine::RecordStream::maxBlockInFlight);
        w.drained = std::max<std::int64_t> (0, std::min (w.drained, end));
    }

    for (;;)
    {
        while (! w.pending.empty() && w.pending.front().position <= w.drained)
        {
            if (! apply (w, w.pending.front()))
                return;
            w.pending.pop_front();
        }

        if (w.drained >= end)
            break;

        const auto boundary = w.pending.empty() ? end : std::min (end, w.pending.front().position);
        processRange (w, w.drained, boundary);
        w.drained = boundary;
    }
}

bool DiskWriter::apply (Watched& w, const engine::RecordEvent& e)
{
    // A take newer than any planned: its plan was queued after this round's commands ran,
    // so wait for the next round rather than miss the start.
    if (e.take > latestTake)
        return false;

    if (w.recording != nullptr && w.recording->take->plan.takeId != e.take)
    {
        w.recording->state = State::stopping;
        w.recording = nullptr;
    }

    if (e.take == 0 || w.recording != nullptr)
        return true;

    if (auto* r = findRecording (e.take, w.stream.get()); r != nullptr && r->state == State::waiting)
        startRecording (w, *r);

    return true;
}

void DiskWriter::startRecording (Watched& w, Recording& r)
{
    r.state = State::recording;
    w.recording = &r;

    // Pre-roll: what's in the history, padded with silence at the front if there's less
    // than asked for, so every file of the take still starts at the same moment.
    const auto wanted = r.take->plan.preRollSamples;
    const auto available = std::min (wanted, w.historyFilled);

    if (wanted > available)
    {
        for (auto& f : r.files)
            if (! f.failed && ! f.writer->writeSilence (wanted - available))
            {
                f.failed = true;
                addError (*r.take, f.writer->getError());
            }
        r.frames += wanted - available;
    }

    const auto channels = w.stream->getNumChannels();
    std::vector<const float*> pointers ((size_t) channels);

    for (std::int64_t done = 0; done < available;)
    {
        const auto index = (w.historyWrite - available + done + w.historySize) % w.historySize;
        const auto n = (int) std::min ({ available - done, w.historySize - index, (std::int64_t) chunkFrames });

        for (int ch = 0; ch < channels; ++ch)
            pointers[(size_t) ch] = w.history[(size_t) ch].data() + index;

        writeToFiles (r, pointers.data(), n);
        done += n;
    }
}

void DiskWriter::processRange (Watched& w, std::int64_t from, std::int64_t to)
{
    if (w.recording == nullptr && w.historySize == 0)
        return;  // nobody needs these samples

    auto& stream = *w.stream;
    const auto channels = stream.getNumChannels();

    if ((int) scratch.size() < channels)
        scratch.resize ((size_t) channels);
    for (int ch = 0; ch < channels; ++ch)
        scratch[(size_t) ch].resize ((size_t) chunkFrames);

    std::vector<const float*> pointers ((size_t) channels);
    for (int ch = 0; ch < channels; ++ch)
        pointers[(size_t) ch] = scratch[(size_t) ch].data();

    for (auto position = from; position < to;)
    {
        const auto n = (int) std::min<std::int64_t> (to - position, chunkFrames);

        // Anything older than this was overwritten before (or while) it was copied.
        auto intact = stream.oldestIntactPosition();
        if (intact < position + n)
            for (int ch = 0; ch < channels; ++ch)
                stream.read (ch, position, n, scratch[(size_t) ch].data());
        intact = stream.oldestIntactPosition();

        const auto lost = (int) std::clamp<std::int64_t> (intact - position, 0, n);
        if (lost > 0)
        {
            for (int ch = 0; ch < channels; ++ch)
                std::fill_n (scratch[(size_t) ch].data(), lost, 0.0f);

            if (auto* r = w.recording)
            {
                auto& dropouts = r->take->status.dropouts;
                if (! dropouts.empty() && dropouts.back().stream == r->plan.name
                    && dropouts.back().position + dropouts.back().length == r->frames)
                    dropouts.back().length += lost;
                else
                    dropouts.push_back ({ r->plan.name, r->frames, lost });
            }
        }

        if (w.historySize > 0)
        {
            for (int done = 0; done < n;)
            {
                const auto index = w.historyWrite;
                const auto run = (int) std::min<std::int64_t> (n - done, w.historySize - index);
                for (int ch = 0; ch < channels; ++ch)
                    std::copy_n (scratch[(size_t) ch].data() + done, run, w.history[(size_t) ch].data() + index);
                w.historyWrite = (index + run) % w.historySize;
                done += run;
            }
            w.historyFilled = std::min (w.historySize, w.historyFilled + n);
        }

        if (w.recording != nullptr)
            writeToFiles (*w.recording, pointers.data(), n);

        position += n;
    }
}

void DiskWriter::writeToFiles (Recording& r, const float* const* channels, int numFrames)
{
    std::vector<const float*> pointers;

    for (auto& f : r.files)
    {
        if (f.failed)
            continue;

        pointers.clear();
        for (auto ch : f.spec.channels)
            pointers.push_back (channels[ch]);

        if (! f.writer->write (pointers.data(), numFrames))
        {
            f.failed = true;
            addError (*r.take, f.writer->getError());
        }
    }

    r.frames += numFrames;
}

void DiskWriter::finishRecording (Recording& r)
{
    for (auto& [key, w] : streams)
        if (w.recording == &r)
            w.recording = nullptr;

    std::vector<WavMarker> markers;
    for (const auto& m : r.take->markers)
    {
        const auto position = m.position + (std::uint64_t) r.take->plan.preRollSamples;
        if (position <= (std::uint64_t) r.frames)
            markers.push_back ({ position, m.name });
    }

    for (auto& f : r.files)
        if (f.writer->isOpen() && ! f.writer->finalise (markers) && ! f.failed)
            addError (*r.take, f.writer->getError());

    r.state = State::done;
}

} // namespace spm::record
