// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "nodes/NodeTypes.h"

#include "engine/CompiledGraph.h"
#include "engine/Smoother.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace spm::nodes
{

using engine::ChannelSpan;
using engine::NodeProcessor;
using engine::ProcessContext;
using engine::Smoother;

//==============================================================================
int NodeType::paramIndex (std::string_view paramId) const noexcept
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == paramId)
            return (int) i;

    return -1;
}

ParamValues NodeType::defaults() const
{
    ParamValues values;
    for (const auto& p : params)
        values.push_back (p.defaultValue);
    return values;
}

ParamValues NodeType::sanitise (ParamValues values) const
{
    values.resize (params.size());

    for (size_t i = 0; i < params.size(); ++i)
    {
        const auto& p = params[i];
        auto v = std::isfinite (values[i]) ? values[i] : p.defaultValue;
        v = std::clamp (v, p.minValue, p.maxValue);

        if (p.kind != ParamKind::continuous)
            v = std::round (v);

        values[i] = v;
    }

    return values;
}

bool NodeType::sameStructure (const ParamValues& a, const ParamValues& b) const noexcept
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].structural && (i >= a.size() || i >= b.size() || a[i] != b[i]))
            return false;

    return true;
}

const NodeType* NodeRegistry::find (std::string_view typeId) const noexcept
{
    for (const auto& t : types)
        if (t.id == typeId)
            return &t;

    return nullptr;
}

//==============================================================================
namespace
{

constexpr double gainRampSeconds = 0.02;

int rampFor (double sampleRate) { return std::max (1, (int) (sampleRate * gainRampSeconds)); }

/** Applies one smoothed gain to every channel while copying in → out. */
void copyWithGain (const ChannelSpan& in, const ChannelSpan& out, Smoother& gain, float* scratch)
{
    const auto n = out.numSamples;

    if (gain.isRamping())
    {
        for (int i = 0; i < n; ++i)
            scratch[i] = gain.next();

        for (int ch = 0; ch < out.numChannels; ++ch)
        {
            const auto* src = in.channel (ch);
            auto* dst = out.channel (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = src[i] * scratch[i];
        }
    }
    else
    {
        const auto g = gain.getCurrent();
        for (int ch = 0; ch < out.numChannels; ++ch)
        {
            const auto* src = in.channel (ch);
            auto* dst = out.channel (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = src[i] * g;
        }
    }
}

//==============================================================================
class HardwareInputProcessor final : public NodeProcessor
{
public:
    HardwareInputProcessor (int channels, int firstParam)
        : NodeProcessor ({}, { channels }, 2), first (firstParam) {}

    void process (const ProcessContext& ctx, const ChannelSpan*, const ChannelSpan* outputs) noexcept override
    {
        const auto& out = outputs[0];
        const auto start = (int) param (first) - 1;

        for (int ch = 0; ch < out.numChannels; ++ch)
        {
            const auto deviceChannel = start + ch;

            if (deviceChannel >= 0 && deviceChannel < ctx.deviceInputs.numChannels)
                std::copy_n (ctx.deviceInputs.channel (deviceChannel), out.numSamples, out.channel (ch));
            else
                std::fill_n (out.channel (ch), out.numSamples, 0.0f);
        }
    }

private:
    int first;
};

class HardwareOutputProcessor final : public NodeProcessor
{
public:
    HardwareOutputProcessor (int channels, int firstParam)
        : NodeProcessor ({ channels }, {}, 2), first (firstParam) {}

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan*) noexcept override
    {
        const auto& in = inputs[0];
        const auto start = (int) param (first) - 1;

        for (int ch = 0; ch < in.numChannels; ++ch)
        {
            const auto deviceChannel = start + ch;

            if (deviceChannel < 0 || deviceChannel >= ctx.deviceOutputs.numChannels)
                continue;

            if (auto* dst = ctx.deviceOutputs.channel (deviceChannel))
            {
                const auto* src = in.channel (ch);
                for (int i = 0; i < in.numSamples; ++i)
                    dst[i] += src[i];
            }
        }
    }

private:
    int first;
};

//==============================================================================
/** Gain / fader / bus: level, optional polarity invert, mute. */
class GainProcessor final : public NodeProcessor
{
public:
    struct Params
    {
        int level = -1, invert = -1, mute = -1;
        float minusInfinityDb = -100.0f;
    };

    GainProcessor (int channels, int numParams, Params p)
        : NodeProcessor ({ channels }, { channels }, numParams), ids (p),
          scratch ((size_t) engine::CompiledGraph::defaultMaxBlockSize, 0.0f)
    {
    }

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        auto target = engine::decibelsToGain (param (ids.level), ids.minusInfinityDb);

        if (ids.invert >= 0 && param (ids.invert) >= 0.5f)
            target = -target; // the ramp passes through zero, so flipping polarity doesn't click

        if (ids.mute >= 0 && param (ids.mute) >= 0.5f)
            target = 0.0f;

        if (! initialised || sampleRate != ctx.sampleRate)
        {
            gain.reset (target, rampFor (ctx.sampleRate));
            sampleRate = ctx.sampleRate;
            initialised = true;
        }

        gain.setTarget (target);
        copyWithGain (inputs[0], outputs[0], gain, scratch.data());
    }

    void reset() noexcept override { initialised = false; }

private:
    Params ids;
    Smoother gain;
    bool initialised = false;
    double sampleRate = 0.0;
    std::vector<float> scratch;
};

//==============================================================================
class PanProcessor final : public NodeProcessor
{
public:
    PanProcessor (int inputChannels, int numParams, int panParam)
        : NodeProcessor ({ inputChannels }, { 2 }, numParams), panId (panParam) {}

