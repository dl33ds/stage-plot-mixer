// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/NodeProcessor.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace spm::nodes
{

enum class ParamKind
{
    continuous,
    toggle,
    choice,
    integer
};

struct ParamSpec
{
    std::string id;
    std::string name;
    ParamKind kind = ParamKind::continuous;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    std::string unit;                  // "dB", "Hz", ...
    bool structural = false;           // changes ports: the node gets a new processor
    bool logarithmic = false;          // e.g. frequency
    bool minusInfinityAtMinimum = false;
    bool linearDb = false;             // dB spread evenly on sliders (thresholds), not the fader law
    std::vector<std::string> choices;  // for ParamKind::choice
    std::string tooltip;
    bool onFace = true;                // shown on the node's face (if not structural)
};

struct PortInfo
{
    std::string name;
    int channels = 1;
};

struct PortLayout
{
    std::vector<PortInfo> inputs, outputs;
};

using ParamValues = std::vector<float>;

/** Description of a built-in node type: what it's called, its parameters, how its ports
    depend on those parameters, and how to create its processor.
*/
struct NodeType
{
    std::string id;           // stable, stored in session files
    std::string name;         // shown to the user
    std::string category;     // Sources, Destinations, Mixing, Routing, Analysis, Groups
    std::string icon;         // Lucide icon name
    std::string description;
    std::vector<ParamSpec> params;

    std::function<PortLayout (const ParamValues&)> layout;
    std::function<std::unique_ptr<engine::NodeProcessor> (const ParamValues&, const PortLayout&)> create;

    int paramIndex (std::string_view paramId) const noexcept;
    ParamValues defaults() const;

    /** Returns values clamped to each parameter's range, padded with defaults. */
    ParamValues sanitise (ParamValues values) const;

    /** True if two value sets have the same structural parameters. */
    bool sameStructure (const ParamValues& a, const ParamValues& b) const noexcept;
};

class NodeRegistry
{
public:
    static const NodeRegistry& builtIn();

    const NodeType* find (std::string_view typeId) const noexcept;
    const std::vector<NodeType>& all() const noexcept { return types; }

    void add (NodeType type) { types.push_back (std::move (type)); }

private:
    std::vector<NodeType> types;
};

/** Built-in type ids. */
namespace types
{
    inline constexpr std::string_view hardwareInput  = "hw.input";
    inline constexpr std::string_view hardwareOutput = "hw.output";
    inline constexpr std::string_view testGenerator  = "gen.test";
    inline constexpr std::string_view gain           = "proc.gain";
    inline constexpr std::string_view filter         = "proc.filter";
    inline constexpr std::string_view eq             = "proc.eq";
    inline constexpr std::string_view compressor     = "proc.compressor";
    inline constexpr std::string_view limiter        = "proc.limiter";
    inline constexpr std::string_view gate           = "proc.gate";
    inline constexpr std::string_view delay          = "proc.delay";
    inline constexpr std::string_view reverb         = "proc.reverb";
    inline constexpr std::string_view fader          = "mix.fader";
    inline constexpr std::string_view pan            = "mix.pan";
    inline constexpr std::string_view bus            = "mix.bus";
    inline constexpr std::string_view channelPick    = "route.pick";
    inline constexpr std::string_view bundle         = "route.bundle";
    inline constexpr std::string_view unbundle       = "route.unbundle";
    inline constexpr std::string_view meter          = "an.meter";
    inline constexpr std::string_view recorder       = "rec.recorder";
    inline constexpr std::string_view group          = "grp.group";
    inline constexpr std::string_view groupInput     = "grp.in";
    inline constexpr std::string_view groupOutput    = "grp.out";
}

/** True for the pins that give a group its inputs and outputs. */
inline bool isGroupPin (std::string_view typeId) noexcept
{
    return typeId == types::groupInput || typeId == types::groupOutput;
}

inline constexpr int maxPortChannels = 64;

} // namespace spm::nodes
