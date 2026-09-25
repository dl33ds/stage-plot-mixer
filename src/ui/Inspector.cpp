// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/Inspector.h"

#include "engine/Smoother.h"

#include "ui/Icons.h"
#include "ui/ParamFormat.h"
#include "ui/Theme.h"

namespace spm::ui
{

namespace
{

constexpr int headerHeight = 64, labelWidth = 96, padding = 14;

juce::String layoutSignature (const nodes::PortLayout& layout)
{
    juce::String s;
    for (auto& p : layout.inputs) s << "i" << p.channels;
    for (auto& p : layout.outputs) s << "o" << p.channels;
    return s;
}

juce::String describePorts (const std::vector<nodes::PortInfo>& ports)
{
    if (ports.empty())
        return "none";

    juce::StringArray parts;
    for (auto& p : ports)
        parts.add (juce::String (p.name) + " (" + (p.channels == 1 ? "mono" : p.channels == 2 ? "stereo" : juce::String (p.channels) + " ch") + ")");

    if (parts.size() > 6)
        return juce::String (parts.size()) + " ports, " + parts[0].fromFirstOccurrenceOf ("(", true, false) + " each";
    return parts.joinIntoString (", ");
}

float linearToWireDb (float gain)
{
    return gain <= 0.0f ? -60.0f : engine::gainToDecibels (gain, -60.0f);
}

float wireDbToLinear (float db)
{
    return db <= -60.0f ? 0.0f : engine::decibelsToGain (db);
}

void styleSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 22);
    s.setScrollWheelEnabled (false);
    s.setColour (juce::Slider::textBoxOutlineColourId, theme::border);
    s.setColour (juce::Slider::textBoxBackgroundColourId, theme::background);
    s.setColour (juce::Slider::textBoxTextColourId, theme::text);
}

} // namespace

Inspector::Inspector (model::Session& s, Selection& sel) : session (s), selection (sel)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    session.getState().addListener (this);
    selection.addChangeListener (this);
    rebuild();
}

Inspector::~Inspector()
{
    selection.removeChangeListener (this);
    session.getState().removeListener (this);
}

juce::String Inspector::currentKey() const
{
    if (selection.getNodes().size() == 1)
    {
        const auto id = selection.getNodes().getFirst();
        const auto* type = session.getNodeType (id);
        return "node:" + juce::String ((juce::int64) id) + ":" + (type != nullptr ? juce::String (type->id) : "?") + ":"
               + layoutSignature (session.getLayout (id));
    }

    if (selection.getNodes().size() > 1)
        return "nodes:" + juce::String (selection.getNodes().size());

    if (selection.getWires().size() >= 1)
    {
        // Includes the node names shown in the description.
        const auto wire = session.findWire (selection.getWires().getFirst());
        return "wire:" + juce::String ((juce::int64) selection.getWires().getFirst()) + ":"
               + session.getNodeName ((graph::NodeId) (juce::int64) wire[model::ids::source]) + ":"
               + session.getNodeName ((graph::NodeId) (juce::int64) wire[model::ids::dest]);
    }

    return "none:" + juce::String (session.getNodeIds().size());
}

void Inspector::handleAsyncUpdate()
{
    if (currentKey() != builtKey)
        rebuild();
    else
        refreshValues();
}

void Inspector::refreshValues()
{
    for (auto& row : rows)
        if (row.refresh)
            row.refresh();

    // The name might have changed.
    if (selection.getNodes().size() == 1)
    {
        const auto name = session.getNodeName (selection.getNodes().getFirst());
        if (name != title)
        {
            title = name;
            repaint();
        }
    }
}

