// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EngineRig.h"

#include "nodes/Recorder.h"
#include "record/DiskWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace spm;
using namespace spm::test;
namespace types = nodes::types;
namespace fs = std::filesystem;

namespace
{

/** A fresh folder, deleted afterwards. */
struct TempFolder
{
    TempFolder()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("spm_record_test_" + std::to_string (rd()));
        fs::create_directories (path);
    }

    ~TempFolder()
    {
        std::error_code ec;
        fs::remove_all (path, ec);
    }

    fs::path operator/ (const std::string& name) const { return path / name; }

    fs::path path;
};

/** Deterministic 24-bit values, as a 24-bit interface would deliver them. */
std::int32_t pcmValue (int ch, std::int64_t i)
{
    auto x = (std::uint64_t) i * 2654435761u + (std::uint64_t) ch * 40503u + 12345u;
    x ^= x >> 13;
    x *= 0x9e3779b97f4a7c15ull;
    return (std::int32_t) (x >> 40) - 8388608;
}

float pcmToFloat (std::int32_t v) { return (float) ((double) v / 8388607.0); }

void useQuantisedInput (EngineRig& rig)
{
    rig.input = [] (int ch, std::int64_t i) { return pcmToFloat (pcmValue (ch, i)); };
}

std::shared_ptr<engine::RecordStream> streamOf (EngineRig& rig, graph::NodeId id)
{
    auto* recorder = dynamic_cast<nodes::RecorderProcessor*> (rig.builder.getProcessor (id));
    REQUIRE (recorder != nullptr);
    return recorder->getStream();
}

record::FileSpec fileSpec (const fs::path& path, std::vector<int> channels, record::SampleFormat format)
{
    record::FileSpec f;
    f.path = path;
    f.channels = std::move (channels);
    f.format = format;
    return f;
}

/** Runs in blocks, letting the writer catch up every few blocks as its thread would. */
void runAndService (EngineRig& rig, record::DiskWriter& writer, int samples)
{
    for (int done = 0; done < samples; done += rig.blockSize * 8)
    {
        rig.run (std::min (rig.blockSize * 8, samples - done));
        writer.service();
    }
}

} // namespace

//==============================================================================
TEST_CASE ("Record stream keeps positions and reports overwritten samples", "[record]")
{
    engine::RecordStream stream (1, 10);  // 1024 samples
    std::vector<float> block (256);
    float* channels[] = { block.data() };

    for (int b = 0; b < 3; ++b)
    {
        for (int i = 0; i < 256; ++i)
            block[(size_t) i] = (float) (b * 256 + i);
        stream.write ({ channels, 1, 256 });
    }

    REQUIRE (stream.getWritePosition() == 768);

    std::vector<float> out (300);
    stream.read (0, 400, 300, out.data());
    for (int i = 0; i < 300; ++i)
        REQUIRE (out[(size_t) i] == (float) (400 + i));

    // Nothing written yet has been overwritten, but a block in flight could reach back.
    REQUIRE (stream.oldestIntactPosition() == 768 + engine::RecordStream::maxBlockInFlight - 1024);

    engine::RecordEvent e;
    REQUIRE_FALSE (stream.popEvent (e));
    REQUIRE (stream.pushEvent ({ 3, 768 }));
    REQUIRE (stream.popEvent (e));
    REQUIRE (e.take == 3);
    REQUIRE (e.position == 768);
}

