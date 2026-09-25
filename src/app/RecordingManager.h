// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "app/AudioEngine.h"
#include "model/Session.h"
#include "record/DiskWriter.h"

namespace spm::app
{

/** Takes: starts and stops every armed Recorder together, names and places the files,
    writes take.json, keeps an eye on disk space, and repairs takes a crash interrupted.
    Message thread.

    A take goes in "<session folder>/Takes/<date>_<time>_TakeNN/", or under
    Documents/StagePlotMixer/Recordings for a session that hasn't been saved.
*/
class RecordingManager final : private juce::Timer
{
public:
    RecordingManager (AudioEngine& engine, model::Session& session, juce::PropertiesFile& settings);
    ~RecordingManager() override;

    enum class State { idle, recording, finishing };
    State getState() const noexcept { return state; }
    bool isRecording() const noexcept { return state != State::idle; }

    /** Starts a take. Returns why it couldn't, or an empty string. */
    juce::String start();

    /** Stops the take; then() runs once every file is finished. */
    void stop (std::function<void()> then = {});

    void addMarker();

    double getRecordedSeconds() const;
    int getNumArmed() const noexcept { return numArmed; }

    /** Recording time left on the disk, e.g. "5 h 20 min", or empty if unknown. */
    juce::String getTimeLeftText() const;
    bool isDiskLow() const noexcept { return diskLow; }

    juce::File getTakesFolder() const;
    juce::StringArray getRecentTakes() const;

    int getPreRollSeconds() const;
    void setPreRollSeconds (int seconds);
    int getLowDiskMinutes() const;
    void setLowDiskMinutes (int minutes);

    /** Repairs takes that were cut short by a crash or power cut. Returns their folders. */
    juce::StringArray recoverInterruptedTakes();

    /** The current session file (none if unsaved). */
    std::function<juce::File()> getSessionFile;

    std::function<void()> onStateChanged;
    std::function<void (const juce::String& title, const juce::String& message)> onProblem;

private:
    struct Recorder
    {
        graph::NodeId id = 0;
        juce::String name;
        std::shared_ptr<engine::RecordStream> stream;
        int channels = 0;
        record::SampleFormat format = record::SampleFormat::float32;
        bool singleFile = true;
        juce::StringArray sources;
    };

    void timerCallback() override;
    std::vector<Recorder> findArmedRecorders() const;
    double bytesPerSecond (const std::vector<Recorder>&, double sampleRate) const;
    void finishTake();
    void writeTakeJson();
    void setUnfinished (const juce::File& folder, bool unfinished);
    void addRecent (const juce::File& folder);

    AudioEngine& engine;
    model::Session& session;
    juce::PropertiesFile& settings;
    record::DiskWriter writer;

    State state = State::idle;
    std::uint32_t takeId = 0;
    juce::File takeFolder;
    double takeSampleRate = 0.0;
    juce::var takeInfo;
    juce::Array<juce::var> markers;
    std::function<void()> afterStop;
    bool reportedErrors = false;

    std::vector<engine::RecordStream*> watched;
    int numArmed = 0;
    double currentRate = 0.0;
    std::int64_t freeBytes = -1;
    double dataRate = 0.0;
    bool diskLow = false, warnedLowDisk = false;
    int diskCheckCountdown = 0;
};

} // namespace spm::app
