// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/Session.h"

#include "graph/GraphRules.h"

#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace spm::model
{

namespace
{

namespace types = nodes::types;

graph::NodeId idOf (const juce::ValueTree& tree) { return (graph::NodeId) (juce::int64) tree[ids::id]; }
graph::NodeId nodeRef (const juce::ValueTree& tree, const juce::Identifier& property) { return (graph::NodeId) (juce::int64) tree[property]; }

juce::var idVar (graph::NodeId id) { return juce::var ((juce::int64) id); }

bool isPinType (const juce::ValueTree& node) { return nodes::isGroupPin (node[ids::type].toString().toStdString()); }

/** Properties as an object; children (if any) in "children", each with its type in "kind". */
juce::var treeToVar (const juce::ValueTree& tree, bool withKind)
{
    auto* object = new juce::DynamicObject();
    if (withKind)
        object->setProperty ("kind", tree.getType().toString());

    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName (i);
        object->setProperty (name, tree[name]);
    }

    if (tree.getNumChildren() > 0)
    {
        juce::Array<juce::var> children;
        for (const auto& child : tree)
            children.add (treeToVar (child, true));
        object->setProperty ("children", children);
    }

    return juce::var (object);
}

juce::ValueTree varToTree (const juce::Identifier& defaultType, const juce::var& v)
{
    const auto kind = v["kind"].toString();
    juce::ValueTree tree (kind.isNotEmpty() ? juce::Identifier (kind) : defaultType);

    if (auto* o = v.getDynamicObject())
        for (const auto& p : o->getProperties())
            if (p.name.toString() != "kind" && p.name.toString() != "children")
                tree.setProperty (p.name, p.value, nullptr);

    if (auto* children = v["children"].getArray())
        for (const auto& child : *children)
            if (child["kind"].toString().isNotEmpty())
                tree.appendChild (varToTree (defaultType, child), nullptr);

    return tree;
}

juce::var propertiesOf (const juce::ValueTree& tree)
{
    auto* object = new juce::DynamicObject();
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName (i);
        object->setProperty (name, tree[name]);
    }
    return juce::var (object);
}

juce::ValueTree fromProperties (const juce::Identifier& type, const juce::var& object)
{
    juce::ValueTree tree (type);
    if (auto* o = object.getDynamicObject())
        for (const auto& p : o->getProperties())
            tree.setProperty (p.name, p.value, nullptr);
    return tree;
}

/** Group ports: each group's input and output pins, in port order. */
struct PinMap
{
    std::map<graph::NodeId, std::vector<graph::NodeId>> inputs, outputs;
};

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
    state.appendChild (juce::ValueTree (ids::panels), nullptr);
    state.appendChild (juce::ValueTree (ids::view), nullptr);
    state.appendChild (juce::ValueTree (ids::layouts), nullptr);
    undo.clearUndoHistory();
}

graph::NodeId Session::takeId()
{
    // Ids aren't part of undo, so undoing and redoing never reuses one.
    const auto id = (graph::NodeId) (juce::int64) state[ids::nextId];
    state.setProperty (ids::nextId, (juce::int64) id + 1, nullptr);
    return id;
}

juce::String Session::uniqueName (const juce::String& base, graph::NodeId parent) const
{
    // Unique among the nodes in the same group; the path tells the groups apart.
    juce::StringArray names;
    for (const auto& n : nodesTree())
        if (nodeRef (n, ids::parent) == parent)
            names.add (n[ids::name].toString());

    if (! names.contains (base))
        return base;

    for (int i = 2;; ++i)
        if (! names.contains (base + " " + juce::String (i)))
            return base + " " + juce::String (i);
}

//==============================================================================
graph::NodeId Session::addNode (std::string_view typeId, juce::Point<float> position, const nodes::ParamValues& params,
                                graph::NodeId parent)
{
    const auto* type = nodes::NodeRegistry::builtIn().find (typeId);
    if (type == nullptr)
        return 0;

    if (parent != 0 && ! isGroup (parent))
        return 0;

    if (parent == 0 && nodes::isGroupPin (typeId))
        return 0;  // a pin belongs to a group

    auto values = type->defaults();
    for (size_t i = 0; i < params.size() && i < values.size(); ++i)
        values[i] = params[i];
    values = type->sanitise (values);

    const auto id = takeId();
    juce::ValueTree node (ids::node);
    node.setProperty (ids::id, idVar (id), nullptr);
    node.setProperty (ids::type, juce::String (type->id), nullptr);
    node.setProperty (ids::name, uniqueName (type->name, parent), nullptr);
    node.setProperty (ids::x, position.x, nullptr);
    node.setProperty (ids::y, position.y, nullptr);
    if (parent != 0)
        node.setProperty (ids::parent, idVar (parent), nullptr);

    for (size_t i = 0; i < type->params.size(); ++i)
        node.setProperty (paramKey (type->params[i]), values[i], nullptr);

    nodesTree().appendChild (node, &undo);
    return id;
}

