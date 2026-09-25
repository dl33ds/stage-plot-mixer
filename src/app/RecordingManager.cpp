// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/RecordingManager.h"

#include "nodes/Recorder.h"

namespace spm::app
{

namespace
{

namespace types = nodes::types;
using nodes::recorder::Param;

juce::StringArray fromLines (const juce::String& text)
{
    juce::StringArray lines;
    lines.addLines (text);
    lines.removeEmptyStrings();
    return lines;
}

juce::String formatDuration (double seconds)
{
    const auto minutes = (int) (seconds / 60.0);
    if (minutes >= 600)
        return juce::String (minutes / 60) + " h";
    if (minutes >= 60)
        return juce::String (minutes / 60) + " h " + juce::String (minutes % 60) + " min";
    return juce::String (minutes) + " min";
}

std::filesystem::path toPath (const juce::File& file)
{
#if JUCE_WINDOWS
    return std::filesystem::path (file.getFullPathName().toWideCharPointer());
#else
    return std::filesystem::path (file.getFullPathName().toRawUTF8());
#endif
}

juce::File toFile (const std::filesystem::path& path)
{
#if JUCE_WINDOWS
    return juce::File (juce::String (path.c_str()));
#else
    return juce::File (juce::String::fromUTF8 (path.c_str()));
#endif
}

juce::var fileSpecsToVar (const record::StreamPlan& plan, const juce::File& folder)
{
    juce::Array<juce::var> files;
    for (const auto& f : plan.files)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("file", toFile (f.path).getRelativePathFrom (folder));
        juce::Array<juce::var> channels;
        for (auto ch : f.channels)
            channels.add (ch + 1);
        o->setProperty ("channels", channels);
        files.add (o);
    }
    return files;
}

} // namespace

RecordingManager::RecordingManager (AudioEngine& e, model::Session& s, juce::PropertiesFile& p)
    : engine (e), session (s), settings (p)
{
    writer.startThread();
    startTimerHz (10);
}

RecordingManager::~RecordingManager()
{
    stopTimer();

    if (state != State::idle)
    {
        // Quitting mid-take (the UI asks first, so this is a last resort): finish what's there.
        if (state == State::recording)
        {
            engine.getCore().setRecordTake (0);
            writer.endTake (takeId, 0.5);
        }

        for (int i = 0; i < 300 && ! writer.getStatus (takeId).finished; ++i)
            juce::Thread::sleep (10);

        finishTake();
    }
}

//==============================================================================
std::vector<RecordingManager::Recorder> RecordingManager::findArmedRecorders() const
{
    std::vector<Recorder> result;
    const auto wires = session.getState().getChildWithName (model::ids::wires);

    for (auto id : session.getNodeIds())
    {
        const auto* type = session.getNodeType (id);
        if (type == nullptr || type->id != types::recorder || session.getParam (id, Param::armed) < 0.5f)
            continue;

        auto* processor = dynamic_cast<nodes::RecorderProcessor*> (engine.getBuilder().getProcessor (id));
        if (processor == nullptr)
            continue;

        Recorder r;
        r.id = id;
        r.name = session.getNodeName (id);
        r.stream = processor->getStream();
        r.channels = r.stream->getNumChannels();

        // What feeds it, and whether that's untouched input (for the Auto format).
        auto raw = true, anyWire = false;
        for (const auto& w : wires)
        {
            if ((graph::NodeId) (juce::int64) w[model::ids::dest] != id || ! engine.getBuilder().isWireActive ((graph::WireId) (juce::int64) w[model::ids::id]))
                continue;

            anyWire = true;
            const auto source = (graph::NodeId) (juce::int64) w[model::ids::source];
            const auto* sourceType = session.getNodeType (source);
            raw = raw && sourceType != nullptr && sourceType->id == types::hardwareInput && (float) w[model::ids::gain] == 1.0f;

            auto name = session.getNodeName (source);
            const auto layout = session.getLayout (source);
            const auto port = (int) w[model::ids::sourcePort];
            if (layout.outputs.size() > 1 && port >= 0 && port < (int) layout.outputs.size())
                name << " " << layout.outputs[(size_t) port].name;
            r.sources.addIfNotAlreadyThere (name);
        }

        const auto format = juce::roundToInt (session.getParam (id, Param::format));
        r.format = format == nodes::recorder::formatPcm24 || (format == nodes::recorder::formatAuto && raw && anyWire)
                       ? record::SampleFormat::pcm24
                       : record::SampleFormat::float32;

        const auto files = juce::roundToInt (session.getParam (id, Param::files));
        r.singleFile = files == nodes::recorder::filesSingle || (files == nodes::recorder::filesAuto && r.channels <= 2);

        result.push_back (std::move (r));
    }

    return result;
}

