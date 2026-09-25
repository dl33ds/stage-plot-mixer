// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// The built-in effects: filters, EQ, dynamics, delay and reverb.

#include "nodes/ParamHelpers.h"

#include "engine/CompiledGraph.h"
#include "engine/Smoother.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace spm::nodes
{

using engine::ChannelSpan;
using engine::NodeProcessor;
using engine::ProcessContext;
using engine::Smoother;

namespace
{

constexpr int maxBlock = engine::CompiledGraph::defaultMaxBlockSize;
constexpr double maxSampleRate = 192000.0;
constexpr double rampSeconds = 0.02;

int rampFor (double sampleRate) { return std::max (1, (int) (sampleRate * rampSeconds)); }

/** One-pole coefficient: the fraction of the remaining distance left after one sample. */
float poleFor (double milliseconds, double sampleRate)
{
    return (float) std::exp (-1.0 / std::max (1.0, milliseconds * 0.001 * sampleRate));
}

float gainToDb (float gain) { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }
float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

//==============================================================================
/** Biquad coefficients (RBJ Audio EQ Cookbook), normalised so a0 = 1. */
struct Coefficients
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static Coefficients highPass (double rate, double freq, double q)
    {
        const auto w = 2.0 * std::numbers::pi * freq / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    static Coefficients lowPass (double rate, double freq, double q)
    {
        const auto w = 2.0 * std::numbers::pi * freq / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    static Coefficients peak (double rate, double freq, double q, double gainDb)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w = 2.0 * std::numbers::pi * freq / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a);
    }

    static Coefficients lowShelf (double rate, double freq, double gainDb)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w = 2.0 * std::numbers::pi * freq / rate, c = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);  // shelf slope 1
        const auto sa = 2.0 * std::sqrt (a) * alpha;
        return normalise (a * ((a + 1) - (a - 1) * c + sa), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - sa),
                          (a + 1) + (a - 1) * c + sa, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - sa);
    }

    static Coefficients highShelf (double rate, double freq, double gainDb)
    {
        const auto a = std::pow (10.0, gainDb / 40.0);
        const auto w = 2.0 * std::numbers::pi * freq / rate, c = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
        const auto sa = 2.0 * std::sqrt (a) * alpha;
        return normalise (a * ((a + 1) + (a - 1) * c + sa), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - sa),
                          (a + 1) - (a - 1) * c + sa, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - sa);
    }

private:
    static Coefficients normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
};

/** Transposed direct form II, in double precision so low frequencies stay clean. */
struct BiquadState
{
    double z1 = 0, z2 = 0;

