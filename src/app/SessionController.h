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
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { changed (true); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { changed (true); }
    void valueTreeRedirected (juce::ValueTree&) override { changed (true); }
    void handleAsyncUpdate() override;

    void changed (bool needsRebuild);

    model::Session& session;
    AudioEngine& engine;
};

} // namespace spm::app
