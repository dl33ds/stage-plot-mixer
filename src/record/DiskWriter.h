// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/RecordStream.h"
#include "record/WavFile.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace spm::record
{

/** One file made from some of a stream's channels. */
struct FileSpec
{
    std::filesystem::path path;
    std::vector<int> channels;
    SampleFormat format = SampleFormat::pcm24;
    double pcmScale = 8388607.0;
};

/** One recorder's part in a take. */
struct StreamPlan
{
    std::shared_ptr<engine::RecordStream> stream;
    std::string name;
    std::vector<FileSpec> files;
};

struct TakePlan
{
    std::uint32_t takeId = 0;
    double sampleRate = 48000.0;
    std::int64_t preRollSamples = 0;  // written before the start, from the pre-roll history
    std::uint64_t timeReference = 0;  // BWF: samples since midnight at the files' first sample
    std::string date, time;           // BWF origination, "yyyy-mm-dd" / "hh:mm:ss"
    std::vector<StreamPlan> streams;
};

struct Dropout
{
    std::string stream;
    std::int64_t position = 0;  // frame in the files (pre-roll included)
    std::int64_t length = 0;
};

struct TakeStatus
{
    bool known = false, started = false, finished = false;
    std::int64_t frames = 0;  // longest file so far, pre-roll included
    std::vector<Dropout> dropouts;
    std::vector<std::string> errors;
};

/** Drains recorder streams to disk on its own thread.

    Every watched stream is drained continuously. With pre-roll on, the last few seconds
    of each are kept in memory, so a take can start before Record was pressed. A take is
    described up front by beginTake(); each stream then starts at the transport event it
    reports, so all files of a take line up to the sample.

    If the writer falls so far behind that a ring buffer wraps, the lost stretch is written
    as silence (so files stay aligned) and reported as a dropout.

    Methods other than service() may be called from any thread; they queue a command for
    the writer. Tests call service() directly instead of starting the thread.
*/
class DiskWriter
{
public:
    DiskWriter();
    ~DiskWriter();

    void startThread();
    void stopThread();

    /** The streams to keep drained (and keep pre-roll for): usually every armed recorder. */
    void setWatchedStreams (std::vector<std::shared_ptr<engine::RecordStream>> streams);

    /** Length of the pre-roll history kept for each watched stream. */
    void setPreRoll (std::int64_t samples);

    /** Opens the files. Call before the transport starts the take. */
    void beginTake (TakePlan plan);

    /** Call after the transport has stopped. Streams that haven't reported the stop within
        the timeout (the node was removed, or audio stopped) are finished where they are.
    */
    void endTake (std::uint32_t takeId, double timeoutSeconds = 1.0);

    /** position: samples since the take started (not counting pre-roll). */
    void addMarker (std::uint32_t takeId, std::int64_t position, std::string name);

    TakeStatus getStatus (std::uint32_t takeId) const;

    /** Drops a finished take's status. */
    void forgetTake (std::uint32_t takeId);

    /** Does one round of work. Called by the thread, or by tests. */
    void service();

    void setRf64Threshold (std::uint64_t bytes);
    void setHeaderInterval (double seconds) { headerInterval = seconds; }

    static constexpr int chunkFrames = 8192;

private:
    using Clock = std::chrono::steady_clock;
    struct Take;

    struct FileOut
    {
        FileSpec spec;
        std::unique_ptr<WavWriter> writer;
        bool failed = false;
    };

    enum class State { waiting, recording, stopping, done };

    struct Recording
    {
        Take* take = nullptr;
        StreamPlan plan;
        std::vector<FileOut> files;
        State state = State::waiting;
        std::int64_t frames = 0;
    };

    struct Take
    {
        TakePlan plan;
        std::vector<std::unique_ptr<Recording>> recordings;
        std::vector<WavMarker> markers;  // take-relative
        TakeStatus status;
        bool ending = false;
        Clock::time_point deadline;
        Clock::time_point lastHeaderUpdate;
    };

    struct Watched
    {
        std::shared_ptr<engine::RecordStream> stream;
        std::int64_t drained = -1;
        std::deque<engine::RecordEvent> pending;
        bool watched = true;  // false: only here because a take uses it
        Recording* recording = nullptr;

        std::vector<std::vector<float>> history;
        std::int64_t historySize = 0, historyWrite = 0, historyFilled = 0;
    };

    void post (std::function<void()> command);
    void runCommands();
    Watched& watch (const std::shared_ptr<engine::RecordStream>& stream, bool explicitly);
    void resizeHistory (Watched&);
    void drain (Watched&);
    bool apply (Watched&, const engine::RecordEvent&);
    void processRange (Watched&, std::int64_t from, std::int64_t to);
    void writeToFiles (Recording&, const float* const* channels, int numFrames);
    void startRecording (Watched&, Recording&);
    void finishRecording (Recording&);
    void addError (Take&, const std::string&);
    void publish();
    Recording* findRecording (std::uint32_t takeId, const engine::RecordStream*);

    // Commands (any thread).
    mutable std::mutex commandLock;
    std::vector<std::function<void()>> commands;

    // Writer thread state.
    std::map<const engine::RecordStream*, Watched> streams;
    std::map<std::uint32_t, std::unique_ptr<Take>> takes;
    std::int64_t preRoll = 0;
    std::uint32_t latestTake = 0;
    std::uint64_t rf64Threshold = WavWriter::defaultRf64Threshold;
    std::atomic<double> headerInterval { 2.0 };
    std::vector<std::vector<float>> scratch;

    // Published status.
    mutable std::mutex statusLock;
    std::map<std::uint32_t, TakeStatus> statuses;

    std::thread thread;
    std::mutex threadLock;
    std::condition_variable wake;
    bool quit = false;
};

} // namespace spm::record