    float process (const Coefficients& c, float in) noexcept
    {
        const auto x = (double) in;
        const auto y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

/** Frequencies and gains move towards their targets over a few tens of milliseconds, so
    sweeping a control doesn't zipper. Filters are recalculated every sub-block.
*/
constexpr int subBlock = 32;

float approach (float current, float target, float fraction)
{
    return current + (target - current) * fraction;
}

float approachLog (float current, float target, float fraction)
{
    return std::exp (approach (std::log (current), std::log (target), fraction));
}

//==============================================================================
/** Crossfades between the processed signal and the dry input when Bypass is switched. */
class BypassFade
{
public:
    void prepare (bool bypassed, double sampleRate) { mix.reset (bypassed ? 1.0f : 0.0f, rampFor (sampleRate)); }

    /** out = processed, dry = input; blends in place. */
    void apply (bool bypassed, const ChannelSpan& dry, const ChannelSpan& out, float* scratch) noexcept
    {
        mix.setTarget (bypassed ? 1.0f : 0.0f);
        const auto n = out.numSamples;

        if (! mix.isRamping())
        {
            if (mix.getCurrent() >= 1.0f)
                for (int ch = 0; ch < out.numChannels; ++ch)
                    std::copy_n (dry.channel (ch), n, out.channel (ch));

            return;
        }

        for (int i = 0; i < n; ++i)
            scratch[i] = mix.next();

        for (int ch = 0; ch < out.numChannels; ++ch)
        {
            const auto* d = dry.channel (ch);
            auto* o = out.channel (ch);
            for (int i = 0; i < n; ++i)
                o[i] += (d[i] - o[i]) * scratch[i];
        }
    }

    bool isFullyBypassed() const noexcept { return ! mix.isRamping() && mix.getCurrent() >= 1.0f; }

private:
    Smoother mix;
};

//==============================================================================
class FilterProcessor final : public NodeProcessor
{
public:
    enum Param { hpOn, hpFreq, lpOn, lpFreq, slope, bypass, channels, numParams };

    explicit FilterProcessor (int numChannels)
        : NodeProcessor ({ numChannels }, { numChannels }, numParams),
          states ((size_t) numChannels), scratch ((size_t) maxBlock) {}

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            hp = param (hpFreq);
            lp = param (lpFreq);
            hpMix.reset (param (hpOn) >= 0.5f ? 1.0f : 0.0f, rampFor (rate));
            lpMix.reset (param (lpOn) >= 0.5f ? 1.0f : 0.0f, rampFor (rate));
            fade.prepare (param (bypass) >= 0.5f, rate);
            initialised = true;
        }

        hpMix.setTarget (param (hpOn) >= 0.5f ? 1.0f : 0.0f);
        lpMix.setTarget (param (lpOn) >= 0.5f ? 1.0f : 0.0f);
        const auto steep = param (slope) >= 0.5f;
        const auto fraction = 1.0f - poleFor (20.0, rate / subBlock);
        const auto nyquistLimit = (float) (rate * 0.45);

        for (int start = 0; start < out.numSamples; start += subBlock)
        {
            const auto n = std::min (subBlock, out.numSamples - start);
            hp = approachLog (hp, std::min (param (hpFreq), nyquistLimit), fraction);
            lp = approachLog (lp, std::min (param (lpFreq), nyquistLimit), fraction);

            // Butterworth: one stage for 12 dB/oct, two for 24 dB/oct.
            const std::array<double, 2> q = steep ? std::array<double, 2> { 0.5412, 1.3066 } : std::array<double, 2> { 0.7071, 0.7071 };
            const std::array<Coefficients, 2> hpc { Coefficients::highPass (rate, hp, q[0]), Coefficients::highPass (rate, hp, q[1]) };
            const std::array<Coefficients, 2> lpc { Coefficients::lowPass (rate, lp, q[0]), Coefficients::lowPass (rate, lp, q[1]) };

            // Switching a filter in or out crossfades; the filters run all the time so they're
            // ready. The mix gains are shared by all channels.
            std::array<float, subBlock> hpGain {}, lpGain {};
            for (int i = 0; i < n; ++i)
            {
                hpGain[(size_t) i] = hpMix.next();
                lpGain[(size_t) i] = lpMix.next();
            }

            for (int ch = 0; ch < out.numChannels; ++ch)
            {
                auto& s = states[(size_t) ch];
                const auto* src = in.channel (ch) + start;
                auto* dst = out.channel (ch) + start;

                for (int i = 0; i < n; ++i)
                {
                    auto x = src[i];

                    auto h = s.hp[0].process (hpc[0], x);
                    h = steep ? s.hp[1].process (hpc[1], h) : h;
                    x += (h - x) * hpGain[(size_t) i];

                    auto l = s.lp[0].process (lpc[0], x);
                    l = steep ? s.lp[1].process (lpc[1], l) : l;
                    x += (l - x) * lpGain[(size_t) i];

                    dst[i] = x;
                }
            }
        }

        fade.apply (param (bypass) >= 0.5f, in, out, scratch.data());
    }

    void reset() noexcept override
    {
        initialised = false;
        for (auto& s : states)
            s = {};
    }

private:
    struct ChannelState
    {
        std::array<BiquadState, 2> hp, lp;
    };

    std::vector<ChannelState> states;
    std::vector<float> scratch;
    Smoother hpMix, lpMix;
    BypassFade fade;
    float hp = 80.0f, lp = 12000.0f;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
class EqProcessor final : public NodeProcessor
{
public:
    enum Param { lowGain, lowFreq, lowMidGain, lowMidFreq, lowMidQ, highMidGain, highMidFreq, highMidQ,
                 highGain, highFreq, bypass, channels, numParams };

    static constexpr int numBands = 4;

    explicit EqProcessor (int numChannels)
        : NodeProcessor ({ numChannels }, { numChannels }, numParams),
          states ((size_t) numChannels), scratch ((size_t) maxBlock) {}

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            for (int b = 0; b < numBands; ++b)
                current[(size_t) b] = target (b);
            fade.prepare (param (bypass) >= 0.5f, rate);
            initialised = true;
        }

        const auto fraction = 1.0f - poleFor (20.0, rate / subBlock);

        for (int start = 0; start < out.numSamples; start += subBlock)
        {
            const auto n = std::min (subBlock, out.numSamples - start);
            std::array<Coefficients, numBands> c;

            for (int b = 0; b < numBands; ++b)
            {
                const auto t = target (b);
                auto& band = current[(size_t) b];
                band.freq = approachLog (band.freq, t.freq, fraction);
                band.gain = approach (band.gain, t.gain, fraction);
                band.q = approachLog (band.q, t.q, fraction);
                c[(size_t) b] = coefficientsFor (b, band);
            }

            for (int ch = 0; ch < out.numChannels; ++ch)
            {
                auto& s = states[(size_t) ch];
                const auto* src = in.channel (ch) + start;
                auto* dst = out.channel (ch) + start;

                for (int i = 0; i < n; ++i)
                {
                    auto x = src[i];
                    for (int b = 0; b < numBands; ++b)
                        x = s[(size_t) b].process (c[(size_t) b], x);
                    dst[i] = x;
                }
            }
        }