juce::Array<graph::NodeId> Session::withDescendants (const juce::Array<graph::NodeId>& nodeIds) const
{
    juce::Array<graph::NodeId> result;
    for (auto id : nodeIds)
        if (findNode (id).isValid())
            result.addIfNotAlreadyThere (id);

    for (int i = 0; i < result.size(); ++i)
        if (isGroup (result[i]))
            for (auto child : getChildren (result[i]))
                result.addIfNotAlreadyThere (child);

    return result;
}

void Session::renumberGroupPorts (graph::NodeId group, const juce::Array<graph::NodeId>& removedPins)
{
    // The group's ports after the removed pins move up; wires on removed ports go.
    for (auto inputs : { true, false })
    {
        const auto pins = getPins (group, inputs);
        std::vector<int> newIndex;
        auto next = 0;
        for (auto pin : pins)
            newIndex.push_back (removedPins.contains (pin) ? -1 : next++);

        juce::Array<graph::WireId> gone;
        std::vector<std::pair<juce::ValueTree, int>> moves;
        const auto& end = inputs ? ids::dest : ids::source;
        const auto& port = inputs ? ids::destPort : ids::sourcePort;

        for (const auto& w : wiresTree())
        {
            if (nodeRef (w, end) != group)
                continue;

            const auto p = (int) w[port];
            const auto mapped = p >= 0 && p < (int) newIndex.size() ? newIndex[(size_t) p] : -1;
            if (mapped < 0)
                gone.add (idOf (w));
            else if (mapped != p)
                moves.push_back ({ w, mapped });
        }

        removeWires (gone);
        for (auto& [w, mapped] : moves)
            juce::ValueTree (w).setProperty (port, mapped, &undo);
    }
}

void Session::removeNodes (const juce::Array<graph::NodeId>& nodeIds)
{
    const auto all = withDescendants (nodeIds);

    juce::Array<graph::WireId> wires;
    for (const auto& w : wiresTree())
        if (all.contains (nodeRef (w, ids::source)) || all.contains (nodeRef (w, ids::dest)))
            wires.add (idOf (w));

    removeWires (wires);

    // Pins removed from a group that stays: its later ports move up.
    std::map<graph::NodeId, juce::Array<graph::NodeId>> removedPins;
    for (auto id : all)
        if (isPinType (findNode (id)) && ! all.contains (getParent (id)))
            removedPins[getParent (id)].add (id);

    for (auto& [group, pins] : removedPins)
        renumberGroupPorts (group, pins);

    // Faces showing them.
    std::function<void (juce::ValueTree)> removeFaces = [&] (juce::ValueTree container)
    {
        for (int i = container.getNumChildren(); --i >= 0;)
        {
            auto child = container.getChild (i);
            if (child.hasType (ids::face) && all.contains (nodeRef (child, ids::node)))
                container.removeChild (i, &undo);
            else if (child.getNumChildren() > 0)
                removeFaces (child);
        }
    };
    removeFaces (getPanels());

    for (auto id : all)
        if (auto node = findNode (id); node.isValid())
            nodesTree().removeChild (node, &undo);
}