TEST_CASE ("WAV writer makes a readable Broadcast WAV", "[record]")
{
    TempFolder folder;

    SECTION ("24-bit mono, bit exact, with markers and time reference")
    {
        const auto path = folder / "mono.wav";
        record::WavWriter writer;
        record::WavSpec spec;
        spec.channels = 1;
        spec.sampleRate = 48000;
        spec.timeReference = 123456789;
        spec.description = "Kick";
        REQUIRE (writer.open (path, spec));

        std::vector<float> samples (1001);  // odd byte count: needs a pad byte
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = pcmToFloat (pcmValue (0, (std::int64_t) i));
        const float* channels[] = { samples.data() };
        REQUIRE (writer.write (channels, (int) samples.size()));
        REQUIRE (writer.finalise ({ { 10, "Verse" }, { 500, "Chorus" } }));

        const auto info = record::readWav (path);
        REQUIRE (info.ok);
        REQUIRE_FALSE (info.rf64);
        REQUIRE (info.channels == 1);
        REQUIRE (info.bitsPerSample == 24);
        REQUIRE (info.sampleRate == 48000.0);
        REQUIRE (info.frames == 1001);
        REQUIRE (info.hasBext);
        REQUIRE (info.timeReference == 123456789);
        REQUIRE (fs::file_size (path) % 2 == 0);

        for (size_t i = 0; i < samples.size(); ++i)
            REQUIRE (info.pcm[0][i] == pcmValue (0, (std::int64_t) i));

        REQUIRE (info.markers.size() == 2);
        REQUIRE (info.markers[0].position == 10);
        REQUIRE (info.markers[0].name == "Verse");
        REQUIRE (info.markers[1].position == 500);
        REQUIRE (info.markers[1].name == "Chorus");
    }

    SECTION ("32-bit float, interleaved stereo and multichannel")
    {
        for (int channelCount : { 2, 3 })
        {
            const auto path = folder / ("float" + std::to_string (channelCount) + ".wav");
            record::WavWriter writer;
            record::WavSpec spec;
            spec.channels = channelCount;
            spec.format = record::SampleFormat::float32;
            REQUIRE (writer.open (path, spec));

            std::vector<std::vector<float>> data ((size_t) channelCount, std::vector<float> (500));
            std::vector<const float*> pointers;
            for (int ch = 0; ch < channelCount; ++ch)
            {
                for (int i = 0; i < 500; ++i)
                    data[(size_t) ch][(size_t) i] = (float) (ch + 1) * 1.5f + (float) i * 0.001f;  // above full scale is fine
                pointers.push_back (data[(size_t) ch].data());
            }

            REQUIRE (writer.write (pointers.data(), 500));
            REQUIRE (writer.finalise());

            const auto info = record::readWav (path);
            REQUIRE (info.ok);
            REQUIRE (info.format == record::SampleFormat::float32);
            REQUIRE (info.channels == channelCount);
            REQUIRE (info.frames == 500);
            REQUIRE (info.samples == data);
        }
    }

    SECTION ("Switches to RF64 past the size limit")
    {
        const auto path = folder / "big.wav";
        record::WavWriter writer;
        writer.setRf64Threshold (10000);
        record::WavSpec spec;
        spec.channels = 2;
        spec.format = record::SampleFormat::float32;
        REQUIRE (writer.open (path, spec));

        std::vector<float> left (1000, 0.25f), right (1000, -0.5f);
        const float* channels[] = { left.data(), right.data() };
        REQUIRE (writer.write (channels, 1000));
        REQUIRE_FALSE (writer.isRf64());
        REQUIRE (writer.write (channels, 1000));
        REQUIRE (writer.updateHeader());
        REQUIRE (writer.isRf64());
        REQUIRE (writer.finalise ({ { 1500, "Late" } }));

        const auto info = record::readWav (path);
        REQUIRE (info.ok);
        REQUIRE (info.rf64);
        REQUIRE (info.frames == 2000);
        REQUIRE (info.samples[0][1999] == 0.25f);
        REQUIRE (info.samples[1][0] == -0.5f);
        REQUIRE (info.markers.size() == 1);
        REQUIRE (info.markers[0].position == 1500);
    }
}

