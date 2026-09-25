// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "app/AudioEngine.h"
#include "model/Session.h"

namespace spm::app
{

/** Keeps the running audio graph in step with the session. Parameter and wire-gain
    changes go straight to the engine; anything structural triggers a rebuild.
*/
class SessionController final : private juce::ValueTree::Listener, private juce::AsyncUpdater
{
public:
    SessionController (model::Session& session, AudioEngine& engine);
    ~SessionController() override;

    /** Rebuilds now (e.g. after loading). */
    void rebuild();

    /** Called for any change to the document (for the "modified" flag). */
    std::function<void()> onDocumentChanged;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { changed (affectsAudio (parent)); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { changed (affectsAudio (parent)); }
    void valueTreeRedirected (juce::ValueTree&) override { changed (true); }
    void handleAsyncUpdate() override;

    void changed (bool needsRebuild);

    /** Nodes and wires make the sound; panels, the view and layouts don't. */
    static bool affectsAudio (const juce::ValueTree& parent)
    {
        return parent.hasType (model::ids::nodes) || parent.hasType (model::ids::wires) || parent.hasType (model::ids::session);
    }

    model::Session& session;
    AudioEngine& engine;
};

} // namespace spm::app