juce::Array<graph::NodeId> Session::duplicateNodes (const juce::Array<graph::NodeId>& nodeIds, juce::Point<float> offset)
{
    // Groups are copied with everything inside them.
    juce::Array<graph::NodeId> top;
    for (auto id : nodeIds)
        if (findNode (id).isValid())
            top.addIfNotAlreadyThere (id);

    const auto chosen = top;  // a node inside another chosen group goes with it
    top.removeIf ([&] (graph::NodeId id) { return std::any_of (chosen.begin(), chosen.end(), [&] (auto g) { return isInside (id, g); }); });

    auto all = withDescendants (top);
    std::sort (all.begin(), all.end());  // so pins keep their order

    std::map<graph::NodeId, graph::NodeId> copies;
    for (auto id : all)
        copies[id] = takeId();

    for (auto id : all)
    {
        const auto original = findNode (id);
        auto copy = original.createCopy();
        copy.setProperty (ids::id, idVar (copies[id]), nullptr);

        if (top.contains (id))
        {
            copy.setProperty (ids::name, uniqueName (original[ids::name].toString(), getParent (id)), nullptr);
            copy.setProperty (ids::x, (float) original[ids::x] + offset.x, nullptr);
            copy.setProperty (ids::y, (float) original[ids::y] + offset.y, nullptr);
        }
        else
        {
            copy.setProperty (ids::parent, idVar (copies[getParent (id)]), nullptr);
        }

        nodesTree().appendChild (copy, &undo);
    }

    // Wires among the copied nodes are copied too.
    std::vector<juce::ValueTree> wiresToCopy;
    for (const auto& w : wiresTree())
        if (copies.count (nodeRef (w, ids::source)) && copies.count (nodeRef (w, ids::dest)))
            wiresToCopy.push_back (w);

    for (const auto& w : wiresToCopy)
        addWire (copies[nodeRef (w, ids::source)], w[ids::sourcePort], copies[nodeRef (w, ids::dest)], w[ids::destPort], (float) w[ids::gain]);

    juce::Array<graph::NodeId> result;
    for (auto id : top)
        result.add (copies[id]);
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
    if (type == nullptr)
        return {};

    if (type->id == types::group)
    {
        nodes::PortLayout layout;
        for (auto inputs : { true, false })
            for (auto pin : getPins (id, inputs))
            {
                const auto* pinType = getNodeType (pin);
                const auto pinLayout = pinType->layout (getParams (pin));
                (inputs ? layout.inputs : layout.outputs).push_back ({ getNodeName (pin).toStdString(), pinLayout.inputs[0].channels });
            }
        return layout;
    }

    auto layout = type->layout (getParams (id));

    // Inside the group, an input pin is a source and an output pin a destination.
    if (type->id == types::groupInput)
        layout.inputs.clear();
    else if (type->id == types::groupOutput)
        layout.outputs.clear();

    return layout;
}

//==============================================================================
graph::NodeId Session::getParent (graph::NodeId id) const
{
    return nodeRef (findNode (id), ids::parent);
}

juce::Array<graph::NodeId> Session::getChildren (graph::NodeId parent) const
{
    juce::Array<graph::NodeId> result;
    for (const auto& n : nodesTree())
        if (nodeRef (n, ids::parent) == parent)
            result.add (idOf (n));
    return result;
}

bool Session::isGroup (graph::NodeId id) const
{
    return findNode (id)[ids::type].toString() == juce::String (types::group.data(), types::group.size());
}

bool Session::isInside (graph::NodeId id, graph::NodeId ancestor) const
{
    // Bounded, in case a damaged file has a loop of parents.
    for (int depth = 0; depth < 64 && id != 0; ++depth)
    {
        id = getParent (id);
        if (id == ancestor)
            return true;
    }
    return false;
}

juce::Array<graph::NodeId> Session::getPins (graph::NodeId group, bool inputs) const
{
    const auto wanted = juce::String (inputs ? types::groupInput.data() : types::groupOutput.data());
    juce::Array<graph::NodeId> result;
    for (const auto& n : nodesTree())
        if (nodeRef (n, ids::parent) == group && n[ids::type].toString() == wanted)
            result.add (idOf (n));

    std::sort (result.begin(), result.end());
    return result;
}

juce::Array<graph::NodeId> Session::getPath (graph::NodeId group) const
{
    juce::Array<graph::NodeId> path;
    for (int depth = 0; depth < 64 && group != 0 && findNode (group).isValid(); ++depth)
    {
        path.insert (0, group);
        group = getParent (group);
    }
    return path;
}