        fade.apply (param (bypass) >= 0.5f, in, out, scratch.data());
    }

    void reset() noexcept override
    {
        initialised = false;
        for (auto& s : states)
            s = {};
    }

private:
    struct Band
    {
        float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    };

    Band target (int band) const noexcept
    {
        const auto limit = (float) (rate * 0.45);

        switch (band)
        {
            case 0:  return { std::min (param (lowFreq), limit), param (lowGain), 0.7071f };
            case 1:  return { std::min (param (lowMidFreq), limit), param (lowMidGain), param (lowMidQ) };
            case 2:  return { std::min (param (highMidFreq), limit), param (highMidGain), param (highMidQ) };
            default: return { std::min (param (highFreq), limit), param (highGain), 0.7071f };
        }
    }

    Coefficients coefficientsFor (int band, const Band& b) const
    {
        switch (band)
        {
            case 0:  return Coefficients::lowShelf (rate, b.freq, b.gain);
            case 3:  return Coefficients::highShelf (rate, b.freq, b.gain);
            default: return Coefficients::peak (rate, b.freq, b.q, b.gain);
        }
    }

    std::vector<std::array<BiquadState, numBands>> states;
    std::array<Band, numBands> current;
    std::vector<float> scratch;
    BypassFade fade;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
/** Feed-forward compressor with a soft knee. All channels share one gain (linked), so
    the stereo image doesn't shift.
*/
class CompressorProcessor final : public NodeProcessor
{
public:
    enum Param { threshold, ratio, attack, release, knee, makeup, bypass, channels, numParams };

    explicit CompressorProcessor (int numChannels)
        : NodeProcessor ({ numChannels }, { numChannels }, numParams), scratch ((size_t) maxBlock) {}

    bool hasGainReduction() const noexcept override { return true; }

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            makeupGain.reset (dbToGain (param (makeup)), rampFor (rate));
            fade.prepare (param (bypass) >= 0.5f, rate);
            reduction = 0.0f;
            initialised = true;
        }

        const auto thr = param (threshold);
        const auto slope = 1.0f / std::max (1.0f, param (ratio)) - 1.0f;  // dB of change per dB over
        const auto width = param (knee);
        const auto attackPole = poleFor (param (attack), rate);
        const auto releasePole = poleFor (param (release), rate);
        makeupGain.setTarget (dbToGain (param (makeup)));

        float most = 0.0f;

        for (int i = 0; i < out.numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < in.numChannels; ++ch)
                level = std::max (level, std::abs (in.channel (ch)[i]));

            const auto over = gainToDb (level) - thr;
            float wanted;  // gain reduction in dB, 0 or more

            if (2.0f * over <= -width)
                wanted = 0.0f;
            else if (2.0f * std::abs (over) < width)
                wanted = -slope * (over + width / 2.0f) * (over + width / 2.0f) / (2.0f * width);
            else
                wanted = -slope * over;

            const auto pole = wanted > reduction ? attackPole : releasePole;
            reduction = wanted + (reduction - wanted) * pole;
            most = std::max (most, reduction);

            const auto g = dbToGain (-reduction) * makeupGain.next();
            for (int ch = 0; ch < out.numChannels; ++ch)
                out.channel (ch)[i] = in.channel (ch)[i] * g;
        }

        fade.apply (param (bypass) >= 0.5f, in, out, scratch.data());
        reportGainReduction (fade.isFullyBypassed() ? 0.0f : most);
    }

    void reset() noexcept override { initialised = false; }

private:
    std::vector<float> scratch;
    Smoother makeupGain;
    BypassFade fade;
    float reduction = 0.0f;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
/** Look-ahead peak limiter. The gain for each sample is the lowest any sample in the
    look-ahead window needs, smoothed by a moving average over the same window, so it is
    already down when a peak arrives and the output never goes over the ceiling.
*/
class LimiterProcessor final : public NodeProcessor
{
public:
    enum Param { inputGain, ceiling, release, bypass, channels, numParams };

    static constexpr int window = 65;               // samples the gain looks at: the peak and 64 ahead of it
    static constexpr int lookAhead = window - 1;    // the audio's delay