TEST_CASE ("An unfinished WAV file is repaired from its length", "[record]")
{
    TempFolder folder;
    const auto path = folder / "crashed.wav";

    std::vector<float> samples (3000);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = (float) i / 3000.0f;

    {
        record::WavWriter writer;
        record::WavSpec spec;
        spec.format = record::SampleFormat::float32;
        REQUIRE (writer.open (path, spec));
        const float* channels[] = { samples.data() };
        REQUIRE (writer.write (channels, 3000));
        REQUIRE (writer.finalise());
    }

    // As if the header was last refreshed at 1000 frames and the app then crashed partway
    // through writing a frame.
    {
        auto* f = record::openFile (path, "r+b");
        REQUIRE (f != nullptr);
        const auto info = record::readWav (path, false);
        auto put32 = [f] (long position, std::uint32_t v)
        {
            unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) };
            std::fseek (f, position, SEEK_SET);
            std::fwrite (b, 1, 4, f);
        };
        put32 ((long) info.dataOffset - 4, 4000);
        put32 (4, (std::uint32_t) (info.dataOffset + 4000 - 8));
        std::fseek (f, 0, SEEK_END);
        std::fwrite ("\x01\x02", 1, 2, f);
        std::fclose (f);
        REQUIRE (record::readWav (path, false).frames == 1000);
    }

    std::string error;
    REQUIRE (record::repairWav (path, error));
    const auto info = record::readWav (path);
    REQUIRE (info.ok);
    REQUIRE (info.frames == 3000);
    REQUIRE (info.samples[0] == samples);

    // Repairing a good file changes nothing.
    const auto size = fs::file_size (path);
    REQUIRE (record::repairWav (path, error));
    REQUIRE (fs::file_size (path) == size);
}

//==============================================================================
TEST_CASE ("Recorders capture a take sample-aligned and bit exact", "[record]")
{
    TempFolder folder;
    EngineRig rig (2, 2);
    useQuantisedInput (rig);

    // Raw: straight from the inputs, one 24-bit file per channel. Processed: through a
    // gain node, one float stereo file.
    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, 2 }));
    d.nodes.push_back (node (2, types::recorder, { 1, nodes::recorder::formatPcm24, nodes::recorder::filesMono, 2 }));
    d.nodes.push_back (node (3, types::gain, { 0.0f, 0.0f, 0.0f, 2.0f }));
    d.nodes.push_back (node (4, types::recorder, { 1, nodes::recorder::formatFloat, nodes::recorder::filesSingle, 2 }));
    d.wires.push_back (wire (10, 1, 0, 2, 0));
    d.wires.push_back (wire (11, 1, 0, 3, 0));
    d.wires.push_back (wire (12, 3, 0, 4, 0));
    rig.submit (d);

    record::DiskWriter writer;
    writer.setWatchedStreams ({ streamOf (rig, 2), streamOf (rig, 4) });
    runAndService (rig, writer, 9600);  // past the wires' fade-in

    record::TakePlan plan;
    plan.takeId = 1;
    plan.streams.push_back ({ streamOf (rig, 2), "Raw",
                              { fileSpec (folder / "Raw 1.wav", { 0 }, record::SampleFormat::pcm24),
                                fileSpec (folder / "Raw 2.wav", { 1 }, record::SampleFormat::pcm24) } });
    plan.streams.push_back ({ streamOf (rig, 4), "Mix", { fileSpec (folder / "Mix.wav", { 0, 1 }, record::SampleFormat::float32) } });
    writer.beginTake (plan);
    writer.service();

    rig.core.setRecordTake (1);
    const auto start = rig.position;
    runAndService (rig, writer, 20000);
    REQUIRE (rig.core.getRecordedSamples() == 20000);

    writer.addMarker (1, 4800, "Solo");
    rig.core.setRecordTake (0);
    rig.run (rig.blockSize);
    writer.endTake (1, 1.0);
    writer.service();

    const auto status = writer.getStatus (1);
    REQUIRE (status.finished);
    REQUIRE (status.errors.empty());
    REQUIRE (status.dropouts.empty());
    REQUIRE (status.frames == 20000);

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto raw = record::readWav (folder / ("Raw " + std::to_string (ch + 1) + ".wav"));
        REQUIRE (raw.ok);
        REQUIRE (raw.channels == 1);
        REQUIRE (raw.frames == 20000);
        for (size_t i = 0; i < raw.frames; ++i)
            REQUIRE (raw.pcm[0][i] == pcmValue (ch, start + (std::int64_t) i));
        REQUIRE (raw.markers.size() == 1);
        REQUIRE (raw.markers[0].position == 4800);
        REQUIRE (raw.markers[0].name == "Solo");
    }

    const auto mix = record::readWav (folder / "Mix.wav");
    REQUIRE (mix.ok);
    REQUIRE (mix.channels == 2);
    REQUIRE (mix.frames == 20000);
    for (int ch = 0; ch < 2; ++ch)
        for (size_t i = 0; i < mix.frames; ++i)
            REQUIRE (mix.samples[(size_t) ch][i] == pcmToFloat (pcmValue (ch, start + (std::int64_t) i)));
}