graph::NodeId Session::groupNodes (const juce::Array<graph::NodeId>& nodeIds, const juce::String& name)
{
    juce::Array<graph::NodeId> members;
    for (auto id : nodeIds)
        if (auto node = findNode (id); node.isValid() && ! isPinType (node))
            members.addIfNotAlreadyThere (id);

    if (members.isEmpty())
        return 0;

    const auto parent = getParent (members.getFirst());
    for (auto id : members)
        if (getParent (id) != parent)
            return 0;

    auto area = juce::Rectangle<float>().withPosition (getNodePosition (members.getFirst()));
    for (auto id : members)
        area = area.getUnion (juce::Rectangle<float> (220.0f, 120.0f).withPosition (getNodePosition (id)));

    const auto group = addNode (types::group, { area.getCentreX() - 94.0f, area.getY() }, {}, parent);
    setNodeName (group, uniqueName (name, parent));

    for (auto id : members)
        findNode (id).setProperty (ids::parent, idVar (group), &undo);

    // Wires crossing the edge go through new pins, one per source port.
    struct Crossing
    {
        graph::NodeId source = 0;
        int sourcePort = 0;
        std::vector<juce::ValueTree> wires;
    };
    std::vector<Crossing> incoming, outgoing;

    auto add = [] (std::vector<Crossing>& list, const juce::ValueTree& w)
    {
        const auto source = nodeRef (w, ids::source);
        const auto port = (int) w[ids::sourcePort];
        for (auto& c : list)
            if (c.source == source && c.sourcePort == port)
            {
                c.wires.push_back (w);
                return;
            }
        list.push_back ({ source, port, { w } });
    };

    for (const auto& w : wiresTree())
    {
        const auto inSource = members.contains (nodeRef (w, ids::source)), inDest = members.contains (nodeRef (w, ids::dest));
        if (inSource && ! inDest)
            add (outgoing, w);
        else if (! inSource && inDest)
            add (incoming, w);
    }

    auto byHeight = [this] (const Crossing& a, const Crossing& b)
    {
        return std::make_pair (getNodePosition (a.source).y, a.sourcePort) < std::make_pair (getNodePosition (b.source).y, b.sourcePort);
    };
    std::sort (incoming.begin(), incoming.end(), byHeight);
    std::sort (outgoing.begin(), outgoing.end(), byHeight);

    auto pinName = [this] (const Crossing& c)
    {
        const auto layout = getLayout (c.source);
        auto text = getNodeName (c.source);
        if (layout.outputs.size() > 1 && c.sourcePort < (int) layout.outputs.size())
            text << " " << layout.outputs[(size_t) c.sourcePort].name;
        return text;
    };

    auto channelsOf = [this] (const Crossing& c)
    {
        const auto layout = getLayout (c.source);
        return c.sourcePort < (int) layout.outputs.size() ? (float) layout.outputs[(size_t) c.sourcePort].channels : 1.0f;
    };

    struct NewWire { graph::NodeId source; int sourcePort; graph::NodeId dest; int destPort; float gain; };
    std::vector<NewWire> newWires;
    juce::Array<graph::WireId> oldWires;

    for (size_t k = 0; k < incoming.size(); ++k)
    {
        const auto& c = incoming[k];
        const auto pin = addNode (types::groupInput, { area.getX() - 260.0f, area.getY() + 110.0f * (float) k }, { channelsOf (c) }, group);
        setNodeName (pin, uniqueName (pinName (c), group));
        newWires.push_back ({ c.source, c.sourcePort, group, (int) k, 1.0f });
        for (auto& w : c.wires)
        {
            newWires.push_back ({ pin, 0, nodeRef (w, ids::dest), (int) w[ids::destPort], (float) w[ids::gain] });
            oldWires.add (idOf (w));
        }
    }

    for (size_t k = 0; k < outgoing.size(); ++k)
    {
        const auto& c = outgoing[k];
        const auto pin = addNode (types::groupOutput, { area.getRight() + 60.0f, area.getY() + 110.0f * (float) k }, { channelsOf (c) }, group);
        setNodeName (pin, uniqueName (pinName (c), group));
        newWires.push_back ({ c.source, c.sourcePort, pin, 0, 1.0f });
        for (auto& w : c.wires)
        {
            newWires.push_back ({ group, (int) k, nodeRef (w, ids::dest), (int) w[ids::destPort], (float) w[ids::gain] });
            oldWires.add (idOf (w));
        }
    }

    removeWires (oldWires);
    for (auto& w : newWires)
        addWire (w.source, w.sourcePort, w.dest, w.destPort, w.gain);

    return group;
}

