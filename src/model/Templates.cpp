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

//==============================================================================
Template channelStripTemplate()
{
    namespace types = nodes::types;

    Session s;
    const auto group = s.addNode (types::group, {});
    s.setNodeName (group, "Channel Strip");

    const auto in = s.addNode (types::groupInput, { 0, 0 }, { 1.0f }, group);
    s.setNodeName (in, "In");
    const auto trim = s.addNode (types::gain, { 240, 0 }, {}, group);
    s.setNodeName (trim, "Trim");
    const auto fader = s.addNode (types::fader, { 480, 0 }, {}, group);
    const auto pan = s.addNode (types::pan, { 720, 0 }, {}, group);
    const auto out = s.addNode (types::groupOutput, { 960, 0 }, { 2.0f }, group);
    s.setNodeName (out, "Out");

    s.addWire (in, 0, trim, 0);
    s.addWire (trim, 0, fader, 0);
    s.addWire (fader, 0, pan, 0);
    s.addWire (pan, 0, out, 0);

    return { "Channel Strip", s.exportNodes (group), {} };
}

juce::Array<Template> getTemplates (const juce::File& folder)
{
    juce::Array<Template> result { channelStripTemplate() };

    juce::Array<Template> user;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.spmtemplate"))
    {
        const auto v = juce::JSON::parse (file);
        if (v["format"].toString() == "Stage Plot Mixer nodes" && v["nodes"].isArray())
            user.add ({ file.getFileNameWithoutExtension(), v, file });
    }

    std::sort (user.begin(), user.end(), [] (const Template& a, const Template& b) { return a.name.compareNatural (b.name) < 0; });
    result.addArray (user);
    return result;
}

juce::String saveTemplate (const Session& session, graph::NodeId root, const juce::String& name, const juce::File& folder)
{
    const auto safeName = juce::File::createLegalFileName (name.trim());
    if (safeName.isEmpty())
        return "The template needs a name";

    if (! folder.createDirectory())
        return "Couldn't create " + folder.getFullPathName();

    const auto file = folder.getChildFile (safeName + ".spmtemplate");
    if (! file.replaceWithText (juce::JSON::toString (session.exportNodes (root))))
        return "Couldn't write " + file.getFullPathName();

    return {};
}

graph::NodeId addTemplate (Session& session, const Template& t, graph::NodeId parent, juce::Point<float> position)
{
    return session.importNodes (t.nodes, parent, position);
}

juce::File defaultTemplatesFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("StagePlotMixer").getChildFile ("Templates");
}

} // namespace spm::model