    void process (const ProcessContext& ctx, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto p = std::clamp (param (panId), -1.0f, 1.0f);
        const auto& in = inputs[0];
        const auto& out = outputs[0];

        float targetL, targetR;

        if (in.numChannels == 1)
        {
            // Constant power: -3 dB each side at centre.
            const auto angle = (p + 1.0f) * (float) std::numbers::pi / 4.0f;
            targetL = std::cos (angle);
            targetR = std::sin (angle);
        }
        else
        {
            // Balance: centre leaves both sides at unity.
            targetL = p > 0.0f ? std::cos (p * (float) std::numbers::pi / 2.0f) : 1.0f;
            targetR = p < 0.0f ? std::cos (-p * (float) std::numbers::pi / 2.0f) : 1.0f;
        }

        if (! initialised || sampleRate != ctx.sampleRate)
        {
            left.reset (targetL, rampFor (ctx.sampleRate));
            right.reset (targetR, rampFor (ctx.sampleRate));
            sampleRate = ctx.sampleRate;
            initialised = true;
        }

        left.setTarget (targetL);
        right.setTarget (targetR);

        const auto* srcL = in.channel (0);
        const auto* srcR = in.numChannels > 1 ? in.channel (1) : in.channel (0);

        for (int i = 0; i < out.numSamples; ++i)
        {
            out.channel (0)[i] = srcL[i] * left.next();
            out.channel (1)[i] = srcR[i] * right.next();
        }
    }

    void reset() noexcept override { initialised = false; }

private:
    int panId;
    Smoother left, right;
    bool initialised = false;
    double sampleRate = 0.0;
};

//==============================================================================
class TestGeneratorProcessor final : public NodeProcessor
{
public:
    enum Waveform { sine, pinkNoise, whiteNoise };

    TestGeneratorProcessor (int channels, int numParams, int waveformParam, int frequencyParam, int levelParam, int onParam)
        : NodeProcessor ({}, { channels }, numParams),
          waveformId (waveformParam), frequencyId (frequencyParam), levelId (levelParam), onId (onParam)
    {
    }

    void process (const ProcessContext& ctx, const ChannelSpan*, const ChannelSpan* outputs) noexcept override
    {
        const auto& out = outputs[0];
        const auto target = param (onId) >= 0.5f ? engine::decibelsToGain (param (levelId)) : 0.0f;

        if (! initialised || sampleRate != ctx.sampleRate)
        {
            level.reset (target, rampFor (ctx.sampleRate));
            sampleRate = ctx.sampleRate;
            initialised = true;
        }

        level.setTarget (target);

        const auto waveform = (int) param (waveformId);
        const auto increment = (double) param (frequencyId) / ctx.sampleRate;
        auto* first = out.channel (0);

        for (int i = 0; i < out.numSamples; ++i)
        {
            float v;

            switch (waveform)
            {
                case pinkNoise: v = pink(); break;
                case whiteNoise: v = white(); break;
                default:
                    v = (float) std::sin (phase * 2.0 * std::numbers::pi);
                    phase += increment;
                    phase -= std::floor (phase);
                    break;
            }

            first[i] = v * level.next();
        }

        for (int ch = 1; ch < out.numChannels; ++ch)
            std::copy_n (first, out.numSamples, out.channel (ch));
    }