void Session::ungroup (graph::NodeId group)
{
    if (! isGroup (group))
        return;

    const auto parent = getParent (group);
    const auto inputs = getPins (group, true), outputs = getPins (group, false);

    struct Feed { graph::NodeId source; int port; float gain; };

    // What reaches each pin from outside the group, through any chain of pins.
    std::function<std::vector<Feed> (graph::NodeId, int)> feeds = [&] (graph::NodeId pin, int depth)
    {
        std::vector<Feed> result;
        if (depth > 4)
            return result;

        if (const auto k = inputs.indexOf (pin); k >= 0)
        {
            for (const auto& w : wiresTree())
                if (nodeRef (w, ids::dest) == group && (int) w[ids::destPort] == k)
                    result.push_back ({ nodeRef (w, ids::source), (int) w[ids::sourcePort], (float) w[ids::gain] });
            return result;
        }

        for (const auto& w : wiresTree())
        {
            if (nodeRef (w, ids::dest) != pin)
                continue;

            const auto source = nodeRef (w, ids::source);
            if (inputs.contains (source))
                for (auto f : feeds (source, depth + 1))
                    result.push_back ({ f.source, f.port, f.gain * (float) w[ids::gain] });
            else
                result.push_back ({ source, (int) w[ids::sourcePort], (float) w[ids::gain] });
        }
        return result;
    };

    std::map<std::tuple<graph::NodeId, int, graph::NodeId, int>, float> newWires;

    for (const auto& w : wiresTree())
    {
        const auto source = nodeRef (w, ids::source), dest = nodeRef (w, ids::dest);
        const auto gain = (float) w[ids::gain];

        // Inside: from an input pin to a node that stays.
        if (inputs.contains (source) && ! outputs.contains (dest))
            for (auto f : feeds (source, 0))
                newWires[{ f.source, f.port, dest, (int) w[ids::destPort] }] += f.gain * gain;

        // Outside: from one of the group's outputs.
        if (source == group && (int) w[ids::sourcePort] < outputs.size())
            for (auto f : feeds (outputs[(int) w[ids::sourcePort]], 0))
                newWires[{ f.source, f.port, dest, (int) w[ids::destPort] }] += f.gain * gain;
    }

    // The contents move out, keeping their layout, to where the group was.
    juce::Array<graph::NodeId> children;
    for (auto id : getChildren (group))
        if (! inputs.contains (id) && ! outputs.contains (id))
            children.add (id);

    if (! children.isEmpty())
    {
        auto topLeft = getNodePosition (children.getFirst());
        for (auto id : children)
            topLeft = { std::min (topLeft.x, getNodePosition (id).x), std::min (topLeft.y, getNodePosition (id).y) };

        const auto shift = getNodePosition (group) - topLeft;
        for (auto id : children)
        {
            auto node = findNode (id);
            if (parent != 0)
                node.setProperty (ids::parent, idVar (parent), &undo);
            else
                node.removeProperty (ids::parent, &undo);
            node.setProperty (ids::name, uniqueName (getNodeName (id), parent), &undo);
            setNodePosition (id, getNodePosition (id) + shift);
        }
    }

    removeNodes ({ group });  // with its pins and their wires

    for (auto& [ends, gain] : newWires)
        addWire (std::get<0> (ends), std::get<1> (ends), std::get<2> (ends), std::get<3> (ends), gain);
}

juce::var Session::exportNodes (graph::NodeId root) const
{
    const auto all = withDescendants ({ root });

    juce::Array<juce::var> nodeList, wireList;
    for (const auto& n : nodesTree())
        if (all.contains (idOf (n)))
            nodeList.add (propertiesOf (n));

    // The root's own wires are outside it.
    for (const auto& w : wiresTree())
    {
        const auto source = nodeRef (w, ids::source), dest = nodeRef (w, ids::dest);
        if (all.contains (source) && all.contains (dest) && source != root && dest != root)
            wireList.add (propertiesOf (w));
    }

    auto* object = new juce::DynamicObject();
    object->setProperty ("format", "Stage Plot Mixer nodes");
    object->setProperty ("root", idVar (root));
    object->setProperty ("nodes", nodeList);
    object->setProperty ("wires", wireList);
    return juce::var (object);
}

