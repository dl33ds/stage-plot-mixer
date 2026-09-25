// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/Session.h"

#include "graph/GraphRules.h"

namespace spm::model
{

namespace
{

graph::NodeId idOf (const juce::ValueTree& tree) { return (graph::NodeId) (juce::int64) tree[ids::id]; }

juce::var idVar (graph::NodeId id) { return juce::var ((juce::int64) id); }

} // namespace

Session::Session()
{
    clear();
}

void Session::clear()
{
    state = juce::ValueTree (ids::session);
    state.setProperty (ids::version, fileVersion, nullptr);
    state.setProperty (ids::nextId, 1, nullptr);
    state.appendChild (juce::ValueTree (ids::nodes), nullptr);
    state.appendChild (juce::ValueTree (ids::wires), nullptr);
    undo.clearUndoHistory();
}

graph::NodeId Session::takeId()
{
    // Ids aren't part of undo, so undoing and redoing never reuses one.
    const auto id = (graph::NodeId) (juce::int64) state[ids::nextId];
    state.setProperty (ids::nextId, (juce::int64) id + 1, nullptr);
    return id;
}

juce::String Session::uniqueName (const juce::String& base) const
{
    juce::StringArray names;
    for (const auto& n : nodesTree())
        names.add (n[ids::name].toString());

    if (! names.contains (base))
        return base;

    for (int i = 2;; ++i)
        if (! names.contains (base + " " + juce::String (i)))
            return base + " " + juce::String (i);
}

//==============================================================================
graph::NodeId Session::addNode (std::string_view typeId, juce::Point<float> position, const nodes::ParamValues& params)
{
    const auto* type = nodes::NodeRegistry::builtIn().find (typeId);
    if (type == nullptr)
        return 0;

    auto values = type->defaults();
    for (size_t i = 0; i < params.size() && i < values.size(); ++i)
        values[i] = params[i];
    values = type->sanitise (values);

    const auto id = takeId();
    juce::ValueTree node (ids::node);
    node.setProperty (ids::id, idVar (id), nullptr);
    node.setProperty (ids::type, juce::String (type->id), nullptr);
    node.setProperty (ids::name, uniqueName (type->name), nullptr);
    node.setProperty (ids::x, position.x, nullptr);
    node.setProperty (ids::y, position.y, nullptr);

    for (size_t i = 0; i < type->params.size(); ++i)
        node.setProperty (paramKey (type->params[i]), values[i], nullptr);

    nodesTree().appendChild (node, &undo);
    return id;
}

void Session::removeNodes (const juce::Array<graph::NodeId>& nodeIds)
{
    juce::Array<graph::WireId> wires;
    for (const auto& w : wiresTree())
        if (nodeIds.contains ((graph::NodeId) (juce::int64) w[ids::source]) || nodeIds.contains ((graph::NodeId) (juce::int64) w[ids::dest]))
            wires.add (idOf (w));

    removeWires (wires);

    for (auto id : nodeIds)
        if (auto node = findNode (id); node.isValid())
            nodesTree().removeChild (node, &undo);
}

juce::Array<graph::NodeId> Session::duplicateNodes (const juce::Array<graph::NodeId>& nodeIds, juce::Point<float> offset)
{
    std::map<graph::NodeId, graph::NodeId> copies;

    for (auto id : nodeIds)
    {
        const auto original = findNode (id);
        if (! original.isValid())
            continue;

        auto copy = original.createCopy();
        const auto newId = takeId();
        copy.setProperty (ids::id, idVar (newId), nullptr);
        copy.setProperty (ids::name, uniqueName (original[ids::name].toString()), nullptr);
        copy.setProperty (ids::x, (float) original[ids::x] + offset.x, nullptr);
        copy.setProperty (ids::y, (float) original[ids::y] + offset.y, nullptr);
        nodesTree().appendChild (copy, &undo);
        copies[id] = newId;
    }

    // Wires between the copied nodes are copied too.
    std::vector<juce::ValueTree> wiresToCopy;
    for (const auto& w : wiresTree())
        if (copies.count ((graph::NodeId) (juce::int64) w[ids::source]) && copies.count ((graph::NodeId) (juce::int64) w[ids::dest]))
            wiresToCopy.push_back (w);

    for (const auto& w : wiresToCopy)
        addWire (copies[(graph::NodeId) (juce::int64) w[ids::source]], w[ids::sourcePort],
                 copies[(graph::NodeId) (juce::int64) w[ids::dest]], w[ids::destPort], (float) w[ids::gain]);

    juce::Array<graph::NodeId> result;
    for (auto& [from, to] : copies)
        result.add (to);
    return result;
}

juce::ValueTree Session::findNode (graph::NodeId id) const
{
    return nodesTree().getChildWithProperty (ids::id, idVar (id));
}

juce::Array<graph::NodeId> Session::getNodeIds() const
{
    juce::Array<graph::NodeId> result;
    for (const auto& n : nodesTree())
        result.add (idOf (n));
    return result;
}

void Session::setNodePosition (graph::NodeId id, juce::Point<float> position)
{
    if (auto node = findNode (id); node.isValid())
    {
        node.setProperty (ids::x, position.x, &undo);
        node.setProperty (ids::y, position.y, &undo);
    }
}

juce::Point<float> Session::getNodePosition (graph::NodeId id) const
{
    const auto node = findNode (id);
    return { (float) node[ids::x], (float) node[ids::y] };
}

void Session::setNodeName (graph::NodeId id, const juce::String& name)
{
    if (auto node = findNode (id); node.isValid() && name.trim().isNotEmpty())
        node.setProperty (ids::name, name.trim(), &undo);
}

juce::String Session::getNodeName (graph::NodeId id) const
{
    return findNode (id)[ids::name].toString();
}

const nodes::NodeType* Session::getNodeType (graph::NodeId id) const
{
    return nodes::NodeRegistry::builtIn().find (findNode (id)[ids::type].toString().toStdString());
}

nodes::ParamValues Session::getParams (graph::NodeId id) const
{
    const auto node = findNode (id);
    const auto* type = getNodeType (id);
    if (type == nullptr)
        return {};

    auto values = type->defaults();
    for (size_t i = 0; i < type->params.size(); ++i)
    {
        const auto& v = node[juce::Identifier (paramKey (type->params[i]))];
        if (! v.isVoid())
            values[i] = (float) v;
    }

    return type->sanitise (values);
}

float Session::getParam (graph::NodeId id, int index) const
{
    const auto values = getParams (id);
    return index >= 0 && index < (int) values.size() ? values[(size_t) index] : 0.0f;
}

void Session::setParam (graph::NodeId id, int index, float value)
{
    auto node = findNode (id);
    const auto* type = getNodeType (id);
    if (! node.isValid() || type == nullptr || index < 0 || index >= (int) type->params.size())
        return;

    auto values = getParams (id);
    values[(size_t) index] = value;
    values = type->sanitise (values);

    const auto& spec = type->params[(size_t) index];
    node.setProperty (juce::Identifier (paramKey (spec)), values[(size_t) index], &undo);

    if (spec.structural)
        removeDanglingWires();
}

nodes::PortLayout Session::getLayout (graph::NodeId id) const
{
    const auto* type = getNodeType (id);
    return type != nullptr ? type->layout (getParams (id)) : nodes::PortLayout {};
}

//==============================================================================
std::string Session::checkWire (graph::NodeId source, int sourcePort, graph::NodeId dest, int destPort) const
{
    return graph::checkConnection (toGraphDesc(), source, sourcePort, dest, destPort);
}

graph::WireId Session::addWire (graph::NodeId source, int sourcePort, graph::NodeId dest, int destPort, float gain)
{
    if (! checkWire (source, sourcePort, dest, destPort).empty())
        return 0;

    const auto id = takeId();
    juce::ValueTree wire (ids::wire);
    wire.setProperty (ids::id, idVar (id), nullptr);
    wire.setProperty (ids::source, idVar (source), nullptr);
    wire.setProperty (ids::sourcePort, sourcePort, nullptr);
    wire.setProperty (ids::dest, idVar (dest), nullptr);
    wire.setProperty (ids::destPort, destPort, nullptr);
    wire.setProperty (ids::gain, gain, nullptr);
    wiresTree().appendChild (wire, &undo);
    return id;
}

void Session::removeWires (const juce::Array<graph::WireId>& wireIds)
{
    for (auto id : wireIds)
        if (auto wire = findWire (id); wire.isValid())
            wiresTree().removeChild (wire, &undo);
}

juce::ValueTree Session::findWire (graph::WireId id) const
{
    return wiresTree().getChildWithProperty (ids::id, idVar (id));
}

void Session::setWireGain (graph::WireId id, float linearGain)
{
    if (auto wire = findWire (id); wire.isValid() && std::isfinite (linearGain))
        wire.setProperty (ids::gain, juce::jmax (0.0f, linearGain), &undo);
}

void Session::removeDanglingWires()
{
    juce::Array<graph::WireId> dangling;

    for (const auto& w : wiresTree())
    {
        const auto source = getLayout ((graph::NodeId) (juce::int64) w[ids::source]);
        const auto dest = getLayout ((graph::NodeId) (juce::int64) w[ids::dest]);

        if ((int) w[ids::sourcePort] >= (int) source.outputs.size() || (int) w[ids::destPort] >= (int) dest.inputs.size())
            dangling.add (idOf (w));
    }

    removeWires (dangling);
}

//==============================================================================
graph::GraphDesc Session::toGraphDesc() const
{
    graph::GraphDesc d;

    for (const auto& n : nodesTree())
        d.nodes.push_back ({ idOf (n), n[ids::type].toString().toStdString(), getParams (idOf (n)) });

    for (const auto& w : wiresTree())
        d.wires.push_back ({ idOf (w), (graph::NodeId) (juce::int64) w[ids::source], (int) w[ids::sourcePort],
                             (graph::NodeId) (juce::int64) w[ids::dest], (int) w[ids::destPort], (float) w[ids::gain] });

    return d;
}

juce::String Session::toJson() const
{
    auto toObject = [] (const juce::ValueTree& tree)
    {
        auto* object = new juce::DynamicObject();
        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            const auto name = tree.getPropertyName (i);
            object->setProperty (name, tree[name]);
        }
        return juce::var (object);
    };

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "Stage Plot Mixer session");
    root->setProperty (ids::version, fileVersion);
    root->setProperty (ids::nextId, state[ids::nextId]);

    juce::Array<juce::var> nodeList, wireList;
    for (const auto& n : nodesTree()) nodeList.add (toObject (n));
    for (const auto& w : wiresTree()) wireList.add (toObject (w));

    root->setProperty ("nodes", nodeList);
    root->setProperty ("wires", wireList);
    return juce::JSON::toString (juce::var (root));
}

