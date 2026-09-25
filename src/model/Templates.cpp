// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/Templates.h"

namespace spm::model
{

void createDefaultSession (Session& session, int numInputs)
{
    namespace types = nodes::types;

    numInputs = juce::jlimit (1, 8, numInputs);
    constexpr float column = 240.0f, row = 180.0f;

    const auto bus = session.addNode (types::bus, { column * 3, 0 });
    session.setNodeName (bus, "Master Bus");
    const auto master = session.addNode (types::fader, { column * 4, 0 }, { 0.0f, 0.0f, 2.0f });
    session.setNodeName (master, "Master Fader");
    const auto out = session.addNode (types::hardwareOutput, { column * 5, 0 }, { 1.0f, 2.0f });
    session.setNodeName (out, "Main Out");

    session.addWire (bus, 0, master, 0);
    session.addWire (master, 0, out, 0);

    for (int i = 0; i < numInputs; ++i)
    {
        const auto y = row * (float) i;
        const auto in = session.addNode (types::hardwareInput, { 0, y }, { (float) i + 1, 1.0f });
        session.setNodeName (in, "Input " + juce::String (i + 1));
        const auto fader = session.addNode (types::fader, { column, y });
        session.setNodeName (fader, "Fader " + juce::String (i + 1));
        const auto pan = session.addNode (types::pan, { column * 2, y });
        session.setNodeName (pan, "Pan " + juce::String (i + 1));

        session.addWire (in, 0, fader, 0);
        session.addWire (fader, 0, pan, 0);
        session.addWire (pan, 0, bus, 0);
    }

    session.getUndoManager().clearUndoHistory();
}

} // namespace spm::model