    explicit LimiterProcessor (int numChannels)
        : NodeProcessor ({ numChannels }, { numChannels }, numParams),
          scaledLine ((size_t) numChannels * lookAhead, 0.0f), dryLine ((size_t) numChannels * lookAhead, 0.0f),
          scratch ((size_t) maxBlock), dryScratch ((size_t) numChannels * maxBlock),
          dryPointers ((size_t) numChannels)
    {
        for (size_t ch = 0; ch < dryPointers.size(); ++ch)
            dryPointers[ch] = dryScratch.data() + ch * (size_t) maxBlock;
    }

    int getLatencySamples() const noexcept override { return lookAhead; }
    bool hasGainReduction() const noexcept override { return true; }

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            drive.reset (dbToGain (param (inputGain)), rampFor (rate));
            fade.prepare (param (bypass) >= 0.5f, rate);
            initialised = true;
        }

        drive.setTarget (dbToGain (param (inputGain)));
        const auto ceilingGain = dbToGain (param (ceiling));
        const auto releasePole = poleFor (param (release), rate);
        const auto numChannels = out.numChannels;
        float lowest = 1.0f;

        for (int i = 0; i < out.numSamples; ++i)
        {
            const auto g = drive.next();
            float peak = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto x = in.channel (ch)[i];
                const auto scaled = x * g;
                peak = std::max (peak, std::abs (scaled));

                // Delay by the look-ahead: read the oldest sample, then overwrite it.
                auto& s = scaledLine[(size_t) ch * lookAhead + (size_t) linePos];
                auto& d = dryLine[(size_t) ch * lookAhead + (size_t) linePos];
                out.channel (ch)[i] = s;
                dryPointers[(size_t) ch][i] = d;
                s = scaled;
                d = x;
            }

            if (++linePos == lookAhead)
                linePos = 0;

            const auto needed = peak > ceilingGain ? ceilingGain / peak : 1.0f;
            const auto minimum = pushMinimum (needed);

            // Release: recover slowly, but never above what the window needs.
            released = minimum < released ? minimum : minimum + (released - minimum) * releasePole;

            // Moving average over the window.
            sum += released - history[(size_t) pos];
            history[(size_t) pos] = released;
            const auto gain = (float) std::min (1.0, sum / window);
            lowest = std::min (lowest, gain);

            if (++pos == window)
            {
                pos = 0;
                sum = 0.0;  // recalculated each lap so rounding can't drift
                for (auto h : history)
                    sum += h;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto y = out.channel (ch)[i] * gain;
                out.channel (ch)[i] = std::clamp (y, -ceilingGain, ceilingGain);
            }
        }

        const ChannelSpan dry { dryPointers.data(), numChannels, out.numSamples };
        fade.apply (param (bypass) >= 0.5f, dry, out, scratch.data());
        reportGainReduction (fade.isFullyBypassed() ? 0.0f : -gainToDb (lowest));
    }

    void reset() noexcept override
    {
        initialised = false;
        std::fill (scaledLine.begin(), scaledLine.end(), 0.0f);
        std::fill (dryLine.begin(), dryLine.end(), 0.0f);
        history.fill (1.0f);
        sum = window;
        released = 1.0f;
        pos = linePos = 0;
        queueHead = queueCount = 0;
        sampleIndex = 0;
    }

private:
    /** Sliding-window minimum of the last `window` values (a monotonic queue). */
    float pushMinimum (float value) noexcept
    {
        while (queueCount > 0 && queueValue[(size_t) back()] >= value)
            --queueCount;

        const auto slot = (queueHead + queueCount) % window;
        queueValue[(size_t) slot] = value;
        queueIndex[(size_t) slot] = sampleIndex;
        ++queueCount;

        while (queueIndex[(size_t) queueHead] <= sampleIndex - window)
        {
            queueHead = (queueHead + 1) % window;
            --queueCount;
        }

        ++sampleIndex;
        return queueValue[(size_t) queueHead];
    }

    int back() const noexcept { return (queueHead + queueCount - 1) % window; }

    std::vector<float> scaledLine, dryLine, scratch, dryScratch;
    std::vector<float*> dryPointers;
    std::array<float, window> history = [] { std::array<float, window> a {}; a.fill (1.0f); return a; }();
    std::array<float, window> queueValue {};
    std::array<std::int64_t, window> queueIndex {};
    int queueHead = 0, queueCount = 0;
    std::int64_t sampleIndex = 0;
    double sum = window;
    float released = 1.0f;
    int pos = 0, linePos = 0;
    Smoother drive;
    BypassFade fade;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
/** Noise gate with hold and hysteresis; channels are linked. */
class GateProcessor final : public NodeProcessor
{
public:
    enum Param { threshold, range, attack, hold, release, bypass, channels, numParams };

    static constexpr float hysteresisDb = 4.0f;