    void reset() noexcept override
    {
        initialised = false;
        phase = 0.0;
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
    }

private:
    float white() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) (rng >> 8) / (float) (1u << 24) * 2.0f - 1.0f;
    }

    /** Paul Kellet's refined pink noise filter. */
    float pink() noexcept
    {
        const auto w = white();
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        const auto out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return out * 0.11f;
    }

    int waveformId, frequencyId, levelId, onId;
    Smoother level;
    bool initialised = false;
    double sampleRate = 0.0, phase = 0.0;
    std::uint32_t rng = 0x9e3779b9u;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};

//==============================================================================
class ChannelPickProcessor final : public NodeProcessor
{
public:
    ChannelPickProcessor (int inputChannels, int outputChannels, int numParams, int firstParam)
        : NodeProcessor ({ inputChannels }, { outputChannels }, numParams), firstId (firstParam) {}

    void process (const ProcessContext&, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        const auto& out = outputs[0];
        const auto start = (int) param (firstId) - 1;

        for (int ch = 0; ch < out.numChannels; ++ch)
        {
            const auto source = start + ch;
            if (source >= 0 && source < in.numChannels)
                std::copy_n (in.channel (source), out.numSamples, out.channel (ch));
            else
                std::fill_n (out.channel (ch), out.numSamples, 0.0f);
        }
    }

private:
    int firstId;
};

class BundleProcessor final : public NodeProcessor
{
public:
    BundleProcessor (int count, int numParams)
        : NodeProcessor (std::vector<int> ((size_t) count, 1), { count }, numParams) {}

    void process (const ProcessContext&, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& out = outputs[0];
        for (int ch = 0; ch < out.numChannels; ++ch)
            std::copy_n (inputs[ch].channel (0), out.numSamples, out.channel (ch));
    }
};

class UnbundleProcessor final : public NodeProcessor
{
public:
    UnbundleProcessor (int count, int numParams)
        : NodeProcessor ({ count }, std::vector<int> ((size_t) count, 1), numParams) {}

    void process (const ProcessContext&, const ChannelSpan* inputs, const ChannelSpan* outputs) noexcept override
    {
        const auto& in = inputs[0];
        for (int ch = 0; ch < in.numChannels; ++ch)
            std::copy_n (in.channel (ch), in.numSamples, outputs[ch].channel (0));
    }
};

/** A sink whose only job is to be metered (input meters are measured by the runtime). */
class MeterProcessor final : public NodeProcessor
{
public:
    MeterProcessor (int channels, int numParams) : NodeProcessor ({ channels }, {}, numParams) {}
    void process (const ProcessContext&, const ChannelSpan*, const ChannelSpan*) noexcept override {}
};

//==============================================================================
ParamSpec integerParam (std::string id, std::string name, int minValue, int maxValue, int def, bool structural, std::string tooltip = {})
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.kind = ParamKind::integer;
    p.minValue = (float) minValue;
    p.maxValue = (float) maxValue;
    p.defaultValue = (float) def;
    p.structural = structural;
    p.tooltip = std::move (tooltip);
    return p;
}

ParamSpec decibelParam (std::string id, std::string name, float minDb, float maxDb, float def, bool minusInfinity)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.minValue = minDb;
    p.maxValue = maxDb;
    p.defaultValue = def;
    p.unit = "dB";
    p.minusInfinityAtMinimum = minusInfinity;
    return p;
}

ParamSpec toggleParam (std::string id, std::string name, bool def, std::string tooltip = {})
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.kind = ParamKind::toggle;
    p.minValue = 0.0f;
    p.maxValue = 1.0f;
    p.defaultValue = def ? 1.0f : 0.0f;
    p.tooltip = std::move (tooltip);
    return p;
}

int channelsOf (const NodeType& type, const ParamValues& v, std::string_view id = "channels")
{
    const auto index = type.paramIndex (id);
    return index >= 0 && index < (int) v.size() ? std::clamp ((int) v[(size_t) index], 1, maxPortChannels) : 1;
}

