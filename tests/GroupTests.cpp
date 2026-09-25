// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EngineRig.h"

#include "model/Session.h"
#include "model/Templates.h"
#include "model/WindowPlacement.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace spm;
using Catch::Approx;
namespace types = nodes::types;

namespace
{

/** Renders a session's graph offline: 2 inputs, 2 outputs. */
std::vector<std::vector<float>> render (const model::Session& s)
{
    test::EngineRig rig (2, 2);
    rig.core.setOutputsMuted (false);
    rig.submit (s.toGraphDesc());
    rig.settle();  // wires fade in: more of them in a row (through pins) fade differently
    rig.clearRecording();
    rig.run (4800);
    return rig.recorded;
}

bool sameAudio (const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t ch = 0; ch < a.size(); ++ch)
    {
        if (a[ch].size() != b[ch].size())
            return false;
        for (size_t i = 0; i < a[ch].size(); ++i)
            if (std::abs (a[ch][i] - b[ch][i]) > 1.0e-6f)
            {
                UNSCOPED_INFO ("ch " << ch << " sample " << i << ": " << a[ch][i] << " vs " << b[ch][i]);
                return false;
            }
    }
    return true;
}

/** Two inputs → gains → fader → out, with a send from the first gain to a meter. */
struct Mix
{
    model::Session s;
    graph::NodeId in1, in2, g1, g2, bus, out, meter;

    Mix()
    {
        in1 = s.addNode (types::hardwareInput, { 0, 0 }, { 1, 1 });
        in2 = s.addNode (types::hardwareInput, { 0, 200 }, { 2, 1 });
        g1 = s.addNode (types::gain, { 240, 0 }, { -6.0f });
        g2 = s.addNode (types::gain, { 240, 200 }, { -3.0f });
        bus = s.addNode (types::bus, { 480, 100 });
        out = s.addNode (types::hardwareOutput, { 720, 100 }, { 1, 2 });
        meter = s.addNode (types::meter, { 480, 300 }, { 1 });
        s.addWire (in1, 0, g1, 0, 0.5f);
        s.addWire (in2, 0, g2, 0);
        s.addWire (g1, 0, bus, 0);
        s.addWire (g2, 0, bus, 0, 0.25f);
        s.addWire (bus, 0, out, 0);
        s.addWire (g1, 0, meter, 0);
    }
};

} // namespace

TEST_CASE ("Grouping adds pins so the sound is unchanged", "[groups]")
{
    Mix m;
    const auto before = render (m.s);

    const auto group = m.s.groupNodes ({ m.g1, m.g2, m.bus }, "Strips");
    REQUIRE (group != 0);
    REQUIRE (m.s.getNodeName (group) == "Strips");
    REQUIRE (m.s.getParent (m.g1) == group);
    REQUIRE (m.s.getPins (group, true).size() == 2);   // one per outside source
    REQUIRE (m.s.getPins (group, false).size() == 2);  // g1 (to the meter) and bus (to out)

    const auto layout = m.s.getLayout (group);
    REQUIRE (layout.inputs.size() == 2);
    REQUIRE (layout.outputs.size() == 2);

    // Only the group and outside nodes are at the top level.
    REQUIRE (m.s.getChildren (0).size() == 5);
    REQUIRE (sameAudio (render (m.s), before));

    // Wires can't cross a group's edge.
    REQUIRE (m.s.checkWire (m.in1, 0, m.bus, 0) == "Both ends must be in the same group");

    SECTION ("ungrouping restores the same sound")
    {
        m.s.ungroup (group);
        REQUIRE (! m.s.findNode (group).isValid());
        REQUIRE (m.s.getParent (m.g1) == 0);
        REQUIRE (m.s.getNodeIds().size() == 7);
        REQUIRE (m.s.toGraphDesc().wires.size() == 6);
        REQUIRE (sameAudio (render (m.s), before));
    }

    SECTION ("undo takes it all back")
    {
        m.s.beginAction ("Next");
        m.s.ungroup (group);
        REQUIRE (m.s.getUndoManager().undo());
        REQUIRE (m.s.findNode (group).isValid());
        REQUIRE (sameAudio (render (m.s), before));
    }

    SECTION ("groups survive a JSON round trip")
    {
        model::Session loaded;
        REQUIRE (loaded.loadJson (m.s.toJson()).isEmpty());
        REQUIRE (loaded.getParent (m.g1) == group);
        REQUIRE (loaded.getPath (group).size() == 1);
        REQUIRE (sameAudio (render (loaded), before));
    }

    SECTION ("duplicating a group copies its contents")
    {
        const auto copies = m.s.duplicateNodes ({ group, m.g1 }, { 0, 400 });
        REQUIRE (copies.size() == 1);  // g1 goes with its group
        REQUIRE (m.s.getChildren (copies[0]).size() == m.s.getChildren (group).size());
        REQUIRE (m.s.getNodeName (copies[0]) == "Strips 2");
    }
}