graph::NodeId Session::importNodes (const juce::var& exported, graph::NodeId parent, juce::Point<float> position)
{
    const auto oldRoot = (graph::NodeId) (juce::int64) exported["root"];
    const auto* nodeList = exported["nodes"].getArray();
    if (nodeList == nullptr || (parent != 0 && ! isGroup (parent)))
        return 0;

    std::map<graph::NodeId, graph::NodeId> copies;
    for (const auto& n : *nodeList)
        if (const auto id = (graph::NodeId) (juce::int64) n[ids::id]; id != 0)
            copies[id] = 0;

    if (copies.count (oldRoot) == 0)
        return 0;

    for (const auto& n : *nodeList)
        if ((graph::NodeId) (juce::int64) n[ids::id] == oldRoot && parent == 0
            && nodes::isGroupPin (n[ids::type].toString().toStdString()))
            return 0;  // a pin belongs to a group

    // New ids in the old order, so pins keep their order.
    for (auto& [id, copy] : copies)
        copy = takeId();

    for (const auto& n : *nodeList)
    {
        const auto id = (graph::NodeId) (juce::int64) n[ids::id];
        if (id == 0)
            continue;

        auto tree = fromProperties (ids::node, n);
        tree.setProperty (ids::id, idVar (copies[id]), nullptr);

        if (id == oldRoot)
        {
            tree.setProperty (ids::x, position.x, nullptr);
            tree.setProperty (ids::y, position.y, nullptr);
            tree.setProperty (ids::name, uniqueName (tree[ids::name].toString(), parent), nullptr);
            if (parent != 0)
                tree.setProperty (ids::parent, idVar (parent), nullptr);
            else
                tree.removeProperty (ids::parent, nullptr);
        }
        else
        {
            const auto oldParent = nodeRef (tree, ids::parent);
            tree.setProperty (ids::parent, idVar (copies.count (oldParent) ? copies[oldParent] : copies[oldRoot]), nullptr);
        }

        nodesTree().appendChild (tree, &undo);
    }

    if (auto* wireList = exported["wires"].getArray())
        for (const auto& w : *wireList)
        {
            const auto source = (graph::NodeId) (juce::int64) w[ids::source], dest = (graph::NodeId) (juce::int64) w[ids::dest];
            if (copies.count (source) && copies.count (dest))
                addWire (copies[source], (int) w[ids::sourcePort], copies[dest], (int) w[ids::destPort],
                         w[ids::gain].isVoid() ? 1.0f : (float) w[ids::gain]);
        }

    return copies[oldRoot];
}

//==============================================================================
std::string Session::translateWire (graph::WireDesc& wire) const
{
    // A wire on a group's port really goes to the pin inside.
    if (isGroup (wire.sourceNode))
    {
        const auto pins = getPins (wire.sourceNode, false);
        if (wire.sourcePort < 0 || wire.sourcePort >= pins.size())
            return "That port doesn't exist";
        wire.sourceNode = pins[wire.sourcePort];
        wire.sourcePort = 0;
    }

    if (isGroup (wire.destNode))
    {
        const auto pins = getPins (wire.destNode, true);
        if (wire.destPort < 0 || wire.destPort >= pins.size())
            return "That port doesn't exist";
        wire.destNode = pins[wire.destPort];
        wire.destPort = 0;
    }

    return {};
}

std::string Session::checkWire (graph::NodeId source, int sourcePort, graph::NodeId dest, int destPort) const
{
    if (source == dest)
        return "A node can't be wired to itself";

    if (! findNode (source).isValid() || ! findNode (dest).isValid())
        return "That node no longer exists";

    if (getParent (source) != getParent (dest))
        return "Both ends must be in the same group";

    if (sourcePort < 0 || sourcePort >= (int) getLayout (source).outputs.size()
        || destPort < 0 || destPort >= (int) getLayout (dest).inputs.size())
        return "That port doesn't exist";

    graph::WireDesc wire { 0, source, sourcePort, dest, destPort, 1.0f };
    if (auto error = translateWire (wire); ! error.empty())
        return error;

    return graph::checkConnection (toGraphDesc(), wire.sourceNode, wire.sourcePort, wire.destNode, wire.destPort);
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
        const auto source = getLayout (nodeRef (w, ids::source));
        const auto dest = getLayout (nodeRef (w, ids::dest));

        if ((int) w[ids::sourcePort] >= (int) source.outputs.size() || (int) w[ids::destPort] >= (int) dest.inputs.size())
            dangling.add (idOf (w));
    }

    removeWires (dangling);
}

//==============================================================================
juce::int64 Session::addPanel (const juce::String& name)
{
    const auto id = (juce::int64) takeId();
    juce::ValueTree panel (ids::panel);
    panel.setProperty (ids::id, id, nullptr);
    panel.setProperty (ids::name, name, nullptr);
    getPanels().appendChild (panel, &undo);
    return id;
}

void Session::removePanel (juce::int64 panelId)
{
    if (auto panel = findPanel (panelId); panel.isValid())
        getPanels().removeChild (panel, &undo);
}