NodeRegistry makeRegistry()
{
    NodeRegistry registry;

    //--------------------------------------------------------------------------
    {
        NodeType t;
        t.id = types::hardwareInput;
        t.name = "Hardware Input";
        t.category = "Sources";
        t.icon = "mic";
        t.description = "Channels coming in from the audio interface.";
        t.params = { integerParam ("first", "First channel", 1, 256, 1, false, "The first interface input this node reads"),
                     integerParam ("channels", "Channels", 1, maxPortChannels, 1, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::hardwareInput);
            return PortLayout { {}, { { "Out", channelsOf (self, v) } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<HardwareInputProcessor> (l.outputs[0].channels, 0);
        };
        registry.add (std::move (t));
    }

    {
        NodeType t;
        t.id = types::hardwareOutput;
        t.name = "Hardware Output";
        t.category = "Destinations";
        t.icon = "speaker";
        t.description = "Channels going out to the audio interface.";
        t.params = { integerParam ("first", "First channel", 1, 256, 1, false, "The first interface output this node writes"),
                     integerParam ("channels", "Channels", 1, maxPortChannels, 2, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::hardwareOutput);
            return PortLayout { { { "In", channelsOf (self, v) } }, {} };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<HardwareOutputProcessor> (l.inputs[0].channels, 0);
        };
        registry.add (std::move (t));
    }

    {
        NodeType t;
        t.id = types::testGenerator;
        t.name = "Test Generator";
        t.category = "Sources";
        t.icon = "audio-waveform";
        t.description = "A sine tone or noise, for checking levels and wiring.";

        ParamSpec waveform;
        waveform.id = "waveform";
        waveform.name = "Signal";
        waveform.kind = ParamKind::choice;
        waveform.minValue = 0;
        waveform.maxValue = 2;
        waveform.choices = { "Sine", "Pink noise", "White noise" };

        ParamSpec frequency;
        frequency.id = "frequency";
        frequency.name = "Frequency";
        frequency.minValue = 20.0f;
        frequency.maxValue = 20000.0f;
        frequency.defaultValue = 1000.0f;
        frequency.unit = "Hz";
        frequency.logarithmic = true;

        t.params = { waveform, frequency, decibelParam ("level", "Level", -60.0f, 0.0f, -18.0f, false),
                     toggleParam ("on", "On", false),
                     integerParam ("channels", "Channels", 1, 2, 1, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::testGenerator);
            return PortLayout { {}, { { "Out", channelsOf (self, v) } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<TestGeneratorProcessor> (l.outputs[0].channels, 5, 0, 1, 2, 3);
        };
        registry.add (std::move (t));
    }

    //--------------------------------------------------------------------------
    {
        NodeType t;
        t.id = types::gain;
        t.name = "Gain";
        t.category = "Processing";
        t.icon = "sliders-horizontal";
        t.description = "Trim the level, flip polarity or mute.";
        t.params = { decibelParam ("gain", "Gain", -60.0f, 24.0f, 0.0f, false),
                     toggleParam ("invert", "Polarity invert", false, "Flips the signal upside down"),
                     toggleParam ("mute", "Mute", false),
                     integerParam ("channels", "Channels", 1, maxPortChannels, 1, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::gain);
            const auto n = channelsOf (self, v);
            return PortLayout { { { "In", n } }, { { "Out", n } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<GainProcessor> (l.inputs[0].channels, 4, GainProcessor::Params { 0, 1, 2, -1000.0f });
        };
        registry.add (std::move (t));
    }

    auto levelNode = [&registry] (std::string_view id, std::string name, std::string icon, std::string description, int defaultChannels)
    {
        NodeType t;
        t.id = id;
        t.name = std::move (name);
        t.category = "Mixing";
        t.icon = std::move (icon);
        t.description = std::move (description);
        t.params = { decibelParam ("level", "Level", -100.0f, 10.0f, 0.0f, true),
                     toggleParam ("mute", "Mute", false),
                     integerParam ("channels", "Channels", 1, maxPortChannels, defaultChannels, true) };
        const std::string typeId (id);
        t.layout = [typeId] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (typeId);
            const auto n = channelsOf (self, v);
            return PortLayout { { { "In", n } }, { { "Out", n } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<GainProcessor> (l.inputs[0].channels, 3, GainProcessor::Params { 0, -1, 1, -100.0f });
        };
        registry.add (std::move (t));
    };

    levelNode (types::fader, "Fader", "sliders-vertical", "A level control with mute.", 1);
    levelNode (types::bus, "Bus", "git-merge", "Mixes everything wired into it, with a master level.", 2);

    {
        NodeType t;
        t.id = types::pan;
        t.name = "Pan";
        t.category = "Mixing";
        t.icon = "move-horizontal";
        t.description = "Places a mono signal in the stereo image, or balances a stereo one.";
        ParamSpec pan;
        pan.id = "pan";
        pan.name = "Pan";
        pan.minValue = -1.0f;
        pan.maxValue = 1.0f;
        pan.defaultValue = 0.0f;
        t.params = { pan, integerParam ("inputs", "Input channels", 1, 2, 1, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::pan);
            return PortLayout { { { "In", channelsOf (self, v, "inputs") } }, { { "Out", 2 } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<PanProcessor> (l.inputs[0].channels, 2, 0);
        };
        registry.add (std::move (t));
    }

    //--------------------------------------------------------------------------
    {
        NodeType t;
        t.id = types::channelPick;
        t.name = "Channel Pick";
        t.category = "Routing";
        t.icon = "list-filter";
        t.description = "Takes some channels out of a multi-channel signal, e.g. channels 3–4.";
        t.params = { integerParam ("inputs", "Input channels", 1, maxPortChannels, 8, true),
                     integerParam ("first", "First channel", 1, maxPortChannels, 1, false),
                     integerParam ("count", "Channels to take", 1, maxPortChannels, 1, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::channelPick);
            return PortLayout { { { "In", channelsOf (self, v, "inputs") } }, { { "Out", channelsOf (self, v, "count") } } };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<ChannelPickProcessor> (l.inputs[0].channels, l.outputs[0].channels, 3, 1);
        };
        registry.add (std::move (t));
    }

    {
        NodeType t;
        t.id = types::bundle;
        t.name = "Bundle";
        t.category = "Routing";
        t.icon = "combine";
        t.description = "Combines mono signals into one multi-channel signal.";
        t.params = { integerParam ("count", "Channels", 2, maxPortChannels, 8, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::bundle);
            const auto n = channelsOf (self, v, "count");
            PortLayout layout;
            for (int i = 0; i < n; ++i)
                layout.inputs.push_back ({ std::to_string (i + 1), 1 });
            layout.outputs.push_back ({ "Out", n });
            return layout;
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<BundleProcessor> (l.outputs[0].channels, 1);
        };
        registry.add (std::move (t));
    }

    {
        NodeType t;
        t.id = types::unbundle;
        t.name = "Unbundle";
        t.category = "Routing";
        t.icon = "split";
        t.description = "Splits a multi-channel signal into separate mono signals.";
        t.params = { integerParam ("count", "Channels", 2, maxPortChannels, 8, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::unbundle);
            const auto n = channelsOf (self, v, "count");
            PortLayout layout;
            layout.inputs.push_back ({ "In", n });
            for (int i = 0; i < n; ++i)
                layout.outputs.push_back ({ std::to_string (i + 1), 1 });
            return layout;
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<UnbundleProcessor> (l.inputs[0].channels, 1);
        };
        registry.add (std::move (t));
    }

    //--------------------------------------------------------------------------
    {
        NodeType t;
        t.id = types::meter;
        t.name = "Meter";
        t.category = "Analysis";
        t.icon = "gauge";
        t.description = "Shows the level of whatever is wired into it.";
        t.params = { integerParam ("channels", "Channels", 1, maxPortChannels, 2, true) };
        t.layout = [] (const ParamValues& v)
        {
            const auto& self = *NodeRegistry::builtIn().find (types::meter);
            return PortLayout { { { "In", channelsOf (self, v) } }, {} };
        };
        t.create = [] (const ParamValues&, const PortLayout& l) -> std::unique_ptr<NodeProcessor>
        {
            return std::make_unique<MeterProcessor> (l.inputs[0].channels, 1);
        };
        registry.add (std::move (t));
    }

    return registry;
}

} // namespace

const NodeRegistry& NodeRegistry::builtIn()
{
    static const NodeRegistry registry = makeRegistry();
    return registry;
}

} // namespace spm::nodes