double RecordingManager::bytesPerSecond (const std::vector<Recorder>& recorders, double sampleRate) const
{
    double rate = 0.0;
    for (const auto& r : recorders)
        rate += r.channels * (r.format == record::SampleFormat::pcm24 ? 3.0 : 4.0) * sampleRate;
    return rate;
}

juce::File RecordingManager::getTakesFolder() const
{
    const auto sessionFile = getSessionFile ? getSessionFile() : juce::File();
    if (sessionFile.existsAsFile())
        return sessionFile.getParentDirectory().getChildFile ("Takes");

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("StagePlotMixer").getChildFile ("Recordings");
}

int RecordingManager::getPreRollSeconds() const { return settings.getIntValue ("preRollSeconds", 0); }
int RecordingManager::getLowDiskMinutes() const { return settings.getIntValue ("lowDiskMinutes", 30); }

void RecordingManager::setPreRollSeconds (int seconds)
{
    settings.setValue ("preRollSeconds", seconds);
    currentRate = 0.0;  // the timer applies it
}

void RecordingManager::setLowDiskMinutes (int minutes)
{
    settings.setValue ("lowDiskMinutes", minutes);
    warnedLowDisk = false;
    diskCheckCountdown = 0;
}

//==============================================================================
juce::String RecordingManager::start()
{
    if (state != State::idle)
        return "A take is already recording.";

    const auto status = engine.getStatus();
    if (! status.running || status.sampleRate <= 0.0)
        return "Audio isn't running. Choose an audio device in Audio settings first.";

    const auto recorders = findArmedRecorders();
    if (recorders.empty())
        return "There's no armed Recorder. Add a Recorder node (under Destinations), wire something into it, "
               "and make sure it's armed.";

    // Folder: <date>_<time>_TakeNN, numbered after the takes already there.
    const auto base = getTakesFolder();
    const auto now = juce::Time::getCurrentTime();
    auto number = 1;
    for (const auto& entry : juce::RangedDirectoryIterator (base, false, "*_Take*", juce::File::findDirectories))
        number = std::max (number, entry.getFile().getFileName().fromLastOccurrenceOf ("_Take", false, false).getIntValue() + 1);

    do
        takeFolder = base.getChildFile (now.formatted ("%Y-%m-%d_%H%M") + "_Take" + juce::String (number++).paddedLeft ('0', 2));
    while (takeFolder.exists());

    if (const auto result = takeFolder.createDirectory(); result.failed())
        return "Couldn't create the take folder " + takeFolder.getFullPathName() + ": " + result.getErrorMessage();

    const auto rate = bytesPerSecond (recorders, status.sampleRate);
    const auto free = takeFolder.getBytesFreeOnVolume();
    if (free > 0 && (double) free < rate * 60.0)
    {
        takeFolder.deleteRecursively();
        return "There's less than a minute of recording space left on this disk.";
    }

    // 24-bit files are written with the inverse of the driver's own scaling, so untouched
    // inputs come out bit for bit.
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    const auto pcmScale = device != nullptr && device->getTypeName() == "ASIO" && device->getCurrentBitDepth() >= 32
                              ? 2147483647.0 / 256.0
                              : 8388607.0;

    const auto preRollSamples = (std::int64_t) getPreRollSeconds() * (std::int64_t) status.sampleRate;
    const auto midnight = juce::Time (now.getYear(), now.getMonth(), now.getDayOfMonth(), 0, 0);
    const auto samplesSinceMidnight = (std::int64_t) ((double) (now - midnight).inSeconds() * status.sampleRate);

    record::TakePlan plan;
    plan.takeId = ++takeId;
    plan.sampleRate = status.sampleRate;
    plan.preRollSamples = preRollSamples;
    plan.timeReference = (std::uint64_t) std::max<std::int64_t> (0, samplesSinceMidnight - preRollSamples);
    plan.date = now.formatted ("%Y-%m-%d").toStdString();
    plan.time = now.formatted ("%H:%M:%S").toStdString();

    juce::StringArray usedNames;
    auto uniqueFile = [&] (const juce::String& wanted)
    {
        auto name = juce::File::createLegalFileName (wanted).trim();
        if (name.isEmpty())
            name = "Recorder";
        auto candidate = name;
        for (int n = 2; usedNames.contains (candidate, true); ++n)
            candidate = name + " (" + juce::String (n) + ")";
        usedNames.add (candidate);
        return takeFolder.getChildFile (candidate + ".wav");
    };

    juce::Array<juce::var> recorderInfo;
    for (const auto& r : recorders)
    {
        record::StreamPlan stream;
        stream.stream = r.stream;
        stream.name = r.name.toStdString();

        auto addFile = [&] (const juce::File& file, std::vector<int> channels)
        {
            record::FileSpec f;
            f.path = toPath (file);
            f.channels = std::move (channels);
            f.format = r.format;
            f.pcmScale = pcmScale;
            stream.files.push_back (std::move (f));
        };

        if (r.singleFile || r.channels == 1)
        {
            std::vector<int> all;
            for (int ch = 0; ch < r.channels; ++ch)
                all.push_back (ch);
            addFile (uniqueFile (r.name), all);
        }
        else
        {
            for (int ch = 0; ch < r.channels; ++ch)
                addFile (uniqueFile (r.name + " " + juce::String (ch + 1)), { ch });
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("name", r.name);
        o->setProperty ("channels", r.channels);
        o->setProperty ("format", r.format == record::SampleFormat::pcm24 ? "24-bit PCM" : "32-bit float");
        o->setProperty ("tap", r.sources.isEmpty() ? juce::String ("(nothing wired)") : r.sources.joinIntoString (", "));
        o->setProperty ("files", fileSpecsToVar (stream, takeFolder));
        recorderInfo.add (o);

        plan.streams.push_back (std::move (stream));
    }

    auto* info = new juce::DynamicObject();
    info->setProperty ("app", "Stage Plot Mixer " SPM_VERSION);
    info->setProperty ("take", takeFolder.getFileName());
    info->setProperty ("session", getSessionFile ? getSessionFile().getFileNameWithoutExtension() : juce::String());
    info->setProperty ("startTime", now.toISO8601 (true));
    info->setProperty ("sampleRate", status.sampleRate);
    info->setProperty ("device", status.deviceName);
    info->setProperty ("deviceType", status.typeName);
    info->setProperty ("preRollSeconds", getPreRollSeconds());
    info->setProperty ("recorders", recorderInfo);
    info->setProperty ("complete", false);
    takeInfo = info;
    markers.clear();
    reportedErrors = false;
    warnedLowDisk = false;
    takeSampleRate = status.sampleRate;
    writeTakeJson();
    setUnfinished (takeFolder, true);

    // The writer has the plan (and has opened the files) before the transport moves.
    writer.beginTake (std::move (plan));
    engine.getCore().setRecordTake (takeId);
    state = State::recording;

    if (onStateChanged)
        onStateChanged();
    return {};
}

void RecordingManager::stop (std::function<void()> then)
{
    if (state == State::idle)
    {
        if (then)
            then();
        return;
    }

    if (then)
        afterStop = std::move (then);

    if (state == State::recording)
    {
        engine.getCore().setRecordTake (0);
        writer.endTake (takeId, 1.0);
        state = State::finishing;

        if (onStateChanged)
            onStateChanged();
    }
}

void RecordingManager::addMarker()
{
    if (state != State::recording)
        return;

    const auto position = std::max<std::int64_t> (0, engine.getCore().getRecordedSamples());
    const auto name = "Marker " + juce::String (markers.size() + 1);
    writer.addMarker (takeId, position, name.toStdString());

    auto* m = new juce::DynamicObject();
    m->setProperty ("name", name);
    m->setProperty ("seconds", (double) position / takeSampleRate);
    m->setProperty ("sample", (juce::int64) (position + (std::int64_t) getPreRollSeconds() * (std::int64_t) takeSampleRate));
    markers.add (m);
}

double RecordingManager::getRecordedSeconds() const
{
    const auto samples = engine.getCore().getRecordedSamples();
    return state == State::recording && samples > 0 && takeSampleRate > 0.0 ? (double) samples / takeSampleRate : 0.0;
}

juce::String RecordingManager::getTimeLeftText() const
{
    if (freeBytes < 0 || dataRate <= 0.0)
        return {};
    return formatDuration ((double) freeBytes / dataRate);
}

//==============================================================================
void RecordingManager::timerCallback()
{
    const auto status = engine.getStatus();

    // Keep armed recorders drained (and their pre-roll filling).
    const auto recorders = findArmedRecorders();
    std::vector<engine::RecordStream*> now;
    std::vector<std::shared_ptr<engine::RecordStream>> streams;
    for (const auto& r : recorders)
    {
        now.push_back (r.stream.get());
        streams.push_back (r.stream);
    }

    if (now != watched)
    {
        watched = now;
        writer.setWatchedStreams (streams);
    }

    numArmed = (int) recorders.size();

    if (status.sampleRate != currentRate && status.sampleRate > 0.0)
    {
        currentRate = status.sampleRate;
        writer.setPreRoll ((std::int64_t) getPreRollSeconds() * (std::int64_t) currentRate);
    }

    // Disk space, every couple of seconds.
    if (--diskCheckCountdown <= 0)
    {
        diskCheckCountdown = 20;
        const auto folder = state != State::idle ? takeFolder : getTakesFolder();
        auto existing = folder;
        while (! existing.exists() && existing.getParentDirectory() != existing)
            existing = existing.getParentDirectory();
        freeBytes = existing.getBytesFreeOnVolume();
        dataRate = bytesPerSecond (recorders, status.sampleRate > 0.0 ? status.sampleRate : 48000.0);

        diskLow = freeBytes >= 0 && dataRate > 0.0 && (double) freeBytes / dataRate < getLowDiskMinutes() * 60.0;
        if (diskLow && state == State::recording && ! warnedLowDisk && onProblem)
        {
            warnedLowDisk = true;
            onProblem ("Disk nearly full", "There's only about " + getTimeLeftText() + " of recording space left.");
        }
    }

    if (state == State::recording)
    {
        // A device change ends the take (everything up to it is kept).
        if (! status.running || status.sampleRate != takeSampleRate)
        {
            stop();
            if (onProblem)
                onProblem ("Recording stopped", "The audio device stopped or changed, so the take was ended. "
                                                "Everything recorded up to that point is saved.");
        }
        else if (const auto takeStatus = writer.getStatus (takeId); ! takeStatus.errors.empty() && ! reportedErrors)
        {
            reportedErrors = true;
            if (onProblem)
                onProblem ("Recording problem", juce::String (takeStatus.errors.front()) + "\n\nOther files keep recording.");
        }
    }

    if (state == State::finishing && writer.getStatus (takeId).finished)
    {
        finishTake();
        if (onStateChanged)
            onStateChanged();
        if (auto then = std::move (afterStop))
            then();
    }
}

void RecordingManager::finishTake()
{
    const auto status = writer.getStatus (takeId);

    if (auto* info = takeInfo.getDynamicObject())
    {
        const auto preRoll = (double) getPreRollSeconds();
        info->setProperty ("durationSeconds", takeSampleRate > 0.0 ? (double) status.frames / takeSampleRate : 0.0);
        info->setProperty ("markers", markers);

        juce::Array<juce::var> dropouts;
        for (const auto& d : status.dropouts)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("recorder", juce::String (d.stream));
            o->setProperty ("seconds", (double) d.position / takeSampleRate - preRoll);
            o->setProperty ("sample", (juce::int64) d.position);
            o->setProperty ("lengthSamples", (juce::int64) d.length);
            dropouts.add (o);
        }
        info->setProperty ("dropouts", dropouts);

        juce::Array<juce::var> errors;
        for (const auto& e : status.errors)
            errors.add (juce::String (e));
        info->setProperty ("errors", errors);
        info->setProperty ("complete", true);
    }

    writeTakeJson();
    setUnfinished (takeFolder, false);
    addRecent (takeFolder);
    writer.forgetTake (takeId);
    state = State::idle;

    if (! status.dropouts.empty() && onProblem)
        onProblem ("Dropouts in the take", juce::String ((int) status.dropouts.size())
                                               + " gap(s) where the disk couldn't keep up were filled with silence. "
                                                 "They're listed in take.json.");
}