TEST_CASE ("Removing a pin renumbers the group's ports", "[groups]")
{
    model::Session s;
    const auto group = s.addNode (types::group, {});
    const auto a = s.addNode (types::groupInput, {}, { 1 }, group);
    const auto b = s.addNode (types::groupInput, {}, { 1 }, group);
    const auto c = s.addNode (types::groupInput, {}, { 1 }, group);
    REQUIRE (s.getPins (group, true) == juce::Array<graph::NodeId> { a, b, c });

    const auto src = s.addNode (types::testGenerator, {});
    const auto w0 = s.addWire (src, 0, group, 0);
    const auto w1 = s.addWire (src, 0, group, 1);
    const auto w2 = s.addWire (src, 0, group, 2);
    REQUIRE (w2 != 0);

    s.removeNodes ({ b });
    REQUIRE (s.findWire (w0).isValid());
    REQUIRE (! s.findWire (w1).isValid());
    REQUIRE ((int) s.findWire (w2)[model::ids::destPort] == 1);

    // Pins only exist inside groups.
    REQUIRE (s.addNode (types::groupInput, {}) == 0);

    // Removing the group removes what's inside.
    s.removeNodes ({ group });
    REQUIRE (s.getNodeIds() == juce::Array<graph::NodeId> { src });
}

TEST_CASE ("Nested groups flatten for the engine", "[groups]")
{
    Mix m;
    const auto before = render (m.s);

    const auto inner = m.s.groupNodes ({ m.g1, m.g2 }, "Inner");
    const auto outer = m.s.groupNodes ({ inner, m.bus }, "Outer");
    REQUIRE (outer != 0);
    REQUIRE (m.s.isInside (m.g1, outer));
    REQUIRE (m.s.getPath (inner) == juce::Array<graph::NodeId> { outer, inner });
    REQUIRE (sameAudio (render (m.s), before));

    m.s.ungroup (outer);
    m.s.ungroup (inner);
    REQUIRE (sameAudio (render (m.s), before));
}

TEST_CASE ("The Channel Strip template and saved templates", "[groups]")
{
    model::Session s;
    const auto in = s.addNode (types::hardwareInput, {});
    const auto strip = model::addTemplate (s, model::channelStripTemplate(), 0, { 200, 0 });
    REQUIRE (s.isGroup (strip));
    REQUIRE (s.getNodeName (strip) == "Channel Strip");

    const auto layout = s.getLayout (strip);
    REQUIRE (layout.inputs.size() == 1);
    REQUIRE (layout.outputs.size() == 1);
    REQUIRE (layout.outputs[0].channels == 2);
    REQUIRE (s.addWire (in, 0, strip, 0) != 0);

    // A second one gets its own name and its own nodes.
    const auto second = model::addTemplate (s, model::channelStripTemplate(), 0, { 200, 200 });
    REQUIRE (s.getNodeName (second) == "Channel Strip 2");
    REQUIRE (s.getChildren (second).size() == 5);

    const auto folder = juce::File::createTempFile ("templates");
    REQUIRE (model::saveTemplate (s, strip, "My Strip", folder).isEmpty());
    const auto all = model::getTemplates (folder);
    REQUIRE (all.size() == 2);
    REQUIRE (all[1].name == "My Strip");

    model::Session other;
    const auto copy = model::addTemplate (other, all[1], 0, {});
    REQUIRE (other.getChildren (copy).size() == 5);
    REQUIRE (other.toGraphDesc().wires.size() == 4);
    folder.deleteRecursively();
}