    explicit GateProcessor (int numChannels)
        : NodeProcessor ({ numChannels }, { numChannels }, numParams), scratch ((size_t) maxBlock) {}

    bool hasGainReduction() const noexcept override { return true; }

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            fade.prepare (param (bypass) >= 0.5f, rate);
            gainDb = param (range);
            envelope = 0.0f;
            open = false;
            initialised = true;
        }

        const auto openLevel = dbToGain (param (threshold));
        const auto closeLevel = dbToGain (param (threshold) - hysteresisDb);
        const auto closedDb = param (range);
        const auto attackPole = poleFor (param (attack), rate);
        const auto releasePole = poleFor (param (release), rate);
        const auto detectorPole = poleFor (10.0, rate);
        const auto holdSamples = (int) (param (hold) * 0.001 * rate);
        float most = 0.0f;

        for (int i = 0; i < out.numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < in.numChannels; ++ch)
                level = std::max (level, std::abs (in.channel (ch)[i]));

            envelope = level > envelope ? level : envelope * detectorPole;

            if (envelope >= openLevel)
            {
                open = true;
                holdLeft = holdSamples;
            }
            else if (envelope < closeLevel)
            {
                if (holdLeft > 0)
                    --holdLeft;
                else
                    open = false;
            }

            const auto wanted = open ? 0.0f : closedDb;
            gainDb = wanted + (gainDb - wanted) * (wanted > gainDb ? attackPole : releasePole);
            most = std::max (most, -gainDb);

            const auto g = gainDb <= -79.9f ? 0.0f : dbToGain (gainDb);
            for (int ch = 0; ch < out.numChannels; ++ch)
                out.channel (ch)[i] = in.channel (ch)[i] * g;
        }

        fade.apply (param (bypass) >= 0.5f, in, out, scratch.data());
        reportGainReduction (fade.isFullyBypassed() ? 0.0f : most);
    }

    void reset() noexcept override { initialised = false; holdLeft = 0; }

private:
    std::vector<float> scratch;
    BypassFade fade;
    float gainDb = -80.0f, envelope = 0.0f;
    int holdLeft = 0;
    bool open = false;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
/** Echo with feedback. Changing the time glides (like tape) rather than clicking. */
class DelayProcessor final : public NodeProcessor
{
public:
    enum Param { time, feedback, mix, channels, numParams };

    static constexpr double maxSeconds = 2.0;

    explicit DelayProcessor (int numChannels)
        : NodeProcessor ({ numChannels }, { numChannels }, numParams),
          length ((int) (maxSeconds * maxSampleRate) + 4),
          lines ((size_t) numChannels * (size_t) length, 0.0f) {}

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            delay = targetDelay();
            feedbackGain.reset (param (feedback) / 100.0f, rampFor (rate));
            wet.reset (param (mix) / 100.0f, rampFor (rate));
            initialised = true;
        }

        feedbackGain.setTarget (param (feedback) / 100.0f);
        wet.setTarget (param (mix) / 100.0f);
        const auto glide = 1.0f - poleFor (50.0, rate);
        const auto wanted = targetDelay();

        for (int i = 0; i < out.numSamples; ++i)
        {
            delay += (wanted - delay) * glide;
            const auto fb = feedbackGain.next();
            const auto w = wet.next();

            auto readPos = (float) write - delay;
            if (readPos < 0.0f)
                readPos += (float) length;

            const auto r0 = (int) readPos;
            const auto frac = readPos - (float) r0;
            const auto r1 = r0 + 1 == length ? 0 : r0 + 1;

            for (int ch = 0; ch < out.numChannels; ++ch)
            {
                auto* line = lines.data() + (size_t) ch * (size_t) length;
                const auto echo = line[r0] + (line[r1] - line[r0]) * frac;
                const auto x = in.channel (ch)[i];
                line[write] = x + echo * fb;
                out.channel (ch)[i] = x + (echo - x) * w;
            }

            if (++write == length)
                write = 0;
        }
    }

    void reset() noexcept override
    {
        initialised = false;
        std::fill (lines.begin(), lines.end(), 0.0f);
        write = 0;
    }

private:
    float targetDelay() const noexcept
    {
        return (float) std::clamp (param (time) * 0.001 * rate, 1.0, (double) length - 2.0);
    }

    int length;
    std::vector<float> lines;
    int write = 0;
    float delay = 1.0f;
    Smoother feedbackGain, wet;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
/** Stereo room reverb: the Freeverb algorithm (Jezar at Dreampoint, public domain),
    with its delay lengths scaled for the sample rate.
*/
class ReverbProcessor final : public NodeProcessor
{
public:
    enum Param { size, damping, width, mix, inputs, numParams };

