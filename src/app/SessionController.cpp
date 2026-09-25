// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/SessionController.h"

namespace spm::app
{

SessionController::SessionController (model::Session& s, AudioEngine& e) : session (s), engine (e)
{
    session.getState().addListener (this);
    rebuild();
}

SessionController::~SessionController()
{
    session.getState().removeListener (this);
}

void SessionController::rebuild()
{
    cancelPendingUpdate();
    engine.setGraph (session.toGraphDesc());
}

void SessionController::handleAsyncUpdate()
{
    rebuild();
}

void SessionController::changed (bool needsRebuild)
{
    if (needsRebuild)
        triggerAsyncUpdate();

    if (onDocumentChanged)
        onDocumentChanged();
}

void SessionController::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    const auto id = (graph::NodeId) (juce::int64) tree[model::ids::id];

    if (tree.hasType (model::ids::node) && property.toString().startsWith ("p."))
    {
        const auto* type = session.getNodeType (id);
        const auto index = type != nullptr ? type->paramIndex (property.toString().substring (2).toStdString()) : -1;

        // Non-structural: straight to the processor, no rebuild.
        if (index >= 0 && ! type->params[(size_t) index].structural
            && engine.getBuilder().setParameter (id, index, session.getParam (id, index)))
        {
            changed (false);
            return;
        }

        changed (true);
        return;
    }

    if (tree.hasType (model::ids::wire))
    {
        if (property == model::ids::gain && engine.getBuilder().setWireGain (id, (float) tree[property]))
            changed (false);
        else
            changed (true);
        return;
    }

    // Names and positions don't affect the audio.
    changed (false);
}

} // namespace spm::app
