// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "graph/GraphBuilder.h"
#include "model/Session.h"
#include "model/Templates.h"

#include <catch2/catch_test_macros.hpp>

using namespace spm;
namespace types = nodes::types;

TEST_CASE ("Nodes and wires can be added, undone and redone", "[session]")
{
    model::Session s;

    s.beginAction ("Add");
    const auto in = s.addNode (types::hardwareInput, { 0, 0 });
    const auto fader = s.addNode (types::fader, { 200, 0 });
    s.beginAction ("Wire");
    const auto wire = s.addWire (in, 0, fader, 0);

    REQUIRE (in != 0);
    REQUIRE (wire != 0);
    REQUIRE (s.toGraphDesc().nodes.size() == 2);
    REQUIRE (s.toGraphDesc().wires.size() == 1);

    REQUIRE (s.getUndoManager().undo());
    REQUIRE (s.toGraphDesc().wires.empty());
    REQUIRE (s.getUndoManager().undo());
    REQUIRE (s.toGraphDesc().nodes.empty());

    REQUIRE (s.getUndoManager().redo());
    REQUIRE (s.getUndoManager().redo());
    REQUIRE (s.toGraphDesc().wires.size() == 1);

    // Ids are never reused, even after undo.
    s.beginAction ("Add");
    REQUIRE (s.addNode (types::meter, {}) > wire);
}

TEST_CASE ("Names are unique and parameters are sanitised", "[session]")
{
    model::Session s;
    const auto a = s.addNode (types::fader, {});
    const auto b = s.addNode (types::fader, {});
    REQUIRE (s.getNodeName (a) == "Fader");
    REQUIRE (s.getNodeName (b) == "Fader 2");

    s.setParam (a, 0, 50.0f);
    REQUIRE (s.getParam (a, 0) == 10.0f);  // fader maximum
}

TEST_CASE ("Invalid wires are refused with a reason", "[session]")
{
    model::Session s;
    const auto a = s.addNode (types::gain, {});
    const auto b = s.addNode (types::gain, {});
    REQUIRE (s.addWire (a, 0, b, 0) != 0);
    REQUIRE (s.checkWire (b, 0, a, 0) == "This would create a feedback loop");
    REQUIRE (s.addWire (b, 0, a, 0) == 0);
}

TEST_CASE ("Removing a node removes its wires; shrinking a node removes dangling wires", "[session]")
{
    model::Session s;
    const auto unbundle = s.addNode (types::unbundle, {}, { 4 });
    const auto meter = s.addNode (types::meter, {});
    const auto fader = s.addNode (types::fader, {});
    s.addWire (unbundle, 3, meter, 0);
    s.addWire (unbundle, 0, fader, 0);

    s.setParam (unbundle, 0, 2);  // now only 2 outputs: the wire from output 4 goes
    REQUIRE (s.toGraphDesc().wires.size() == 1);

    s.removeNodes ({ fader });
    REQUIRE (s.toGraphDesc().wires.empty());
    REQUIRE (s.toGraphDesc().nodes.size() == 2);
}

TEST_CASE ("Duplicating copies internal wires only", "[session]")
{
    model::Session s;
    const auto in = s.addNode (types::hardwareInput, {});
    const auto a = s.addNode (types::gain, {});
    const auto b = s.addNode (types::fader, {});
    s.addWire (in, 0, a, 0);
    s.addWire (a, 0, b, 0);

    const auto copies = s.duplicateNodes ({ a, b }, { 20, 20 });
    REQUIRE (copies.size() == 2);
    REQUIRE (s.toGraphDesc().wires.size() == 3);
    REQUIRE (s.getNodePosition (copies[0]) == s.getNodePosition (a) + juce::Point<float> (20, 20));
}

TEST_CASE ("Sessions round-trip through JSON", "[session]")
{
    model::Session s;
    const auto in = s.addNode (types::hardwareInput, { 10, 20 }, { 3, 2 });
    const auto out = s.addNode (types::hardwareOutput, { 300, 20 });
    s.addWire (in, 0, out, 0, 0.5f);
    s.setNodeName (in, "Vocals");

    model::Session loaded;
    REQUIRE (loaded.loadJson (s.toJson()).isEmpty());

    const auto a = s.toGraphDesc(), b = loaded.toGraphDesc();
    REQUIRE (b.nodes.size() == 2);
    REQUIRE (b.nodes[0].params == a.nodes[0].params);
    REQUIRE (b.wires[0].gain == 0.5f);
    REQUIRE (loaded.getNodeName (in) == "Vocals");
    REQUIRE (loaded.getNodePosition (in) == juce::Point<float> (10, 20));

    // New ids continue after the loaded ones.
    REQUIRE (loaded.addNode (types::meter, {}) > b.wires[0].id);

    REQUIRE (loaded.loadJson ("not json").isNotEmpty());
    REQUIRE (loaded.loadJson ("{\"format\": \"something else\"}") == "This file isn't a Stage Plot Mixer session");
}

TEST_CASE ("Missing parameters load as defaults", "[session]")
{
    model::Session s;
    REQUIRE (s.loadJson (R"({"format": "Stage Plot Mixer session", "version": 1, "nextId": 5,
                             "nodes": [{"id": 1, "type": "mix.fader", "name": "Old", "x": 0, "y": 0}],
                             "wires": [{"id": 2, "source": 1, "sourcePort": 0, "dest": 99, "destPort": 0, "gain": 1}]})").isEmpty());

    REQUIRE (s.getParams (1) == nodes::NodeRegistry::builtIn().find (types::fader)->defaults());
    REQUIRE (s.toGraphDesc().wires.empty());  // it pointed at a node that doesn't exist
}

TEST_CASE ("Default session is a working mix", "[session]")
{
    model::Session s;
    model::createDefaultSession (s, 2);

    const auto desc = s.toGraphDesc();
    REQUIRE (desc.nodes.size() == 3 + 2 * 3);
    REQUIRE (desc.wires.size() == 2 + 2 * 3);
    REQUIRE (! s.getUndoManager().canUndo());

    // Every wire was accepted, and the result builds.
    graph::GraphBuilder builder;
    REQUIRE (builder.build (desc) != nullptr);
}