void RecordingManager::writeTakeJson()
{
    takeFolder.getChildFile ("take.json").replaceWithText (juce::JSON::toString (takeInfo));
}

void RecordingManager::setUnfinished (const juce::File& folder, bool unfinished)
{
    auto list = fromLines (settings.getValue ("unfinishedTakes"));
    list.removeString (folder.getFullPathName());
    if (unfinished)
        list.add (folder.getFullPathName());
    settings.setValue ("unfinishedTakes", list.joinIntoString ("\n"));
    settings.saveIfNeeded();
}

void RecordingManager::addRecent (const juce::File& folder)
{
    auto list = fromLines (settings.getValue ("recentTakes"));
    list.removeString (folder.getFullPathName());
    list.insert (0, folder.getFullPathName());
    while (list.size() > 10)
        list.remove (list.size() - 1);
    settings.setValue ("recentTakes", list.joinIntoString ("\n"));
}

juce::StringArray RecordingManager::getRecentTakes() const
{
    juce::StringArray result;
    for (const auto& path : fromLines (settings.getValue ("recentTakes")))
        if (juce::File (path).isDirectory())
            result.add (path);
    return result;
}

juce::StringArray RecordingManager::recoverInterruptedTakes()
{
    juce::StringArray recovered;

    for (const auto& path : fromLines (settings.getValue ("unfinishedTakes")))
    {
        const juce::File folder (path);
        if (! folder.isDirectory())
            continue;

        juce::StringArray problems;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.wav"))
        {
            std::string error;
            if (! record::repairWav (toPath (entry.getFile()), error))
                problems.add (entry.getFile().getFileName() + ": " + juce::String (error));
        }

        const auto jsonFile = folder.getChildFile ("take.json");
        auto info = juce::JSON::parse (jsonFile);
        if (auto* o = info.getDynamicObject())
        {
            o->setProperty ("complete", true);
            o->setProperty ("recovered", true);
            if (! problems.isEmpty())
                o->setProperty ("recoveryProblems", problems);
            jsonFile.replaceWithText (juce::JSON::toString (info));
        }

        recovered.add (path);
        addRecent (folder);
    }

    settings.setValue ("unfinishedTakes", juce::String());
    return recovered;
}

} // namespace spm::app
