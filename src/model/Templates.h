// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "model/Session.h"

namespace spm::model
{

/** Fills an empty session with a starting mix: each input through a fader and pan into a
    stereo master bus, then a master fader to outputs 1–2.
*/
void createDefaultSession (Session& session, int numInputs);

/** A template: a group (or single node) saved with everything inside it. */
struct Template
{
    juce::String name;
    juce::var nodes;     // from Session::exportNodes
    juce::File file;     // empty for built-in templates
};

/** The Channel Strip: mono in → Trim → Fader → Pan → stereo out, in a group. */
Template channelStripTemplate();

/** Built-in templates, then the user's (from the templates folder), by name. */
juce::Array<Template> getTemplates (const juce::File& folder);

/** Saves a node (and its contents) as a template file; returns an error or empty. */
juce::String saveTemplate (const Session& session, graph::NodeId root, const juce::String& name, const juce::File& folder);

/** Adds a template to the session in a group; returns the new node, or 0. */
graph::NodeId addTemplate (Session& session, const Template& t, graph::NodeId parent, juce::Point<float> position);

/** Documents/StagePlotMixer/Templates. */
juce::File defaultTemplatesFolder();

} // namespace spm::model
