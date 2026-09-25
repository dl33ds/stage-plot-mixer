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
    std::vector<std::string> choices;  // for ParamKind::choice
    std::string tooltip;
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
    std::string category;     // Sources, Destinations, Mixing, Routing, Analysis
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
    inline constexpr std::string_view fader          = "mix.fader";
    inline constexpr std::string_view pan            = "mix.pan";
    inline constexpr std::string_view bus            = "mix.bus";
    inline constexpr std::string_view channelPick    = "route.pick";
    inline constexpr std::string_view bundle         = "route.bundle";
    inline constexpr std::string_view unbundle       = "route.unbundle";
    inline constexpr std::string_view meter          = "an.meter";
}

inline constexpr int maxPortChannels = 64;

} // namespace spm::nodes