    static constexpr int numCombs = 8, numAllPasses = 4, stereoSpread = 23;
    static constexpr std::array<int, numCombs> combTuning { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr std::array<int, numAllPasses> allPassTuning { 556, 441, 341, 225 };

    explicit ReverbProcessor (int numInputs)
        : NodeProcessor ({ numInputs }, { 2 }, numParams)
    {
        const auto scale = maxSampleRate / 44100.0;

        for (int side = 0; side < 2; ++side)
        {
            for (int c = 0; c < numCombs; ++c)
                combs[(size_t) side][(size_t) c].buffer.resize ((size_t) ((combTuning[(size_t) c] + stereoSpread) * scale) + 2, 0.0f);

            for (int a = 0; a < numAllPasses; ++a)
                allPasses[(size_t) side][(size_t) a].buffer.resize ((size_t) ((allPassTuning[(size_t) a] + stereoSpread) * scale) + 2, 0.0f);
        }
    }

    void process (const ProcessContext& ctx, const ChannelSpan* inputSpans, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputSpans[0];
        const auto& out = outputs[0];

        if (! initialised || rate != ctx.sampleRate)
        {
            rate = ctx.sampleRate;
            const auto scale = rate / 44100.0;

            for (int side = 0; side < 2; ++side)
            {
                const auto spread = side == 0 ? 0 : stereoSpread;

                for (int c = 0; c < numCombs; ++c)
                    combs[(size_t) side][(size_t) c].setLength ((int) ((combTuning[(size_t) c] + spread) * scale));

                for (int a = 0; a < numAllPasses; ++a)
                    allPasses[(size_t) side][(size_t) a].setLength ((int) ((allPassTuning[(size_t) a] + spread) * scale));
            }

            roomSize.reset (roomFeedback(), rampFor (rate));
            wet.reset (param (mix) / 100.0f, rampFor (rate));
            initialised = true;
        }

        roomSize.setTarget (roomFeedback());
        wet.setTarget (param (mix) / 100.0f);

        const auto damp = param (damping) / 100.0f * 0.4f;
        const auto spread = param (width) / 100.0f;
        const auto wet1 = 3.0f * (spread / 2.0f + 0.5f), wet2 = 3.0f * ((1.0f - spread) / 2.0f);
        constexpr float fixedGain = 0.015f;

        const auto* inL = in.channel (0);
        const auto* inR = in.numChannels > 1 ? in.channel (1) : in.channel (0);

        for (int i = 0; i < out.numSamples; ++i)
        {
            const auto feedback = roomSize.next();
            const auto w = wet.next();
            const auto dryL = inL[i], dryR = inR[i];
            const auto input = (dryL + dryR) * fixedGain;

            std::array<float, 2> result {};

            for (int side = 0; side < 2; ++side)
            {
                float acc = 0.0f;
                for (auto& comb : combs[(size_t) side])
                    acc += comb.process (input, feedback, damp);

                for (auto& allPass : allPasses[(size_t) side])
                    acc = allPass.process (acc);

                result[(size_t) side] = acc;
            }

            const auto left = result[0] * wet1 + result[1] * wet2;
            const auto right = result[1] * wet1 + result[0] * wet2;
            out.channel (0)[i] = dryL + (left - dryL) * w;
            out.channel (1)[i] = dryR + (right - dryR) * w;
        }
    }

    void reset() noexcept override
    {
        initialised = false;

        for (auto& side : combs)
            for (auto& comb : side)
                comb.clear();

        for (auto& side : allPasses)
            for (auto& allPass : side)
                allPass.clear();
    }

private:
    float roomFeedback() const noexcept { return param (size) / 100.0f * 0.28f + 0.7f; }

    struct Comb
    {
        std::vector<float> buffer;
        int length = 1, index = 0;
        float store = 0.0f;

        void setLength (int n) noexcept { length = std::clamp (n, 1, (int) buffer.size()); clear(); }
        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); index = 0; store = 0.0f; }

        float process (float input, float feedback, float damp) noexcept
        {
            const auto output = buffer[(size_t) index];
            store = output * (1.0f - damp) + store * damp;
            buffer[(size_t) index] = input + store * feedback;
            if (++index >= length)
                index = 0;
            return output;
        }
    };

    struct AllPass
    {
        std::vector<float> buffer;
        int length = 1, index = 0;

        void setLength (int n) noexcept { length = std::clamp (n, 1, (int) buffer.size()); clear(); }
        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); index = 0; }

        float process (float input) noexcept
        {
            const auto buffered = buffer[(size_t) index];
            buffer[(size_t) index] = input + buffered * 0.5f;
            if (++index >= length)
                index = 0;
            return buffered - input;
        }
    };

    std::array<std::array<Comb, numCombs>, 2> combs;
    std::array<std::array<AllPass, numAllPasses>, 2> allPasses;
    Smoother roomSize, wet;
    double rate = 48000.0;
    bool initialised = false;
};