void Inspector::rebuild()
{
    rows.clear();
    builtKey = currentKey();
    iconName = {};
    subtitle = {};
    titleColour = theme::textMuted;

    if (selection.getNodes().size() == 1)
        buildForNode (selection.getNodes().getFirst());
    else if (selection.getNodes().size() > 1)
        buildForNodes (selection.getNodes().size());
    else if (! selection.getWires().isEmpty())
        buildForWire (selection.getWires().getFirst());
    else
        buildEmpty();

    for (auto& row : rows)
    {
        if (row.label != nullptr)
            content.addAndMakeVisible (*row.label);
        if (row.control != nullptr)
            content.addAndMakeVisible (*row.control);
    }

    refreshValues();
    layoutContent();
    repaint();
}

juce::Label* Inspector::addHeading (const juce::String& text)
{
    auto label = std::make_unique<juce::Label> (juce::String(), text.toUpperCase());
    label->setFont (theme::font (10.5f, theme::Weight::semiBold));
    label->setColour (juce::Label::textColourId, theme::textMuted);
    label->setBorderSize ({ 10, 0, 0, 0 });
    auto* raw = label.get();
    rows.push_back ({ std::move (label), nullptr, 30, true, nullptr });
    return raw;
}

juce::Label* Inspector::addText (const juce::String& text, int height, bool muted)
{
    auto label = std::make_unique<juce::Label> (juce::String(), text);
    label->setFont (theme::font (12.0f));
    label->setColour (juce::Label::textColourId, muted ? theme::textMuted : theme::text);
    label->setJustificationType (juce::Justification::topLeft);
    label->setBorderSize ({ 2, 0, 2, 0 });
    label->setMinimumHorizontalScale (1.0f);
    auto* raw = label.get();
    rows.push_back ({ std::move (label), nullptr, height, true, nullptr });
    return raw;
}

void Inspector::addRow (const juce::String& labelText, std::unique_ptr<juce::Component> control, std::function<void()> refresh, int height)
{
    auto label = std::make_unique<juce::Label> (juce::String(), labelText);
    label->setFont (theme::font (12.0f));
    label->setColour (juce::Label::textColourId, theme::textMuted);
    label->setBorderSize ({ 0, 0, 0, 4 });
    label->setMinimumHorizontalScale (1.0f);
    rows.push_back ({ std::move (label), std::move (control), height, false, std::move (refresh) });
}

void Inspector::addButtons()
{
    auto duplicate = std::make_unique<IconButton> ("copy", "Duplicate (Ctrl+D)", "Duplicate");
    auto remove = std::make_unique<IconButton> ("trash", "Delete (Del)", "Delete");
    duplicate->onClick = [this] { if (onDuplicate) onDuplicate(); };
    remove->onClick = [this] { if (onDelete) onDelete(); };

    struct Pair final : juce::Component
    {
        std::unique_ptr<IconButton> a, b;
        void resized() override
        {
            auto r = getLocalBounds();
            if (a != nullptr) a->setBounds (r.removeFromLeft (a->getIdealWidth (r.getHeight())));
            r.removeFromLeft (8);
            if (b != nullptr) b->setBounds (r.removeFromLeft (b->getIdealWidth (r.getHeight())));
        }
    };

    auto pair = std::make_unique<Pair>();
    pair->a = std::move (duplicate);
    pair->b = std::move (remove);
    pair->addAndMakeVisible (*pair->a);
    pair->addAndMakeVisible (*pair->b);
    rows.push_back ({ nullptr, std::move (pair), 44, true, nullptr });
}