TEST_CASE ("Panels, faces and layouts round-trip", "[panels]")
{
    Mix m;
    auto& s = m.s;

    const auto panelId = s.addPanel ("FOH");
    auto panel = s.findPanel (panelId);
    s.addFace (panel, m.g1, { 10, 10 }, model::FaceSize::large);
    auto faceGroup = s.addFaceGroup (panel, "Vocals", { 200, 10 });
    auto face = s.addFace (faceGroup, m.g2, {});
    s.addFace (faceGroup, m.bus, {});

    // Face groups don't nest; faces move between containers.
    auto another = s.addFaceGroup (panel, "Band", { 400, 10 });
    s.moveFaceItem (another, faceGroup, {});
    REQUIRE (another.getParent() == panel);
    s.moveFaceItem (face, panel, { 50, 300 });
    REQUIRE (face.getParent() == panel);

    juce::ValueTree view (model::ids::view);
    view.setProperty (model::ids::tab, panelId, nullptr);
    juce::ValueTree window (model::ids::window);
    window.setProperty (model::ids::panel, panelId, nullptr);
    window.setProperty (model::ids::bounds, "100 100 800 600", nullptr);
    view.appendChild (window, nullptr);
    s.setView (view);
    s.storeLayout ("Show");
    s.setShowLocked (true);

    model::Session loaded;
    REQUIRE (loaded.loadJson (s.toJson()).isEmpty());
    REQUIRE (loaded.isShowLocked());
    REQUIRE (loaded.getPanels().getNumChildren() == 1);
    REQUIRE (loaded.getPanels().isEquivalentTo (s.getPanels()));
    REQUIRE (loaded.getLayoutNames() == juce::StringArray { "Show" });
    REQUIRE (loaded.getView().isEquivalentTo (s.getView()));

    // Recalling brings back the stored windows.
    loaded.setView (juce::ValueTree (model::ids::view));
    REQUIRE (loaded.recallLayout ("Show"));
    REQUIRE (loaded.getView().getNumChildren() == 1);
    REQUIRE (loaded.getView()[model::ids::current].toString() == "Show");

    // New ids continue past the panel items.
    REQUIRE ((juce::int64) loaded.addNode (types::meter, {}) > (juce::int64) another[model::ids::id]);

    // Removing a node removes its faces.
    s.removeNodes ({ m.bus });
    REQUIRE (s.findPanel (panelId).getChildWithName (model::ids::faceGroup).getNumChildren() == 0);
}

TEST_CASE ("Windows are put back on a display that exists", "[panels]")
{
    using R = juce::Rectangle<int>;
    const R laptop (0, 0, 1440, 900), monitor (1440, 0, 1920, 1080);

    // Same displays: back where it was.
    REQUIRE (model::placeWindow ({ 1500, 100, 800, 600 }, monitor, { laptop, monitor }) == R (1500, 100, 800, 600));

    // The monitor is gone: onto the laptop, same place relative to the corner.
    REQUIRE (model::placeWindow ({ 1500, 100, 800, 600 }, monitor, { laptop }) == R (60, 100, 800, 600));

    // Too big for the display it lands on: shrunk to fit.
    REQUIRE (model::placeWindow ({ 1440, 0, 1920, 1080 }, monitor, { laptop }) == laptop);

    // Hanging off the edge: moved fully on.
    REQUIRE (model::placeWindow ({ 1000, 500, 800, 600 }, laptop, { laptop }) == R (640, 300, 800, 600));
}