juce::ValueTree Session::findPanel (juce::int64 panelId) const
{
    return getPanels().getChildWithProperty (ids::id, panelId);
}

juce::ValueTree Session::addFace (juce::ValueTree container, graph::NodeId node, juce::Point<float> position, FaceSize size)
{
    if (! container.isValid() || ! findNode (node).isValid())
        return {};

    juce::ValueTree face (ids::face);
    face.setProperty (ids::id, (juce::int64) takeId(), nullptr);
    face.setProperty (ids::node, idVar (node), nullptr);
    face.setProperty (ids::x, position.x, nullptr);
    face.setProperty (ids::y, position.y, nullptr);
    face.setProperty (ids::size, (int) size, nullptr);
    container.appendChild (face, &undo);
    return face;
}

juce::ValueTree Session::addFaceGroup (juce::ValueTree panel, const juce::String& name, juce::Point<float> position)
{
    if (! panel.isValid())
        return {};

    juce::ValueTree group (ids::faceGroup);
    group.setProperty (ids::id, (juce::int64) takeId(), nullptr);
    group.setProperty (ids::name, name, nullptr);
    group.setProperty (ids::x, position.x, nullptr);
    group.setProperty (ids::y, position.y, nullptr);
    group.setProperty (ids::collapsed, false, nullptr);
    panel.appendChild (group, &undo);
    return group;
}

void Session::moveFaceItem (juce::ValueTree item, juce::ValueTree newContainer, juce::Point<float> position)
{
    if (! item.isValid() || ! newContainer.isValid())
        return;

    // Face groups don't nest.
    if (item.hasType (ids::faceGroup) && newContainer.hasType (ids::faceGroup))
        newContainer = newContainer.getParent();

    if (auto oldContainer = item.getParent(); oldContainer != newContainer)
    {
        oldContainer.removeChild (item, &undo);
        newContainer.appendChild (item, &undo);
    }

    item.setProperty (ids::x, position.x, &undo);
    item.setProperty (ids::y, position.y, &undo);
}

//==============================================================================
void Session::setView (const juce::ValueTree& view)
{
    auto copy = view.createCopy();
    auto old = getView();
    old.copyPropertiesAndChildrenFrom (copy, nullptr);
}

juce::StringArray Session::getLayoutNames() const
{
    juce::StringArray names;
    for (const auto& l : layoutsTree())
        names.add (l[ids::name].toString());
    return names;
}

void Session::storeLayout (const juce::String& name)
{
    removeLayout (name);
    juce::ValueTree layout (ids::layout);
    layout.copyPropertiesAndChildrenFrom (getView(), nullptr);
    layout.setProperty (ids::name, name, nullptr);
    layoutsTree().appendChild (layout, nullptr);
    getView().setProperty (ids::current, name, nullptr);
}

bool Session::recallLayout (const juce::String& name)
{
    const auto layout = layoutsTree().getChildWithProperty (ids::name, name);
    if (! layout.isValid())
        return false;

    getView().copyPropertiesAndChildrenFrom (layout, nullptr);
    getView().removeProperty (ids::name, nullptr);
    getView().setProperty (ids::current, name, nullptr);
    return true;
}

void Session::removeLayout (const juce::String& name)
{
    if (auto layout = layoutsTree().getChildWithProperty (ids::name, name); layout.isValid())
        layoutsTree().removeChild (layout, nullptr);
}