void Inspector::buildForNode (graph::NodeId id)
{
    const auto* type = session.getNodeType (id);
    title = session.getNodeName (id);

    if (type == nullptr)
    {
        subtitle = "Unknown node type";
        addText ("This node's type isn't available in this version, so it's left out of the audio.", 48);
        addButtons();
        return;
    }

    subtitle = type->name;
    iconName = type->icon;
    titleColour = theme::categoryColour (type->category);

    // Name.
    {
        auto editor = std::make_unique<juce::TextEditor>();
        auto* ed = editor.get();
        ed->setFont (theme::font (13.0f));
        ed->setIndents (6, 4);
        ed->setColour (juce::TextEditor::backgroundColourId, theme::background);
        ed->setColour (juce::TextEditor::outlineColourId, theme::border);
        ed->setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        ed->setColour (juce::TextEditor::textColourId, theme::text);
        auto commit = [this, id, ed]
        {
            const auto name = ed->getText().trim();
            if (name.isNotEmpty() && name != session.getNodeName (id))
            {
                session.beginAction ("Rename");
                session.setNodeName (id, name);
            }
            else
            {
                ed->setText (session.getNodeName (id), false);
            }
        };
        ed->onReturnKey = commit;
        ed->onFocusLost = commit;
        ed->onEscapeKey = [this, id, ed] { ed->setText (session.getNodeName (id), false); ed->unfocusAllComponents(); };
        addRow ("Name", std::move (editor), [this, id, ed]
        {
            if (! ed->hasKeyboardFocus (true))
                ed->setText (session.getNodeName (id), false);
        });
    }

    addText (type->description, 36);

    if (! type->params.empty())
        addHeading ("Settings");

    const auto isHardware = type->id == nodes::types::hardwareInput || type->id == nodes::types::hardwareOutput;
    const auto lockStructure = type->id == nodes::types::recorder && isRecording && isRecording();

    for (int i = 0; i < (int) type->params.size(); ++i)
    {
        const auto& spec = type->params[(size_t) i];
        const auto tooltip = lockStructure && spec.structural
                                 ? juce::String ("Can't change while recording")
                                 : juce::String (spec.tooltip.empty() ? spec.name : spec.tooltip)
                                       + (spec.structural ? " (changes the node's ports)" : "");

        if (spec.kind == nodes::ParamKind::toggle)
        {
            auto toggle = std::make_unique<juce::ToggleButton>();
            auto* t = toggle.get();
            t->setTooltip (tooltip);
            t->onClick = [this, id, i, t, spec]
            {
                session.beginAction ("Change " + juce::String (spec.name));
                session.setParam (id, i, t->getToggleState() ? 1.0f : 0.0f);
            };
            addRow (spec.name, std::move (toggle), [this, id, i, t]
            {
                t->setToggleState (session.getParam (id, i) >= 0.5f, juce::dontSendNotification);
            });
            continue;
        }

        // Lists: choices, channel numbers, and structural counts (a change rebuilds the node,
        // so a list is better than a slider that would rebuild it at every step).
        if (spec.kind == nodes::ParamKind::choice || (isHardware && spec.id == "first")
            || (spec.kind == nodes::ParamKind::integer && spec.structural))
        {
            auto combo = std::make_unique<juce::ComboBox>();
            auto* c = combo.get();
            c->setTooltip (tooltip);

            std::function<float (int)> itemToValue;

            if (spec.kind == nodes::ParamKind::choice)
            {
                for (int k = 0; k < (int) spec.choices.size(); ++k)
                    c->addItem (spec.choices[(size_t) k], k + 1);
                itemToValue = [] (int item) { return (float) (item - 1); };
            }
            else if (isHardware && spec.id == "first")
            {
                const auto input = type->id == nodes::types::hardwareInput;
                const auto names = getDeviceChannelNames ? getDeviceChannelNames (input) : juce::StringArray();
                const auto layout = session.getLayout (id);
                const auto count = input ? (layout.outputs.empty() ? 1 : layout.outputs[0].channels)
                                         : (layout.inputs.empty() ? 1 : layout.inputs[0].channels);
                const auto available = std::max (count, names.isEmpty() ? 32 : names.size());

                for (int first = 1; first + count - 1 <= available; ++first)
                {
                    auto text = count == 1 ? juce::String (first) : juce::String (first) + "-" + juce::String (first + count - 1);
                    if (first - 1 < names.size())
                        text << "  " << names[first - 1];
                    c->addItem (text, first);
                }

                // Keep a saved channel that the current interface doesn't have.
                const auto current = juce::roundToInt (session.getParam (id, i));
                if (current + count - 1 > available)
                    c->addItem (juce::String (current) + " (not on this interface)", current);

                itemToValue = [] (int item) { return (float) item; };
            }
            else
            {
                for (int v = (int) spec.minValue; v <= (int) spec.maxValue; ++v)
                    c->addItem (juce::String (v), v - (int) spec.minValue + 1);
                const auto base = (int) spec.minValue;
                itemToValue = [base] (int item) { return (float) (item - 1 + base); };
            }

            c->onChange = [this, id, i, c, spec, itemToValue]
            {
                const auto value = itemToValue (c->getSelectedId());
                if (juce::roundToInt (value) != juce::roundToInt (session.getParam (id, i)))
                {
                    session.beginAction ("Change " + juce::String (spec.name));
                    session.setParam (id, i, value);
                }
            };

            c->setEnabled (! (lockStructure && spec.structural));
            addRow (spec.name, std::move (combo), [this, id, i, c, spec]
            {
                const auto value = juce::roundToInt (session.getParam (id, i));
                const auto item = spec.kind == nodes::ParamKind::choice ? value + 1
                                  : spec.id == "first" ? value
                                                       : value - (int) spec.minValue + 1;
                c->setSelectedId (item, juce::dontSendNotification);
            });
            continue;
        }

        auto slider = std::make_unique<juce::Slider>();
        auto* s = slider.get();
        styleSlider (*s);
        configureSlider (*s, spec);
        s->setTooltip (tooltip);
        s->onDragStart = [this, spec] { session.beginAction ("Change " + juce::String (spec.name)); };
        s->onValueChange = [this, id, i, s, spec]
        {
            if (! s->isMouseButtonDown())
                session.beginAction ("Change " + juce::String (spec.name));
            session.setParam (id, i, (float) s->getValue());
        };
        addRow (spec.name, std::move (slider), [this, id, i, s]
        {
            if (! s->isMouseButtonDown())
                s->setValue (session.getParam (id, i), juce::dontSendNotification);
        });
    }

    const auto layout = session.getLayout (id);
    addHeading ("Ports");
    addText ("Inputs: " + describePorts (layout.inputs), 24, false);
    addText ("Outputs: " + describePorts (layout.outputs), 24, false);

    addButtons();
}

