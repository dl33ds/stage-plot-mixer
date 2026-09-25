// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace spm::record
{

enum class SampleFormat
{
    pcm24,
    float32
};

struct WavSpec
{
    int channels = 1;
    double sampleRate = 48000.0;
    SampleFormat format = SampleFormat::pcm24;

    /** Float-to-integer scale for 24-bit files. Use the inverse of the scale the driver
        used to make floats (8388607 for 24-bit drivers, 2147483647 / 256 for 32-bit
        containers), so untouched inputs are written bit for bit.
    */
    double pcmScale = 8388607.0;

    // BWF "bext" metadata.
    std::string description, originator = "Stage Plot Mixer", originationDate, originationTime;  // "yyyy-mm-dd", "hh:mm:ss"
    std::uint64_t timeReference = 0;  // samples since midnight at the first sample
};

struct WavMarker
{
    std::uint64_t position = 0;  // sample frame
    std::string name;
};

/** Streams a Broadcast WAV file, switching to RF64 if it passes 4 GB.

    Space for the ds64 chunk is reserved up front (as JUNK), so the switch only rewrites
    the header. updateHeader() makes the sizes on disk match what's been written so far;
    call it every few seconds so a crash loses at most that much. Not thread-safe.
*/
class WavWriter
{
public:
    WavWriter() = default;
    ~WavWriter();

    WavWriter (const WavWriter&) = delete;
    WavWriter& operator= (const WavWriter&) = delete;

    bool open (const std::filesystem::path& path, const WavSpec& spec);

    /** Writes numFrames frames; channels[ch] points at the samples for channel ch. */
    bool write (const float* const* channels, int numFrames);

    /** Writes numFrames frames of silence. */
    bool writeSilence (std::int64_t numFrames);

    bool updateHeader();

    /** Adds the markers, fixes the sizes and closes the file. */
    bool finalise (const std::vector<WavMarker>& markers = {});

    bool isOpen() const noexcept { return file != nullptr; }
    std::uint64_t getFramesWritten() const noexcept { return frames; }
    const std::string& getError() const noexcept { return error; }
    const std::filesystem::path& getPath() const noexcept { return filePath; }
    bool isRf64() const noexcept { return rf64; }

    /** Files whose RIFF size would pass this become RF64 (lowered by tests). */
    void setRf64Threshold (std::uint64_t bytes) noexcept { rf64Threshold = bytes; }
    static constexpr std::uint64_t defaultRf64Threshold = 0xffffffffull - 65536;

private:
    bool fail (const std::string& message);
    bool writeFrames (const float* const* channels, int numFrames, bool silent);
    bool writeSizes (std::uint64_t riffEnd);

    std::FILE* file = nullptr;
    std::filesystem::path filePath;
    WavSpec spec;
    int blockAlign = 0;
    std::uint64_t dataOffset = 0, factValueOffset = 0, frames = 0;
    std::uint64_t rf64Threshold = defaultRf64Threshold;
    bool rf64 = false;
    std::string error;
    std::vector<unsigned char> scratch;
};

/** What readWav() found. Samples are only loaded when asked for. */
struct WavInfo
{
    bool ok = false;
    std::string error;
    bool rf64 = false;
    int channels = 0, bitsPerSample = 0;
    double sampleRate = 0.0;
    SampleFormat format = SampleFormat::pcm24;
    std::uint64_t frames = 0, dataOffset = 0, dataBytes = 0;
    std::uint64_t timeReference = 0;
    bool hasBext = false;
    std::vector<WavMarker> markers;
    std::vector<std::vector<std::int32_t>> pcm;  // 24-bit files: raw sample values
    std::vector<std::vector<float>> samples;     // float files
};

WavInfo readWav (const std::filesystem::path& path, bool loadSamples = true);

/** Fixes the header of a file whose writer never finished (e.g. after a crash), using
    the file's length. Returns false with a reason if the file can't be repaired.
*/
bool repairWav (const std::filesystem::path& path, std::string& error,
                std::uint64_t rf64Threshold = WavWriter::defaultRf64Threshold);

/** fopen with a path that may contain any characters. */
std::FILE* openFile (const std::filesystem::path& path, const char* mode);

} // namespace spm::record