//==============================================================================
ParamSpec frequencyParam (std::string id, std::string name, float minHz, float maxHz, float def, std::string tooltip = {})
{
    return notOnFace (valueParam (std::move (id), std::move (name), minHz, maxHz, def, "Hz", true, std::move (tooltip)));
}

ParamSpec eqGain (std::string id, std::string name)
{
    return decibelParam (std::move (id), std::move (name), -18.0f, 18.0f, 0.0f, false);
}

ParamSpec qParam (std::string id, std::string name)
{
    return notOnFace (valueParam (std::move (id), std::move (name), 0.3f, 10.0f, 1.0f, {}, true,
                                  "Width of the band: higher is narrower"));
}

ParamSpec bypassParam()
{
    return toggleParam ("bypass", "Bypass", false, "Passes the signal through unprocessed");
}

ParamSpec channelsParam (int maxChannels, int def, std::string id = "channels", std::string name = "Channels")
{
    return integerParam (std::move (id), std::move (name), 1, maxChannels, def, true);
}

/** Registers an effect whose output has as many channels as its input (the last parameter). */
void addInsert (NodeRegistry& registry, std::string_view id, std::string name, std::string icon, std::string description,
                std::vector<ParamSpec> params,
                std::unique_ptr<NodeProcessor> (*create) (int channels))
{
    NodeType t;
    t.id = id;
    t.name = std::move (name);
    t.category = "Processing";
    t.icon = std::move (icon);
    t.description = std::move (description);
    t.params = std::move (params);
    const auto channelsIndex = t.params.size() - 1;
    const auto maxChannels = (int) t.params.back().maxValue;

    t.layout = [channelsIndex, maxChannels] (const ParamValues& v)
    {
        const auto n = channelsIndex < v.size() ? std::clamp ((int) v[channelsIndex], 1, maxChannels) : 1;
        return PortLayout { { { "In", n } }, { { "Out", n } } };
    };
    t.create = [create] (const ParamValues&, const PortLayout& l) { return create (l.inputs[0].channels); };
    registry.add (std::move (t));
}

} // namespace