void Inspector::buildForWire (graph::WireId wireId)
{
    const auto wire = session.findWire (wireId);
    title = "Wire";
    iconName = "git-merge";
    titleColour = theme::accent;

    const auto source = (graph::NodeId) (juce::int64) wire[model::ids::source];
    const auto dest = (graph::NodeId) (juce::int64) wire[model::ids::dest];
    const auto sourceLayout = session.getLayout (source), destLayout = session.getLayout (dest);
    const auto sourcePort = (int) wire[model::ids::sourcePort], destPort = (int) wire[model::ids::destPort];

    auto portName = [] (const std::vector<nodes::PortInfo>& ports, int p)
    {
        return p >= 0 && p < (int) ports.size() ? juce::String (ports[(size_t) p].name) : juce::String ("?");
    };

    subtitle = "Carries audio from one node to another";
    addRow ("From", std::make_unique<juce::Label> (juce::String(), session.getNodeName (source) + juce::String::fromUTF8 ("  \xe2\x80\xba  ") + portName (sourceLayout.outputs, sourcePort)), nullptr, 26);
    addRow ("To", std::make_unique<juce::Label> (juce::String(), session.getNodeName (dest) + juce::String::fromUTF8 ("  \xe2\x80\xba  ") + portName (destLayout.inputs, destPort)), nullptr, 26);

    for (auto& row : rows)
        if (auto* label = dynamic_cast<juce::Label*> (row.control.get()))
        {
            label->setFont (theme::font (12.0f, theme::Weight::medium));
            label->setColour (juce::Label::textColourId, theme::text);
        }

    addHeading ("Level");
    auto slider = std::make_unique<juce::Slider>();
    auto* s = slider.get();
    styleSlider (*s);
    configureWireGainSlider (*s);
    s->onDragStart = [this] { session.beginAction ("Change wire gain"); };
    s->onValueChange = [this, wireId, s]
    {
        if (! s->isMouseButtonDown())
            session.beginAction ("Change wire gain");
        session.setWireGain (wireId, wireDbToLinear ((float) s->getValue()));
    };
    addRow ("Gain", std::move (slider), [this, wireId, s]
    {
        if (! s->isMouseButtonDown())
            s->setValue (linearToWireDb ((float) session.findWire (wireId)[model::ids::gain]), juce::dontSendNotification);
    });

    addText ("Where the channel counts differ, a mono signal is copied to every channel, and several channels "
             "into a mono input are averaged.", 64);

    auto remove = std::make_unique<IconButton> ("trash", "Delete this wire (Del)", "Delete wire");
    remove->onClick = [this] { if (onDelete) onDelete(); };
    rows.push_back ({ nullptr, std::move (remove), 44, true, nullptr });
}