TEST_CASE ("Pre-roll starts the files before Record was pressed", "[record]")
{
    TempFolder folder;
    EngineRig rig (1, 1);
    useQuantisedInput (rig);

    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, 1 }));
    d.nodes.push_back (node (2, types::recorder, { 1, nodes::recorder::formatPcm24, nodes::recorder::filesAuto, 1 }));
    d.wires.push_back (wire (10, 1, 0, 2, 0));
    rig.submit (d);

    rig.run (4800);  // past the wire's fade-in

    record::DiskWriter writer;
    writer.setPreRoll (4800);
    writer.setWatchedStreams ({ streamOf (rig, 2) });
    writer.service();
    const auto watchStart = rig.position;
    rig.run (2000);  // less history than the pre-roll asks for...
    writer.service();

    // ...so this take gets silence in front, while a later one gets it all from memory.
    auto recordTake = [&] (std::uint32_t id, const std::string& name)
    {
        record::TakePlan plan;
        plan.takeId = id;
        plan.preRollSamples = 4800;
        plan.streams.push_back ({ streamOf (rig, 2), "In", { fileSpec (folder / name, { 0 }, record::SampleFormat::pcm24) } });
        writer.beginTake (plan);
        writer.service();

        rig.core.setRecordTake (id);
        const auto start = rig.position;
        runAndService (rig, writer, 6000);
        rig.core.setRecordTake (0);
        rig.run (rig.blockSize);
        writer.endTake (id, 1.0);
        writer.service();
        REQUIRE (writer.getStatus (id).finished);
        return start;
    };

    const auto firstStart = recordTake (1, "first.wav");
    runAndService (rig, writer, 10000);
    const auto secondStart = recordTake (2, "second.wav");

    const auto first = record::readWav (folder / "first.wav");
    REQUIRE (first.frames == 4800 + 6000);
    for (std::int64_t i = 0; i < (std::int64_t) first.frames; ++i)
    {
        const auto source = firstStart - 4800 + i;
        REQUIRE (first.pcm[0][(size_t) i] == (source < watchStart ? 0 : pcmValue (0, source)));
    }

    const auto second = record::readWav (folder / "second.wav");
    REQUIRE (second.frames == 4800 + 6000);
    for (std::int64_t i = 0; i < (std::int64_t) second.frames; ++i)
        REQUIRE (second.pcm[0][(size_t) i] == pcmValue (0, secondStart - 4800 + i));
}