void addEffectNodes (NodeRegistry& registry)
{
    addInsert (registry, types::filter, "Filter", "funnel",
               "High-pass and low-pass filters: take out rumble and hiss.",
               { toggleParam ("hp", "High-pass", true, "Cuts everything below the high-pass frequency"),
                 withTooltip (valueParam ("hpFreq", "High-pass freq", 20.0f, 2000.0f, 80.0f, "Hz", true), "Below this, the signal is cut"),
                 toggleParam ("lp", "Low-pass", false, "Cuts everything above the low-pass frequency"),
                 withTooltip (valueParam ("lpFreq", "Low-pass freq", 1000.0f, 20000.0f, 12000.0f, "Hz", true), "Above this, the signal is cut"),
                 notOnFace (choiceParam ("slope", "Slope", { "12 dB/oct", "24 dB/oct" }, 0, "How steeply the filters cut")),
                 bypassParam(),
                 channelsParam (maxPortChannels, 1) },
               [] (int n) -> std::unique_ptr<NodeProcessor> { return std::make_unique<FilterProcessor> (n); });

    addInsert (registry, types::eq, "EQ", "audio-lines",
               "Four-band equaliser: low shelf, two bell bands and a high shelf. Frequencies and widths are in the inspector.",
               { eqGain ("lowGain", "Low"), frequencyParam ("lowFreq", "Low freq", 20.0f, 1000.0f, 100.0f),
                 eqGain ("lowMidGain", "Low mid"), frequencyParam ("lowMidFreq", "Low mid freq", 100.0f, 5000.0f, 400.0f), qParam ("lowMidQ", "Low mid Q"),
                 eqGain ("highMidGain", "High mid"), frequencyParam ("highMidFreq", "High mid freq", 500.0f, 16000.0f, 2500.0f), qParam ("highMidQ", "High mid Q"),
                 eqGain ("highGain", "High"), frequencyParam ("highFreq", "High freq", 1000.0f, 20000.0f, 8000.0f),
                 bypassParam(),
                 channelsParam (maxPortChannels, 1) },
               [] (int n) -> std::unique_ptr<NodeProcessor> { return std::make_unique<EqProcessor> (n); });

    addInsert (registry, types::compressor, "Compressor", "chevrons-down-up",
               "Evens out the level: turns down whatever goes over the threshold. Channels are linked.",
               { thresholdParam ("threshold", "Threshold", -60.0f, 0.0f, -20.0f, "Above this level the signal is turned down"),
                 valueParam ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", true, "4:1 means 4 dB over the threshold comes out as 1 dB over"),
                 notOnFace (valueParam ("attack", "Attack", 0.1f, 100.0f, 10.0f, "ms", true, "How fast it turns down")),
                 notOnFace (valueParam ("release", "Release", 10.0f, 2000.0f, 150.0f, "ms", true, "How fast it recovers")),
                 notOnFace (thresholdParam ("knee", "Knee", 0.0f, 24.0f, 6.0f, "How gradually it starts around the threshold")),
                 withTooltip (decibelParam ("makeup", "Makeup", 0.0f, 24.0f, 0.0f, false), "Turns the result back up"),
                 bypassParam(),
                 channelsParam (maxPortChannels, 1) },
               [] (int n) -> std::unique_ptr<NodeProcessor> { return std::make_unique<CompressorProcessor> (n); });

    addInsert (registry, types::limiter, "Limiter", "brick-wall",
               "Stops peaks from going over the ceiling. Looks ahead 64 samples, which other paths are delayed to match.",
               { withTooltip (decibelParam ("input", "Input gain", 0.0f, 24.0f, 0.0f, false), "Pushes the signal into the limiter"),
                 withTooltip (thresholdParam ("ceiling", "Ceiling", -24.0f, 0.0f, -1.0f), "The output never goes above this"),
                 notOnFace (valueParam ("release", "Release", 1.0f, 1000.0f, 50.0f, "ms", true, "How fast it recovers")),
                 bypassParam(),
                 channelsParam (maxPortChannels, 2) },
               [] (int n) -> std::unique_ptr<NodeProcessor> { return std::make_unique<LimiterProcessor> (n); });

    addInsert (registry, types::gate, "Gate", "door-open",
               "Silences the signal when it drops below the threshold, e.g. to cut spill between drum hits.",
               { thresholdParam ("threshold", "Threshold", -80.0f, 0.0f, -50.0f, "Below this level the gate closes"),
                 thresholdParam ("range", "Range", -80.0f, 0.0f, -80.0f, "How far it turns down when closed"),
                 notOnFace (valueParam ("attack", "Attack", 0.05f, 50.0f, 0.5f, "ms", true, "How fast it opens")),
                 notOnFace (valueParam ("hold", "Hold", 0.0f, 500.0f, 50.0f, "ms", false, "How long it stays open after the signal drops")),
                 notOnFace (valueParam ("release", "Release", 5.0f, 2000.0f, 100.0f, "ms", true, "How fast it closes")),
                 bypassParam(),
                 channelsParam (maxPortChannels, 1) },
               [] (int n) -> std::unique_ptr<NodeProcessor> { return std::make_unique<GateProcessor> (n); });

    addInsert (registry, types::delay, "Delay", "timer",
               "Echoes, with feedback for repeats.",
               { valueParam ("time", "Time", 1.0f, 2000.0f, 350.0f, "ms", true, "Time between echoes"),
                 valueParam ("feedback", "Feedback", 0.0f, 95.0f, 30.0f, "%", false, "How much of each echo repeats"),
                 valueParam ("mix", "Mix", 0.0f, 100.0f, 30.0f, "%", false, "0% is dry, 100% is only the echoes"),
                 channelsParam (2, 1) },
               [] (int n) -> std::unique_ptr<NodeProcessor> { return std::make_unique<DelayProcessor> (n); });

    {
        NodeType t;
        t.id = types::reverb;
        t.name = "Reverb";
        t.category = "Processing";
        t.icon = "waves";
        t.description = "Stereo room reverb. Often fed from a send, with Mix at 100%.";
        t.params = { valueParam ("size", "Size", 0.0f, 100.0f, 50.0f, "%", false, "Room size: bigger rings longer"),
                     valueParam ("damping", "Damping", 0.0f, 100.0f, 50.0f, "%", false, "Higher is darker"),
                     notOnFace (valueParam ("width", "Width", 0.0f, 100.0f, 100.0f, "%", false, "Stereo width of the reverb")),
                     valueParam ("mix", "Mix", 0.0f, 100.0f, 25.0f, "%", false, "0% is dry, 100% is only the reverb"),
                     channelsParam (2, 2, "inputs", "Input channels") };
        t.layout = [] (const ParamValues& v)
        {
            const auto n = v.size() > 4 ? std::clamp ((int) v[4], 1, 2) : 2;
            return PortLayout { { { "In", n } }, { { "Out", 2 } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<ReverbProcessor> (l.inputs[0].channels);
        };
        registry.add (std::move (t));
    }
}

} // namespace spm::nodes