void Inspector::buildForNodes (int count)
{
    title = juce::String (count) + " nodes selected";
    subtitle = "Drag any of them to move them all";
    iconName = "circle-dot";
    titleColour = theme::accent;
    addButtons();
}

void Inspector::buildEmpty()
{
    title = "Nothing selected";
    subtitle = "Click a node or wire to see its settings";
    iconName = "circle-dot";

    addHeading ("Getting around");
    addText (juce::String::fromUTF8 (
                 "Tab or double-click: add a node\n"
                 "Drag from a port: connect\n"
                 "Drag a wire off an input: move or remove it\n"
                 "Drag on empty space: select several\n"
                 "Right or middle drag, or Space + drag: pan\n"
                 "Wheel: zoom    F: fit everything\n"
                 "Delete: remove    Ctrl+D: duplicate\n"
                 "Double-click a node's title: rename\n"
                 "Ctrl+G: group    Ctrl+Shift+G: ungroup\n"
                 "Double-click a group: open it    Esc: back out\n"
                 "Right-click a node: add its face to a panel"),
             204, false);
}

void Inspector::layoutContent()
{
    const auto width = std::max (100, viewport.getMaximumVisibleWidth());
    auto y = 8;

    for (auto& row : rows)
    {
        auto r = juce::Rectangle<int> (padding, y, width - 2 * padding, row.height);

        if (row.fullWidth)
        {
            if (row.label != nullptr)
                row.label->setBounds (r);
            if (row.control != nullptr)
                row.control->setBounds (r.reduced (0, 6));
        }
        else
        {
            if (row.label != nullptr)
                row.label->setBounds (r.removeFromLeft (labelWidth));
            if (row.control != nullptr)
                row.control->setBounds (r.reduced (0, 3));
        }

        y += row.height;
    }

    content.setSize (width, y + 12);
}

void Inspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (headerHeight).withTrimmedLeft (1));
    layoutContent();
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (theme::surface);
    g.setColour (theme::border);
    g.fillRect (0, 0, 1, getHeight());

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 12).withTrimmedLeft (1);
    g.fillRect (1, headerHeight - 1, getWidth() - 1, 1);

    if (iconName.isNotEmpty())
    {
        drawIcon (g, iconName, header.removeFromLeft (22).toFloat().withSizeKeepingCentre (18.0f, 18.0f), titleColour);
        header.removeFromLeft (8);
    }

    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, theme::Weight::semiBold));
    g.drawText (title, header.removeFromTop (header.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.5f));
    g.drawText (subtitle, header.withTrimmedTop (2), juce::Justification::topLeft, true);
}

} // namespace spm::ui