//==============================================================================
graph::GraphDesc Session::toGraphDesc() const
{
    graph::GraphDesc d;

    // Groups are flattened away: their pins stay (passing the signal through) and wires
    // on a group's ports go to the pins instead.
    PinMap pins;
    std::set<graph::NodeId> groups;
    const auto groupType = juce::String (types::group.data(), types::group.size());
    const auto inputType = juce::String (types::groupInput.data(), types::groupInput.size());
    const auto outputType = juce::String (types::groupOutput.data(), types::groupOutput.size());

    for (const auto& n : nodesTree())
    {
        const auto type = n[ids::type].toString();
        if (type == groupType)
        {
            groups.insert (idOf (n));
            continue;
        }

        if (type == inputType)
            pins.inputs[nodeRef (n, ids::parent)].push_back (idOf (n));
        else if (type == outputType)
            pins.outputs[nodeRef (n, ids::parent)].push_back (idOf (n));

        d.nodes.push_back ({ idOf (n), type.toStdString(), getParams (idOf (n)) });
    }

    for (auto* map : { &pins.inputs, &pins.outputs })
        for (auto& [group, list] : *map)
            std::sort (list.begin(), list.end());

    auto resolve = [&groups] (std::map<graph::NodeId, std::vector<graph::NodeId>>& map, graph::NodeId& node, int& port)
    {
        if (groups.count (node) == 0)
            return true;

        const auto& list = map[node];
        if (port < 0 || port >= (int) list.size())
            return false;

        node = list[(size_t) port];
        port = 0;
        return true;
    };

    for (const auto& w : wiresTree())
    {
        graph::WireDesc wire { idOf (w), nodeRef (w, ids::source), (int) w[ids::sourcePort],
                               nodeRef (w, ids::dest), (int) w[ids::destPort], (float) w[ids::gain] };

        if (resolve (pins.outputs, wire.sourceNode, wire.sourcePort) && resolve (pins.inputs, wire.destNode, wire.destPort))
            d.wires.push_back (wire);
    }

    return d;
}

juce::String Session::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "Stage Plot Mixer session");
    root->setProperty (ids::version, fileVersion);
    root->setProperty (ids::nextId, state[ids::nextId]);
    root->setProperty (ids::showLock, isShowLocked());

    juce::Array<juce::var> nodeList, wireList, panelList, layoutList;
    for (const auto& n : nodesTree()) nodeList.add (propertiesOf (n));
    for (const auto& w : wiresTree()) wireList.add (propertiesOf (w));
    for (const auto& p : getPanels()) panelList.add (treeToVar (p, false));
    for (const auto& l : layoutsTree()) layoutList.add (treeToVar (l, false));

    root->setProperty ("nodes", nodeList);
    root->setProperty ("wires", wireList);
    root->setProperty ("panels", panelList);
    root->setProperty ("view", treeToVar (getView(), false));
    root->setProperty ("layouts", layoutList);
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

    std::function<void (const juce::ValueTree&)> noteId = [&] (const juce::ValueTree& tree)
    {
        highestId = juce::jmax (highestId, (juce::int64) tree[ids::id]);
        for (const auto& child : tree)
            noteId (child);
    };

    if (auto* list = root["nodes"].getArray())
        for (const auto& n : *list)
            if (auto tree = fromProperties (ids::node, n); (juce::int64) tree[ids::id] > 0)
            {
                noteId (tree);
                nodesTree().appendChild (tree, nullptr);
            }

    // A parent that's missing or isn't a group (a damaged file): the node goes to the top.
    for (auto n : nodesTree())
    {
        const auto parent = nodeRef (n, ids::parent);
        if (n.hasProperty (ids::parent) && (parent == 0 || ! isGroup (parent) || isInside (parent, idOf (n))))
            n.removeProperty (ids::parent, nullptr);
    }

    for (int i = nodesTree().getNumChildren(); --i >= 0;)
        if (auto n = nodesTree().getChild (i); isPinType (n) && ! n.hasProperty (ids::parent))
            nodesTree().removeChild (i, nullptr);  // a pin outside any group

    if (auto* list = root["wires"].getArray())
        for (const auto& w : *list)
            if (auto tree = fromProperties (ids::wire, w); (juce::int64) tree[ids::id] > 0)
            {
                const auto source = nodeRef (tree, ids::source), dest = nodeRef (tree, ids::dest);
                if (findNode (source).isValid() && findNode (dest).isValid() && getParent (source) == getParent (dest))
                {
                    noteId (tree);
                    wiresTree().appendChild (tree, nullptr);
                }
            }

    if (auto* list = root["panels"].getArray())
        for (const auto& p : *list)
        {
            auto panel = varToTree (ids::panel, p);
            noteId (panel);
            getPanels().appendChild (panel, nullptr);
        }

    if (root["view"].isObject())
        getView().copyPropertiesAndChildrenFrom (varToTree (ids::view, root["view"]), nullptr);

    if (auto* list = root["layouts"].getArray())
        for (const auto& l : *list)
            layoutsTree().appendChild (varToTree (ids::layout, l), nullptr);

    state.setProperty (ids::showLock, (bool) root[ids::showLock], nullptr);
    state.setProperty (ids::nextId, juce::jmax ((juce::int64) root[ids::nextId], highestId + 1), nullptr);
    undo.clearUndoHistory();
    return {};
}

} // namespace spm::model