juce::String Session::loadJson (const juce::String& json)
{
    juce::var root;
    const auto result = juce::JSON::parse (json, root);

    if (result.failed() || ! root.isObject())
        return "This file isn't a valid session (" + result.getErrorMessage() + ")";

    if (root["format"].toString() != "Stage Plot Mixer session")
        return "This file isn't a Stage Plot Mixer session";

    if ((int) root[ids::version] > fileVersion)
        return "This session was saved by a newer version of Stage Plot Mixer";

    clear();
    juce::int64 highestId = 0;

    auto fromObject = [&highestId] (const juce::Identifier& type, const juce::var& object)
    {
        juce::ValueTree tree (type);
        if (auto* o = object.getDynamicObject())
            for (const auto& p : o->getProperties())
                tree.setProperty (p.name, p.value, nullptr);

        highestId = juce::jmax (highestId, (juce::int64) tree[ids::id]);
        return tree;
    };

    if (auto* list = root["nodes"].getArray())
        for (const auto& n : *list)
            if (auto tree = fromObject (ids::node, n); (juce::int64) tree[ids::id] > 0)
                nodesTree().appendChild (tree, nullptr);

    if (auto* list = root["wires"].getArray())
        for (const auto& w : *list)
            if (auto tree = fromObject (ids::wire, w); (juce::int64) tree[ids::id] > 0)
                if (findNode ((graph::NodeId) (juce::int64) tree[ids::source]).isValid()
                    && findNode ((graph::NodeId) (juce::int64) tree[ids::dest]).isValid())
                    wiresTree().appendChild (tree, nullptr);

    state.setProperty (ids::nextId, juce::jmax ((juce::int64) root[ids::nextId], highestId + 1), nullptr);
    undo.clearUndoHistory();
    return {};
}

} // namespace spm::model