TEST_CASE ("A writer that falls behind logs a dropout and keeps the files aligned", "[record]")
{
    TempFolder folder;
    EngineRig rig (1, 1, 48000.0, 1024);
    useQuantisedInput (rig);

    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, 1 }));
    d.nodes.push_back (node (2, types::recorder, { 1, nodes::recorder::formatPcm24, nodes::recorder::filesAuto, 1 }));
    d.wires.push_back (wire (10, 1, 0, 2, 0));
    rig.submit (d);

    rig.run (4800);  // past the wire's fade-in

    const auto stream = streamOf (rig, 2);
    record::DiskWriter writer;
    writer.setWatchedStreams ({ stream });
    writer.service();

    record::TakePlan plan;
    plan.takeId = 1;
    plan.streams.push_back ({ stream, "In", { fileSpec (folder / "in.wav", { 0 }, record::SampleFormat::pcm24) } });
    writer.beginTake (plan);
    writer.service();

    rig.core.setRecordTake (1);
    const auto start = rig.position;
    runAndService (rig, writer, 10240);

    // Stall for longer than the ring holds.
    const auto stall = (int) stream->getCapacity() + 50 * 1024;
    rig.run (stall);
    runAndService (rig, writer, 10240);

    rig.core.setRecordTake (0);
    rig.run (1024);
    writer.endTake (1, 1.0);
    writer.service();

    const auto status = writer.getStatus (1);
    REQUIRE (status.finished);
    REQUIRE (status.dropouts.size() == 1);
    const auto& dropout = status.dropouts[0];
    REQUIRE (dropout.position >= 10240);
    REQUIRE (dropout.length > 0);

    const auto file = record::readWav (folder / "in.wav");
    const auto total = 10240 + stall + 10240;
    REQUIRE ((std::int64_t) file.frames == total);
    REQUIRE (status.frames == total);

    for (std::int64_t i = 0; i < total; ++i)
    {
        const auto lost = i >= dropout.position && i < dropout.position + dropout.length;
        REQUIRE (file.pcm[0][(size_t) i] == (lost ? 0 : pcmValue (0, start + i)));
    }
}

TEST_CASE ("The writer thread keeps up with a running engine", "[record]")
{
    TempFolder folder;
    EngineRig rig (8, 2, 48000.0, 64);
    useQuantisedInput (rig);

    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, 8 }));
    d.nodes.push_back (node (2, types::recorder, { 1, nodes::recorder::formatPcm24, nodes::recorder::filesMono, 8 }));
    d.wires.push_back (wire (10, 1, 0, 2, 0));
    rig.submit (d);

    rig.run (4800);  // past the wire's fade-in

    const auto stream = streamOf (rig, 2);
    record::DiskWriter writer;
    writer.setHeaderInterval (0.01);
    writer.setWatchedStreams ({ stream });
    writer.startThread();

    record::TakePlan plan;
    plan.takeId = 7;
    std::vector<record::FileSpec> files;
    for (int ch = 0; ch < 8; ++ch)
        files.push_back (fileSpec (folder / ("ch" + std::to_string (ch) + ".wav"), { ch }, record::SampleFormat::pcm24));
    plan.streams.push_back ({ stream, "In", files });
    writer.beginTake (plan);

    rig.core.setRecordTake (7);
    const auto start = rig.position;

    // About 5 s of audio in small blocks, at a pace the ring can absorb.
    for (int i = 0; i < 40; ++i)
    {
        rig.run (6000);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    const auto length = rig.position - start;

    rig.core.setRecordTake (0);
    rig.run (64);
    writer.endTake (7, 1.0);

    for (int i = 0; i < 500 && ! writer.getStatus (7).finished; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    writer.stopThread();

    const auto status = writer.getStatus (7);
    REQUIRE (status.finished);
    REQUIRE (status.dropouts.empty());

    for (int ch = 0; ch < 8; ++ch)
    {
        const auto file = record::readWav (folder / ("ch" + std::to_string (ch) + ".wav"));
        REQUIRE ((std::int64_t) file.frames == length);
        for (std::int64_t i = 0; i < length; ++i)
            REQUIRE (file.pcm[0][(size_t) i] == pcmValue (ch, start + i));
    }
}
