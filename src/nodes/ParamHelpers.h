// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "nodes/NodeTypes.h"

#include <string>
#include <utility>
#include <vector>

/** Shorthands for declaring built-in node types (internal to the nodes library). */
namespace spm::nodes
{

inline ParamSpec integerParam (std::string id, std::string name, int minValue, int maxValue, int def, bool structural, std::string tooltip = {})
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

inline ParamSpec decibelParam (std::string id, std::string name, float minDb, float maxDb, float def, bool minusInfinity)
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

inline ParamSpec toggleParam (std::string id, std::string name, bool def, std::string tooltip = {})
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

inline ParamSpec choiceParam (std::string id, std::string name, std::vector<std::string> choices, int def, std::string tooltip = {})
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.kind = ParamKind::choice;
    p.minValue = 0.0f;
    p.maxValue = (float) choices.size() - 1.0f;
    p.defaultValue = (float) def;
    p.choices = std::move (choices);
    p.tooltip = std::move (tooltip);
    return p;
}

/** For settings you make once, not while mixing. */
inline ParamSpec notOnFace (ParamSpec p)
{
    p.onFace = false;
    return p;
}

/** A continuous parameter with a unit: "Hz", "ms", "%", ":1" and so on. */
inline ParamSpec valueParam (std::string id, std::string name, float minValue, float maxValue, float def,
                             std::string unit, bool logarithmic = false, std::string tooltip = {})
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.defaultValue = def;
    p.unit = std::move (unit);
    p.logarithmic = logarithmic;
    p.tooltip = std::move (tooltip);
    return p;
}

/** A level setting (threshold, range) spread evenly along its slider, not on the fader law. */
inline ParamSpec thresholdParam (std::string id, std::string name, float minDb, float maxDb, float def, std::string tooltip = {})
{
    auto p = decibelParam (std::move (id), std::move (name), minDb, maxDb, def, false);
    p.linearDb = true;
    p.tooltip = std::move (tooltip);
    return p;
}

inline ParamSpec withTooltip (ParamSpec p, std::string tooltip)
{
    p.tooltip = std::move (tooltip);
    return p;
}

/** Adds the built-in effects (EffectNodes.cpp). */
void addEffectNodes (NodeRegistry& registry);

} // namespace spm::nodes
