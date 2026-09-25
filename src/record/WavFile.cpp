// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "record/WavFile.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>

static_assert (std::endian::native == std::endian::little, "WAV writing assumes a little-endian machine");

namespace spm::record
{

namespace
{

// Layout of the files we write:
//   RIFF/RF64 · JUNK/ds64 (28) · bext (602) · fmt (16, 18 or 40) · [fact (4)] · data · [cue · LIST adtl]
constexpr std::uint64_t ds64ChunkOffset = 12, ds64BodySize = 28, bextBodySize = 602;
constexpr std::uint16_t formatPcm = 1, formatFloat = 3, formatExtensible = 0xfffe;

bool seekTo (std::FILE* f, std::uint64_t position)
{
#if defined(_WIN32)
    return _fseeki64 (f, (__int64) position, SEEK_SET) == 0;
#else
    return fseeko (f, (off_t) position, SEEK_SET) == 0;
#endif
}

bool seekToEnd (std::FILE* f)
{
#if defined(_WIN32)
    return _fseeki64 (f, 0, SEEK_END) == 0;
#else
    return fseeko (f, 0, SEEK_END) == 0;
#endif
}

std::uint64_t tell (std::FILE* f)
{
#if defined(_WIN32)
    return (std::uint64_t) _ftelli64 (f);
#else
    return (std::uint64_t) ftello (f);
#endif
}

struct Bytes
{
    std::vector<unsigned char> data;

    void tag (const char* fourCC) { data.insert (data.end(), fourCC, fourCC + 4); }
    void u16 (std::uint32_t v) { for (int i = 0; i < 2; ++i) data.push_back ((unsigned char) (v >> (8 * i))); }
    void u32 (std::uint32_t v) { for (int i = 0; i < 4; ++i) data.push_back ((unsigned char) (v >> (8 * i))); }
    void u64 (std::uint64_t v) { for (int i = 0; i < 8; ++i) data.push_back ((unsigned char) (v >> (8 * i))); }
    void zeros (size_t n) { data.insert (data.end(), n, 0); }

    void text (const std::string& s, size_t width)
    {
        const auto n = std::min (s.size(), width);
        data.insert (data.end(), s.begin(), s.begin() + (std::ptrdiff_t) n);
        zeros (width - n);
    }

    bool writeTo (std::FILE* f) const { return data.empty() || std::fwrite (data.data(), 1, data.size(), f) == data.size(); }
};

std::uint32_t readU32 (const unsigned char* p) { return (std::uint32_t) p[0] | (std::uint32_t) p[1] << 8 | (std::uint32_t) p[2] << 16 | (std::uint32_t) p[3] << 24; }
std::uint16_t readU16 (const unsigned char* p) { return (std::uint16_t) (p[0] | p[1] << 8); }
std::uint64_t readU64 (const unsigned char* p) { return (std::uint64_t) readU32 (p) | (std::uint64_t) readU32 (p + 4) << 32; }

/** Where the size fields are, so the writer and repair share one routine. */
struct Layout
{
    bool hasDs64Space = false;  // a JUNK or ds64 chunk of the right size at offset 12
    std::uint64_t factValueOffset = 0;
    std::uint64_t dataOffset = 0;
};

bool writeSizeFields (std::FILE* f, const Layout& layout, std::uint64_t riffEnd, std::uint64_t dataBytes,
                      std::uint64_t frames, std::uint64_t threshold, bool& rf64)
{
    if (! rf64 && (riffEnd - 8 > threshold || dataBytes > threshold))
        rf64 = true;

    if (rf64 && ! layout.hasDs64Space)
        return false;

    auto put32 = [f] (std::uint64_t position, std::uint32_t value)
    {
        Bytes b;
        b.u32 (value);
        return seekTo (f, position) && b.writeTo (f);
    };

    if (rf64)
    {
        Bytes head;
        head.tag ("RF64");
        head.u32 (0xffffffff);
        Bytes ds64;
        ds64.tag ("ds64");
        ds64.u32 ((std::uint32_t) ds64BodySize);
        ds64.u64 (riffEnd - 8);
        ds64.u64 (dataBytes);
        ds64.u64 (frames);
        ds64.u32 (0);

        if (! (seekTo (f, 0) && head.writeTo (f) && seekTo (f, ds64ChunkOffset) && ds64.writeTo (f)
               && put32 (layout.dataOffset - 4, 0xffffffff)))
            return false;
    }
    else
    {
        if (! (put32 (4, (std::uint32_t) (riffEnd - 8)) && put32 (layout.dataOffset - 4, (std::uint32_t) dataBytes)))
            return false;
    }

    if (layout.factValueOffset != 0 && ! put32 (layout.factValueOffset, (std::uint32_t) std::min<std::uint64_t> (frames, 0xffffffff)))
        return false;

    return seekToEnd (f);
}

struct Parsed
{
    WavInfo info;
    Layout layout;
    std::uint64_t fileLength = 0;
    std::uint64_t declaredEnd = 0;  // where the header says the file ends
    int blockAlign = 0;
};

Parsed parse (std::FILE* f)
{
    Parsed p;
    auto& info = p.info;

    seekToEnd (f);
    p.fileLength = tell (f);
    seekTo (f, 0);

    unsigned char head[12];
    if (std::fread (head, 1, 12, f) != 12 || (std::memcmp (head, "RIFF", 4) != 0 && std::memcmp (head, "RF64", 4) != 0)
        || std::memcmp (head + 8, "WAVE", 4) != 0)
    {
        info.error = "Not a WAV file";
        return p;
    }

    info.rf64 = std::memcmp (head, "RF64", 4) == 0;
    p.declaredEnd = (std::uint64_t) readU32 (head + 4) + 8;

    std::uint64_t ds64Riff = 0, ds64Data = 0;
    bool haveFormat = false, haveData = false;
    std::map<std::uint32_t, std::uint64_t> cuePositions;
    std::vector<std::uint32_t> cueOrder;
    std::map<std::uint32_t, std::string> labels;

    for (std::uint64_t position = 12; position + 8 <= p.fileLength;)
    {
        unsigned char chunk[8];
        if (! seekTo (f, position) || std::fread (chunk, 1, 8, f) != 8)
            break;

        const auto size32 = readU32 (chunk + 4);
        const auto body = position + 8;
        std::uint64_t size = size32;

        auto readBody = [&] (size_t n)
        {
            std::vector<unsigned char> bytes (n);
            if (std::fread (bytes.data(), 1, n, f) != n)
                bytes.clear();
            return bytes;
        };

        if (std::memcmp (chunk, "ds64", 4) == 0 || std::memcmp (chunk, "JUNK", 4) == 0)
        {
            if (position == ds64ChunkOffset && size32 == ds64BodySize)
                p.layout.hasDs64Space = true;

            if (std::memcmp (chunk, "ds64", 4) == 0)
                if (const auto b = readBody (16); b.size() == 16)
                {
                    ds64Riff = readU64 (b.data());
                    ds64Data = readU64 (b.data() + 8);
                }
        }
        else if (std::memcmp (chunk, "fmt ", 4) == 0)
        {
            const auto b = readBody (std::min<size_t> (size32, 40));
            if (b.size() >= 16)
            {
                auto tag = readU16 (b.data());
                info.channels = readU16 (b.data() + 2);
                info.sampleRate = readU32 (b.data() + 4);
                p.blockAlign = readU16 (b.data() + 12);
                info.bitsPerSample = readU16 (b.data() + 14);
                if (tag == formatExtensible && b.size() >= 26)
                    tag = readU16 (b.data() + 24);
                info.format = tag == formatFloat ? SampleFormat::float32 : SampleFormat::pcm24;
                haveFormat = (tag == formatPcm && info.bitsPerSample == 24) || (tag == formatFloat && info.bitsPerSample == 32);
                if (! haveFormat)
                    info.error = "Unsupported sample format";
            }
        }
        else if (std::memcmp (chunk, "fact", 4) == 0)
        {
            p.layout.factValueOffset = body;
        }
        else if (std::memcmp (chunk, "bext", 4) == 0)
        {
            if (const auto b = readBody (346); b.size() == 346)
            {
                info.timeReference = readU64 (b.data() + 338);
                info.hasBext = true;
            }
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            haveData = true;
            p.layout.dataOffset = body;
            size = (info.rf64 && size32 == 0xffffffff) ? ds64Data : size32;
            size = std::min (size, p.fileLength - body);
            info.dataOffset = body;
            info.dataBytes = size;
        }
        else if (std::memcmp (chunk, "cue ", 4) == 0)
        {
            const auto b = readBody (std::min<size_t> (size32, 4 + 24 * 10000));
            const auto count = b.size() >= 4 ? std::min<size_t> (readU32 (b.data()), (b.size() - 4) / 24) : 0;
            for (size_t i = 0; i < count; ++i)
            {
                const auto* point = b.data() + 4 + 24 * i;
                cuePositions[readU32 (point)] = readU32 (point + 20);
                cueOrder.push_back (readU32 (point));
            }
        }
        else if (std::memcmp (chunk, "LIST", 4) == 0)
        {
            const auto b = readBody (std::min<size_t> (size32, 1 << 20));
            if (b.size() >= 4 && std::memcmp (b.data(), "adtl", 4) == 0)
            {
                for (size_t q = 4; q + 12 <= b.size();)
                {
                    const auto subSize = readU32 (b.data() + q + 4);
                    if (std::memcmp (b.data() + q, "labl", 4) == 0 && subSize >= 4 && q + 8 + subSize <= b.size())
                    {
                        const auto* text = reinterpret_cast<const char*> (b.data() + q + 12);
                        labels[readU32 (b.data() + q + 8)] = std::string (text, strnlen (text, subSize - 4));
                    }
                    q += 8 + subSize + (subSize & 1);
                }
            }
        }

        position = body + size + (size & 1);
    }

    if (info.rf64)
        p.declaredEnd = ds64Riff + 8;

    if (! haveFormat || ! haveData)
    {
        if (info.error.empty())
            info.error = haveFormat ? "No audio data" : "No format chunk";
        return p;
    }

    if (p.blockAlign <= 0 || p.blockAlign != info.channels * info.bitsPerSample / 8)
    {
        info.error = "Bad block size";
        return p;
    }

    info.frames = info.dataBytes / (std::uint64_t) p.blockAlign;

    for (auto id : cueOrder)
        info.markers.push_back ({ cuePositions[id], labels.count (id) != 0 ? labels[id] : std::string() });

    info.ok = true;
    return p;
}

/** For messages: path::string() can throw on Windows for names outside the code page. */
std::string pathText (const std::filesystem::path& path)
{
    const auto u = path.u8string();
    return std::string (u.begin(), u.end());
}

std::string defaultTimestamp (bool date)
{
    const auto now = std::time (nullptr);
    std::tm local {};
#if defined(_WIN32)
    localtime_s (&local, &now);
#else
    localtime_r (&now, &local);
#endif
    char text[16];
    std::strftime (text, sizeof (text), date ? "%Y-%m-%d" : "%H:%M:%S", &local);
    return text;
}

} // namespace

std::FILE* openFile (const std::filesystem::path& path, const char* mode)
{
#if defined(_WIN32)
    std::wstring wideMode (mode, mode + std::strlen (mode));
    return _wfopen (path.c_str(), wideMode.c_str());
#else
    return std::fopen (path.c_str(), mode);
#endif
}

//==============================================================================
WavWriter::~WavWriter()
{
    if (file != nullptr)
        finalise();
}

bool WavWriter::fail (const std::string& message)
{
    if (error.empty())
        error = message;
    return false;
}

bool WavWriter::open (const std::filesystem::path& path, const WavSpec& newSpec)
{
    spec = newSpec;
    filePath = path;
    frames = 0;
    rf64 = false;
    error.clear();

    const auto bytesPerSample = spec.format == SampleFormat::pcm24 ? 3 : 4;
    blockAlign = spec.channels * bytesPerSample;

    if (spec.channels < 1 || spec.channels > 256 || spec.sampleRate <= 0)
        return fail ("Bad file format");

    file = openFile (path, "wb");
    if (file == nullptr)
        return fail ("Couldn't create " + pathText (path));

    std::setvbuf (file, nullptr, _IOFBF, 1 << 20);

    const auto extensible = spec.channels > 2;
    const auto isFloat = spec.format == SampleFormat::float32;
    const auto tag = isFloat ? formatFloat : formatPcm;
    const auto fmtSize = extensible ? 40u : isFloat ? 18u : 16u;

    Bytes h;
    h.tag ("RIFF");
    h.u32 (0);
    h.tag ("WAVE");

    h.tag ("JUNK");
    h.u32 ((std::uint32_t) ds64BodySize);
    h.zeros (ds64BodySize);

    h.tag ("bext");
    h.u32 ((std::uint32_t) bextBodySize);
    const auto bextStart = h.data.size();
    h.text (spec.description, 256);
    h.text (spec.originator, 32);
    h.text ({}, 32);  // originator reference
    h.text (spec.originationDate.empty() ? defaultTimestamp (true) : spec.originationDate, 10);
    h.text (spec.originationTime.empty() ? defaultTimestamp (false) : spec.originationTime, 8);
    h.u64 (spec.timeReference);
    h.u16 (1);   // version
    h.zeros (64 + 10 + 180);  // UMID, loudness, reserved
    if (h.data.size() - bextStart != bextBodySize)
        return fail ("Internal error: bext size");

    h.tag ("fmt ");
    h.u32 (fmtSize);
    h.u16 (extensible ? formatExtensible : tag);
    h.u16 ((std::uint32_t) spec.channels);
    h.u32 ((std::uint32_t) std::lround (spec.sampleRate));
    h.u32 ((std::uint32_t) (std::lround (spec.sampleRate) * blockAlign));
    h.u16 ((std::uint32_t) blockAlign);
    h.u16 ((std::uint32_t) bytesPerSample * 8);

    if (extensible)
    {
        h.u16 (22);
        h.u16 ((std::uint32_t) bytesPerSample * 8);  // valid bits
        h.u32 (0);                                   // no speaker positions
        h.u16 (tag);                                 // sub-format GUID: tag + the standard suffix
        const unsigned char guid[] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
        h.data.insert (h.data.end(), std::begin (guid), std::end (guid));
    }
    else if (isFloat)
    {
        h.u16 (0);
    }

    Layout layout;
    if (isFloat || extensible)
    {
        h.tag ("fact");
        h.u32 (4);
        layout.factValueOffset = h.data.size();
        h.u32 (0);
    }

    h.tag ("data");
    h.u32 (0);
    dataOffset = h.data.size();

    if (! h.writeTo (file))
        return fail ("Couldn't write to " + pathText (path));

    factValueOffset = layout.factValueOffset;
    return updateHeader();
}

bool WavWriter::writeFrames (const float* const* channels, int numFrames, bool silent)
{
    if (file == nullptr)
        return fail ("File not open");

    const auto bytes = (size_t) numFrames * (size_t) blockAlign;
    scratch.resize (bytes);
    auto* out = scratch.data();

    if (silent)
    {
        std::fill (scratch.begin(), scratch.end(), 0);
    }
    else if (spec.format == SampleFormat::pcm24)
    {
        const auto scale = spec.pcmScale;
        for (int i = 0; i < numFrames; ++i)
            for (int ch = 0; ch < spec.channels; ++ch)
            {
                auto x = (double) channels[ch][i] * scale;
                x = x == x ? std::clamp (x, -8388608.0, 8388607.0) : 0.0;
                const auto v = (std::int32_t) std::lround (x);
                *out++ = (unsigned char) v;
                *out++ = (unsigned char) (v >> 8);
                *out++ = (unsigned char) (v >> 16);
            }
    }
    else
    {
        for (int i = 0; i < numFrames; ++i)
            for (int ch = 0; ch < spec.channels; ++ch)
            {
                std::memcpy (out, channels[ch] + i, 4);
                out += 4;
            }
    }

    if (std::fwrite (scratch.data(), 1, bytes, file) != bytes)
        return fail ("Couldn't write to " + pathText (filePath) + " (is the disk full?)");

    frames += (std::uint64_t) numFrames;
    return true;
}

bool WavWriter::write (const float* const* channels, int numFrames)
{
    return writeFrames (channels, numFrames, false);
}

bool WavWriter::writeSilence (std::int64_t numFrames)
{
    for (std::int64_t done = 0; done < numFrames;)
    {
        const auto n = (int) std::min<std::int64_t> (numFrames - done, 16384);
        if (! writeFrames (nullptr, n, true))
            return false;
        done += n;
    }
    return true;
}

bool WavWriter::writeSizes (std::uint64_t riffEnd)
{
    Layout layout;
    layout.hasDs64Space = true;
    layout.factValueOffset = factValueOffset;
    layout.dataOffset = dataOffset;

    if (! writeSizeFields (file, layout, riffEnd, frames * (std::uint64_t) blockAlign, frames, rf64Threshold, rf64))
        return fail ("Couldn't update " + pathText (filePath));

    return true;
}

bool WavWriter::updateHeader()
{
    if (file == nullptr)
        return false;

    if (! writeSizes (dataOffset + frames * (std::uint64_t) blockAlign))
        return false;

    if (std::fflush (file) != 0)
        return fail ("Couldn't write to " + pathText (filePath) + " (is the disk full?)");

    return true;
}

bool WavWriter::finalise (const std::vector<WavMarker>& markers)
{
    if (file == nullptr)
        return false;

    auto ok = error.empty();
    const auto dataBytes = frames * (std::uint64_t) blockAlign;

    Bytes tail;
    if (dataBytes & 1)
        tail.zeros (1);

    if (! markers.empty())
    {
        tail.tag ("cue ");
        tail.u32 ((std::uint32_t) (4 + 24 * markers.size()));
        tail.u32 ((std::uint32_t) markers.size());
        for (size_t i = 0; i < markers.size(); ++i)
        {
            const auto position = (std::uint32_t) std::min<std::uint64_t> (markers[i].position, 0xffffffff);
            tail.u32 ((std::uint32_t) i + 1);
            tail.u32 (position);
            tail.tag ("data");
            tail.u32 (0);
            tail.u32 (0);
            tail.u32 (position);
        }

        Bytes list;
        list.tag ("adtl");
        for (size_t i = 0; i < markers.size(); ++i)
        {
            const auto& name = markers[i].name;
            const auto size = (std::uint32_t) (4 + name.size() + 1);
            list.tag ("labl");
            list.u32 (size);
            list.u32 ((std::uint32_t) i + 1);
            list.data.insert (list.data.end(), name.begin(), name.end());
            list.zeros (1 + (size & 1));
        }

        tail.tag ("LIST");
        tail.u32 ((std::uint32_t) list.data.size());
        tail.data.insert (tail.data.end(), list.data.begin(), list.data.end());
    }

    ok = seekToEnd (file) && tail.writeTo (file) && ok;
    ok = writeSizes (tell (file)) && ok;
    ok = std::fflush (file) == 0 && ok;
    ok = std::fclose (file) == 0 && ok;
    file = nullptr;

    if (! ok)
        fail ("Couldn't finish writing " + pathText (filePath));
    return ok;
}

//==============================================================================
WavInfo readWav (const std::filesystem::path& path, bool loadSamples)
{
    auto* f = openFile (path, "rb");
    if (f == nullptr)
    {
        WavInfo info;
        info.error = "Couldn't open " + pathText (path);
        return info;
    }

    auto parsed = parse (f);
    auto& info = parsed.info;

    if (info.ok && loadSamples)
    {
        std::vector<unsigned char> bytes ((size_t) info.dataBytes);
        seekTo (f, info.dataOffset);
        if (std::fread (bytes.data(), 1, bytes.size(), f) != bytes.size())
        {
            info.ok = false;
            info.error = "Couldn't read the audio";
        }
        else if (info.format == SampleFormat::pcm24)
        {
            info.pcm.assign ((size_t) info.channels, std::vector<std::int32_t> ((size_t) info.frames));
            const auto* p = bytes.data();
            for (size_t i = 0; i < info.frames; ++i)
                for (int ch = 0; ch < info.channels; ++ch, p += 3)
                    info.pcm[(size_t) ch][i] = (std::int32_t) ((std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24) >> 8;
        }
        else
        {
            info.samples.assign ((size_t) info.channels, std::vector<float> ((size_t) info.frames));
            const auto* p = bytes.data();
            for (size_t i = 0; i < info.frames; ++i)
                for (int ch = 0; ch < info.channels; ++ch, p += 4)
                    std::memcpy (&info.samples[(size_t) ch][i], p, 4);
        }
    }

    std::fclose (f);
    return info;
}

bool repairWav (const std::filesystem::path& path, std::string& error, std::uint64_t rf64Threshold)
{
    auto* f = openFile (path, "r+b");
    if (f == nullptr)
    {
        error = "Couldn't open " + pathText (path);
        return false;
    }

    const auto parsed = parse (f);
    const auto& info = parsed.info;

    if (! info.ok)
    {
        std::fclose (f);
        error = info.error;
        return false;
    }

    // A finished file (or one whose header was just refreshed) needs nothing.
    if (parsed.declaredEnd == parsed.fileLength && info.dataOffset + info.dataBytes <= parsed.fileLength)
    {
        std::fclose (f);
        return true;
    }

    // Otherwise the audio runs to the end of the file; drop any partly written frame.
    const auto dataBytes = (parsed.fileLength - info.dataOffset) / (std::uint64_t) parsed.blockAlign * (std::uint64_t) parsed.blockAlign;
    const auto frames = dataBytes / (std::uint64_t) parsed.blockAlign;
    auto end = info.dataOffset + dataBytes;
    auto rf64 = info.rf64;

    std::error_code ec;
    std::fclose (f);
    std::filesystem::resize_file (path, end + (dataBytes & 1), ec);
    if (ec)
    {
        error = "Couldn't shorten " + pathText (path) + ": " + ec.message();
        return false;
    }
    end += dataBytes & 1;

    f = openFile (path, "r+b");
    if (f == nullptr)
    {
        error = "Couldn't reopen " + pathText (path);
        return false;
    }

    const auto ok = writeSizeFields (f, parsed.layout, end, dataBytes, frames, rf64Threshold, rf64);
    const auto closed = std::fclose (f) == 0;

    if (! ok || ! closed)
    {
        error = "Couldn't rewrite the header of " + pathText (path);
        return false;
    }

    return true;
}

} // namespace spm::record
