// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/NodeComponent.h"

#include "ui/ParamFormat.h"
#include "ui/Theme.h"

namespace spm::ui
{

namespace
{

constexpr int margin = 8;           // room outside the body for the port dots
constexpr float portRadius = 5.0f;
constexpr int sliderHeight = 24, toggleRowHeight = 26;

juce::String describeChannels (int channels)
{
    if (channels == 1) return "mono";
    if (channels == 2) return "stereo";
    return juce::String (channels) + " ch";
}

bool isBipolar (const nodes::ParamSpec& spec)
{
    return spec.minValue < 0.0f && spec.maxValue > 0.0f && ! spec.minusInfinityAtMinimum && spec.unit != "dB";
}

juce::Colour toggleColour (const nodes::ParamSpec& spec)
{
    if (spec.id == "mute") return theme::danger;
    if (spec.id == "invert") return theme::warning;
    return theme::good;
}

juce::String toggleText (const nodes::ParamSpec& spec)
{
    if (spec.id == "invert") return juce::String::fromUTF8 ("\xc3\x98");  // Ø, the usual polarity mark
    return spec.name;
}

} // namespace

//==============================================================================
class NodeComponent::InlineSlider final : public juce::Slider
{
public:
    explicit InlineSlider (const nodes::ParamSpec& s) : spec (s)
    {
        setSliderStyle (juce::Slider::LinearBar);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setSliderSnapsToMousePosition (false);
        setScrollWheelEnabled (false);
        setMouseDragSensitivity (300);
        configureSlider (*this, spec);
        setTooltip (juce::String (spec.name) + " - drag to change, double-click to reset. More in the inspector.");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        g.setColour (isMouseOverOrDragging() ? theme::surfaceHigh.brighter (0.08f) : theme::surfaceHigh);
        g.fillRoundedRectangle (r, 4.0f);

        const auto pos = (float) valueToProportionOfLength (getValue());
        const auto origin = isBipolar (spec) ? (float) valueToProportionOfLength (0.0) : 0.0f;
        const auto x1 = r.getX() + r.getWidth() * std::min (pos, origin), x2 = r.getX() + r.getWidth() * std::max (pos, origin);

        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (r, 4.0f);
        g.reduceClipRegion (clip);
        g.setColour (theme::accent.withAlpha (0.28f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, r.getY(), x2, r.getBottom()));
        g.setColour (theme::accent);
        g.fillRect (r.getX() + r.getWidth() * pos - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.restoreState();

        auto text = r.reduced (7.0f, 0.0f);
        g.setFont (theme::font (11.0f));
        g.setColour (theme::textMuted);
        g.drawText (spec.name, text, juce::Justification::centredLeft, true);
        g.setFont (theme::font (11.0f, theme::Weight::medium, true));
        g.setColour (theme::text);
        g.drawText (getTextFromValue (getValue()), text, juce::Justification::centredRight, true);
    }

    const nodes::ParamSpec spec;
};

//==============================================================================
class NodeComponent::InlineToggle final : public juce::Button
{
public:
    InlineToggle (const nodes::ParamSpec& s, int index) : juce::Button (s.name), spec (s), param (index)
    {
        setTooltip (spec.tooltip.empty() ? juce::String (spec.name) : juce::String (spec.tooltip));
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        const auto on = getToggleState();
        auto fill = on ? toggleColour (spec) : theme::surfaceHigh;
        if (highlighted || down)
            fill = fill.brighter (0.1f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? theme::background : theme::textMuted);
        g.setFont (theme::font (11.0f, theme::Weight::semiBold));
        g.drawText (toggleText (spec), r, juce::Justification::centred, false);
    }

    const nodes::ParamSpec spec;
    const int param;
};

//==============================================================================
class NodeComponent::InlineChoice final : public juce::Button
{
public:
    InlineChoice (juce::String labelText, std::function<juce::PopupMenu()> menu, std::function<void (int)> chosen)
        : juce::Button (labelText), label (std::move (labelText)), makeMenu (std::move (menu)), onChosen (std::move (chosen))
    {
        onClick = [this]
        {
            makeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                      [safe = juce::Component::SafePointer (this)] (int result)
                                      {
                                          if (safe != nullptr && result != 0)
                                              safe->onChosen (result);
                                      });
        };
    }

    void setValueText (const juce::String& text)
    {
        if (text != value)
        {
            value = text;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        g.setColour ((highlighted || down) ? theme::surfaceHigh.brighter (0.08f) : theme::surfaceHigh);
        g.fillRoundedRectangle (r, 4.0f);

        auto text = r.reduced (7.0f, 0.0f);
        auto arrow = text.removeFromRight (10.0f);
        juce::Path p;
        p.addTriangle (arrow.getX(), arrow.getCentreY() - 2.0f, arrow.getRight(), arrow.getCentreY() - 2.0f,
                       arrow.getCentreX(), arrow.getCentreY() + 3.0f);
        g.setColour (theme::textMuted);
        g.fillPath (p);

        g.setFont (theme::font (11.0f));
        g.drawText (label, text, juce::Justification::centredLeft, true);
        g.setFont (theme::font (11.0f, theme::Weight::medium, true));
        g.setColour (theme::text);
        g.drawText (value, text.withTrimmedRight (6.0f), juce::Justification::centredRight, true);
    }

private:
    juce::String label, value;
    std::function<juce::PopupMenu()> makeMenu;
    std::function<void (int)> onChosen;
};

//==============================================================================
NodeComponent::NodeComponent (GraphCanvas& c, graph::NodeId nodeId) : canvas (c), id (nodeId)
{
    setRepaintsOnMouseActivity (false);
    update();
}

NodeComponent::~NodeComponent() = default;

void NodeComponent::update()
{
    auto& session = canvas.getSession();
    const auto* newType = session.getNodeType (id);
    auto newLayout = session.getLayout (id);

    const auto sameLayout = [] (const nodes::PortLayout& a, const nodes::PortLayout& b)
    {
        const auto samePorts = [] (const std::vector<nodes::PortInfo>& x, const std::vector<nodes::PortInfo>& y)
        {
            return std::equal (x.begin(), x.end(), y.begin(), y.end(),
                               [] (auto& p, auto& q) { return p.name == q.name && p.channels == q.channels; });
        };
        return samePorts (a.inputs, b.inputs) && samePorts (a.outputs, b.outputs);
    };

    const auto structureChanged = newType != type || ! sameLayout (newLayout, layout) || ! built;
    built = true;
    type = newType;
    layout = std::move (newLayout);

    const auto newName = session.getNodeName (id);
    if (newName != name)
    {
        name = newName;
        repaint();
    }

    if (structureChanged)
        rebuildControls();

    updateControlValues();

    const auto pos = session.getNodePosition (id);
    const auto x = juce::roundToInt (pos.x) - margin, y = juce::roundToInt (pos.y);
    if (getX() != x || getY() != y)
        setTopLeftPosition (x, y);
}

void NodeComponent::rebuildControls()
{
    controls.clear();
    toggles.clear();
    toggleRow = nullptr;

    if (type == nullptr)
    {
        setSize (width + 2 * margin, headerHeight + 40);
        return;
    }

    const auto isHardware = type->id == nodes::types::hardwareInput || type->id == nodes::types::hardwareOutput;

    for (int i = 0; i < (int) type->params.size(); ++i)
    {
        const auto& spec = type->params[(size_t) i];

        if (spec.structural)
            continue;  // channel counts and the like: in the inspector

        if (isHardware && spec.id == "first")
        {
            const auto input = type->id == nodes::types::hardwareInput;

            auto choice = std::make_unique<InlineChoice> (
                "Channels",
                [this, input]
                {
                    auto names = canvas.getDeviceChannelNames ? canvas.getDeviceChannelNames (input) : juce::StringArray();
                    const auto count = layout.inputs.empty() && layout.outputs.empty() ? 1
                                       : (input ? layout.outputs[0].channels : layout.inputs[0].channels);
                    const auto available = names.isEmpty() ? 32 : names.size();
                    const auto current = juce::roundToInt (canvas.getSession().getParam (id, 0));

                    juce::PopupMenu menu;
                    menu.addSectionHeader (input ? "Interface inputs" : "Interface outputs");
                    for (int first = 1; first + count - 1 <= std::max (count, available); ++first)
                    {
                        auto text = count == 1 ? juce::String (first) : juce::String (first) + "-" + juce::String (first + count - 1);
                        if (first - 1 < names.size())
                            text << "   " << names[first - 1] << (count > 1 && first + count - 2 < names.size() ? " / " + names[first + count - 2] : "");
                        menu.addItem (first, text, true, first == current);
                    }
                    return menu;
                },
                [this, i] (int first) { setParamFromControl (i, (float) first, true); });

            choice->setTooltip (input ? "Which interface inputs this node reads" : "Which interface outputs this node plays to");
            controls.push_back ({ i, std::move (choice), sliderHeight });
            continue;
        }

        switch (spec.kind)
        {
            case nodes::ParamKind::toggle:
            {
                if (toggleRow == nullptr)
                    toggleRow = std::make_unique<juce::Component>();

                auto toggle = std::make_unique<InlineToggle> (spec, i);
                auto* t = toggle.get();
                t->onClick = [this, t] { setParamFromControl (t->param, t->getToggleState() ? 0.0f : 1.0f, true); };
                toggleRow->addAndMakeVisible (*t);
                toggles.push_back (std::move (toggle));
                break;
            }

            case nodes::ParamKind::choice:
            {
                auto choice = std::make_unique<InlineChoice> (
                    spec.name,
                    [this, spec, i]
                    {
                        juce::PopupMenu menu;
                        const auto current = juce::roundToInt (canvas.getSession().getParam (id, i));
                        for (int c = 0; c < (int) spec.choices.size(); ++c)
                            menu.addItem (c + 1, spec.choices[(size_t) c], true, c == current);
                        return menu;
                    },
                    [this, i] (int result) { setParamFromControl (i, (float) (result - 1), true); });
                controls.push_back ({ i, std::move (choice), sliderHeight });
                break;
            }

            case nodes::ParamKind::continuous:
            case nodes::ParamKind::integer:
            {
                auto slider = std::make_unique<InlineSlider> (spec);
                auto* s = slider.get();
                s->onDragStart = [this, s] { canvas.getSession().beginAction ("Change " + juce::String (s->spec.name)); };
                s->onValueChange = [this, s, i]
                {
                    setParamFromControl (i, (float) s->getValue(), ! s->isMouseButtonDown());
                };
                controls.push_back ({ i, std::move (slider), sliderHeight });
                break;
            }
        }
    }

    for (auto& c : controls)
        addAndMakeVisible (*c.component);

    if (toggleRow != nullptr)
    {
        addAndMakeVisible (*toggleRow);
        toggleRow->setInterceptsMouseClicks (false, true);
    }

    const auto rows = std::max (layout.inputs.size(), layout.outputs.size());
    auto height = headerHeight + 6 + (int) rows * portRowHeight() + 4;
    for (auto& c : controls)
        height += c.height;
    if (toggleRow != nullptr)
        height += toggleRowHeight;
    height += meterHeight() + 10;

    setSize (width + 2 * margin, height);
    resized();
    lastMeterStep = std::numeric_limits<int>::min();
}

void NodeComponent::updateControlValues()
{
    if (type == nullptr)
        return;

    auto& session = canvas.getSession();

    for (auto& c : controls)
    {
        const auto value = session.getParam (id, c.param);

        if (auto* slider = dynamic_cast<InlineSlider*> (c.component.get()))
        {
            if (! slider->isMouseButtonDown())
                slider->setValue (value, juce::dontSendNotification);
        }
        else if (auto* choice = dynamic_cast<InlineChoice*> (c.component.get()))
        {
            const auto& spec = type->params[(size_t) c.param];

            if (spec.id == "first")
            {
                const auto count = type->id == nodes::types::hardwareInput ? (layout.outputs.empty() ? 1 : layout.outputs[0].channels)
                                                                          : (layout.inputs.empty() ? 1 : layout.inputs[0].channels);
                const auto first = juce::roundToInt (value);
                choice->setValueText (count == 1 ? juce::String (first) : juce::String (first) + "-" + juce::String (first + count - 1));
            }
            else
            {
                choice->setValueText (formatParam (spec, value));
            }
        }
    }

    for (auto& toggle : toggles)
        toggle->setToggleState (session.getParam (id, toggle->param) >= 0.5f, juce::dontSendNotification);
}

void NodeComponent::setParamFromControl (int index, float value, bool newGesture)
{
    auto& session = canvas.getSession();

    if (newGesture && type != nullptr)
        session.beginAction ("Change " + juce::String (type->params[(size_t) index].name));

    session.setParam (id, index, value);
}

int NodeComponent::portRowHeight() const noexcept
{
    return std::max (layout.inputs.size(), layout.outputs.size()) > 8 ? 16 : 22;
}

juce::Rectangle<int> NodeComponent::portsArea() const
{
    const auto rows = (int) std::max (layout.inputs.size(), layout.outputs.size());
    return { margin, headerHeight + 6, width, rows * portRowHeight() };
}

int NodeComponent::meterHeight() const noexcept
{
    const auto channels = meterChannels();
    const auto big = type != nullptr && type->id == nodes::types::meter;
    const auto perChannel = big ? 10 : 5;
    return juce::jlimit (big ? 24 : 6, big ? 96 : 32, channels * perChannel);
}

juce::Rectangle<int> NodeComponent::meterArea() const
{
    return { margin + 10, getHeight() - 8 - meterHeight(), width - 20, meterHeight() };
}

void NodeComponent::resized()
{
    auto area = juce::Rectangle<int> (margin + 10, portsArea().getBottom() + 4, width - 20, getHeight());

    for (auto& c : controls)
        c.component->setBounds (area.removeFromTop (c.height));

    if (toggleRow != nullptr)
    {
        auto row = area.removeFromTop (toggleRowHeight);
        toggleRow->setBounds (row);

        const auto n = (int) toggles.size();
        auto local = row.withZeroOrigin();
        const auto gap = 4;
        const auto w = (local.getWidth() - gap * (n - 1)) / std::max (1, n);
        for (auto& t : toggles)
        {
            t->setBounds (local.removeFromLeft (w));
            local.removeFromLeft (gap);
        }
    }

    if (renameEditor != nullptr)
        renameEditor->setBounds (margin + 28, 5, width - 36, headerHeight - 8);
}

std::optional<juce::Point<float>> NodeComponent::getPortCentre (bool input, int port) const
{
    const auto& ports = input ? layout.inputs : layout.outputs;
    if (port < 0 || port >= (int) ports.size())
        return {};

    const auto area = portsArea();
    const auto y = (float) area.getY() + ((float) port + 0.5f) * (float) portRowHeight();
    return juce::Point<float> ((float) (input ? area.getX() : area.getRight()), y);
}

int NodeComponent::getPortChannels (bool input, int port) const noexcept
{
    const auto& ports = input ? layout.inputs : layout.outputs;
    return port >= 0 && port < (int) ports.size() ? ports[(size_t) port].channels : 0;
}

std::optional<PortRef> NodeComponent::portAt (juce::Point<float> p) const
{
    const auto reach = std::max (8.0f, (float) portRowHeight() * 0.5f);

    for (int input = 0; input < 2; ++input)
        for (int i = 0; i < getNumPorts (input != 0); ++i)
            if (auto c = getPortCentre (input != 0, i))
            {
                // Generous on the outside edge, a little way into the body.
                const auto dx = input != 0 ? p.x - c->x : c->x - p.x;
                if (dx > -reach - 2.0f && dx < 14.0f && std::abs (p.y - c->y) <= (float) portRowHeight() * 0.5f)
                    return PortRef { id, input != 0, i };
            }

    return {};
}

std::optional<std::pair<bool, int>> NodeComponent::meterPort() const
{
    // The single output if there is one (what this node sends on), otherwise the first input.
    if (layout.outputs.size() == 1 || (layout.inputs.empty() && ! layout.outputs.empty()))
        return std::pair { false, 0 };
    if (! layout.inputs.empty())
        return std::pair { true, 0 };
    return {};
}

int NodeComponent::meterChannels() const
{
    const auto port = meterPort();
    return port ? std::max (1, getPortChannels (port->first, port->second)) : 1;
}

const PortLevels* NodeComponent::meterLevels() const
{
    const auto port = meterPort();
    return port ? canvas.getMeters().get (id, port->first, port->second) : nullptr;
}

void NodeComponent::updateMeter()
{
    const auto* levels = meterLevels();
    auto step = 0;

    if (levels != nullptr)
    {
        for (size_t i = 0; i < levels->peakDb.size(); ++i)
            step = step * 31 + juce::roundToInt (levels->peakDb[i] * 2.0f) + juce::roundToInt (levels->rmsDb[i]) * 7;
        step = step * 2 + (levels->clipped ? 1 : 0);
    }

    if (step != lastMeterStep)
    {
        lastMeterStep = step;
        repaint (meterArea().expanded (2));
    }
}

void NodeComponent::paintMeter (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto* levels = meterLevels();
    const auto channels = levels != nullptr ? std::max (1, (int) levels->peakDb.size()) : meterChannels();

    auto clipArea = area.removeFromRight (6.0f);
    area.removeFromRight (3.0f);

    const auto toX = [&area] (float db)
    {
        return area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, (db - MeterCache::floorDb) / -MeterCache::floorDb);
    };

    juce::ColourGradient gradient (theme::good, toX (-60.0f), 0.0f, theme::danger, toX (0.0f), 0.0f, false);
    gradient.addColour ((toX (-18.0f) - area.getX()) / area.getWidth(), theme::good);
    gradient.addColour ((toX (-12.0f) - area.getX()) / area.getWidth(), theme::warning);
    gradient.addColour ((toX (-6.0f) - area.getX()) / area.getWidth(), theme::warning);
    gradient.addColour ((toX (-3.0f) - area.getX()) / area.getWidth(), theme::danger);

    const auto gap = channels > 8 ? 0.0f : 1.0f;
    const auto barHeight = (area.getHeight() - gap * (float) (channels - 1)) / (float) channels;

    for (int ch = 0; ch < channels; ++ch)
    {
        auto bar = juce::Rectangle<float> (area.getX(), area.getY() + (float) ch * (barHeight + gap), area.getWidth(), barHeight);
        g.setColour (theme::background);
        g.fillRect (bar);

        if (levels == nullptr || ch >= (int) levels->peakDb.size())
            continue;

        g.setGradientFill (gradient);
        const auto peak = toX (levels->peakDb[(size_t) ch]), rms = toX (levels->rmsDb[(size_t) ch]);
        g.setOpacity (0.45f);
        g.fillRect (bar.withRight (peak));
        g.setOpacity (1.0f);
        g.fillRect (bar.withRight (rms));
    }

    // Tick at -18 dBFS (a usual line-up level).
    g.setColour (theme::text.withAlpha (0.25f));
    g.fillRect (toX (-18.0f), area.getY(), 1.0f, area.getHeight());

    g.setColour (levels != nullptr && levels->clipped ? theme::danger : theme::surfaceHigh);
    g.fillRoundedRectangle (clipArea, 1.5f);
}

void NodeComponent::paint (juce::Graphics& g)
{
    const auto body = juce::Rectangle<float> ((float) margin, 0.0f, (float) width, (float) getHeight());
    const auto selected = canvas.getSelection().contains (id);
    const auto category = type != nullptr ? juce::String (type->category) : juce::String ("Unknown");
    const auto colour = theme::categoryColour (category);

    g.setColour (theme::surface);
    g.fillRoundedRectangle (body, theme::radius);

    // Header: category strip, icon, name.
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (body, theme::radius);
        g.reduceClipRegion (clip);
        g.setColour (colour);
        g.fillRect (body.withHeight (3.0f));
        g.setColour (theme::surfaceHigh.withAlpha (0.5f));
        g.fillRect (body.withTop (3.0f).withHeight ((float) headerHeight - 3.0f));
        g.restoreState();

        if (type != nullptr)
            drawIcon (g, type->icon, { body.getX() + 9.0f, 9.0f, 15.0f, 15.0f }, colour);

        if (renameEditor == nullptr)
        {
            g.setColour (theme::text);
            g.setFont (theme::font (13.0f, theme::Weight::semiBold));
            g.drawText (type != nullptr ? name : name + " (unknown type)",
                        juce::Rectangle<float> (body.getX() + 31.0f, 3.0f, body.getWidth() - 39.0f, (float) headerHeight - 3.0f),
                        juce::Justification::centredLeft, true);
        }
    }

    g.setColour (selected ? theme::accent : theme::border);
    g.drawRoundedRectangle (body.reduced (selected ? 1.0f : 0.5f), theme::radius, selected ? 2.0f : 1.0f);

    // Ports.
    const auto row = (float) portRowHeight();
    for (int input = 0; input < 2; ++input)
    {
        const auto& ports = input != 0 ? layout.inputs : layout.outputs;

        for (int i = 0; i < (int) ports.size(); ++i)
        {
            const auto centre = *getPortCentre (input != 0, i);
            const auto state = canvas.getPortState ({ id, input != 0, i });
            const auto portColour = theme::channelColour (ports[(size_t) i].channels);
            const auto hovered = state == PortState::hovered || (hoveredPort && *hoveredPort == PortRef { id, input != 0, i });

            auto r = hovered ? portRadius + 1.5f : portRadius;
            g.setColour (state == PortState::dimmed ? portColour.withAlpha (0.25f) : portColour);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
            g.setColour (theme::surface);
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.5f);

            if (state == PortState::available || state == PortState::refused)
            {
                g.setColour (state == PortState::available ? theme::text : theme::danger);
                g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (centre), 1.5f);
            }

            // Label: name, then how many channels.
            auto text = juce::Rectangle<float> (body.getX() + 12.0f, centre.y - row * 0.5f, body.getWidth() - 24.0f, row);
            const auto& port = ports[(size_t) i];
            const auto just = input != 0 ? juce::Justification::centredLeft : juce::Justification::centredRight;
            const auto nameWidth = juce::GlyphArrangement::getStringWidth (theme::font (11.0f, theme::Weight::medium), port.name);

            g.setFont (theme::font (11.0f, theme::Weight::medium));
            g.setColour (state == PortState::dimmed ? theme::textMuted.withAlpha (0.5f) : theme::text);
            g.drawText (port.name, text, just, true);

            g.setFont (theme::font (10.0f));
            g.setColour (theme::textMuted);
            if (input != 0)
                g.drawText (describeChannels (port.channels), text.withTrimmedLeft (nameWidth + 6.0f), just, true);
            else
                g.drawText (describeChannels (port.channels), text.withTrimmedRight (nameWidth + 6.0f), just, true);
        }
    }

    paintMeter (g, meterArea().toFloat());
}

void NodeComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && ! juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey))
    {
        if (auto port = portAt (e.position))
        {
            canvas.portMouseDown (*port, e.getEventRelativeTo (&canvas));
            return;
        }

        if (meterArea().expanded (2).contains (e.getPosition()))
            canvas.clearClips (id);
    }

    canvas.nodeMouseDown (*this, e.getEventRelativeTo (&canvas));
}

void NodeComponent::mouseDrag (const juce::MouseEvent& e)
{
    canvas.nodeMouseDrag (*this, e.getEventRelativeTo (&canvas));
}

void NodeComponent::mouseUp (const juce::MouseEvent& e)
{
    canvas.nodeMouseUp (*this, e.getEventRelativeTo (&canvas));
}

void NodeComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y < (float) headerHeight)
        startRename();
}

void NodeComponent::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    const auto port = portAt (e.position);

    if (port != hoveredPort)
    {
        hoveredPort = port;
        setMouseCursor (port ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        repaint (portsArea().expanded (margin, 4));
    }
}

void NodeComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoveredPort)
    {
        hoveredPort.reset();
        repaint (portsArea().expanded (margin, 4));
    }
}

juce::String NodeComponent::getTooltip()
{
    if (hoveredPort)
    {
        const auto& ports = hoveredPort->input ? layout.inputs : layout.outputs;
        const auto& port = ports[(size_t) hoveredPort->port];
        return juce::String (hoveredPort->input ? "Input" : "Output") + " \"" + port.name + "\", "
               + describeChannels (port.channels) + (hoveredPort->input ? ". Drag to move its wire." : ". Drag to connect.");
    }

    if (type != nullptr && lastMouse.y < (float) headerHeight)
        return juce::String (type->name) + ": " + type->description + " Double-click to rename.";

    return {};
}

void NodeComponent::startRename()
{
    if (renameEditor != nullptr)
        return;

    renameEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *renameEditor;
    ed.setFont (theme::font (13.0f, theme::Weight::semiBold));
    ed.setColour (juce::TextEditor::backgroundColourId, theme::background);
    ed.setColour (juce::TextEditor::outlineColourId, theme::accent);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    ed.setColour (juce::TextEditor::textColourId, theme::text);
    ed.setIndents (4, 3);
    ed.setText (name, false);
    ed.selectAll();
    addAndMakeVisible (ed);
    resized();
    ed.grabKeyboardFocus();

    auto finish = [this] (bool commit)
    {
        if (renameEditor == nullptr)
            return;

        const auto newName = renameEditor->getText().trim();

        // Delete the editor after its callback has returned.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr)
            {
                safe->renameEditor = nullptr;
                safe->repaint();
            }
        });

        renameEditor->onFocusLost = nullptr;
        renameEditor->onReturnKey = nullptr;
        renameEditor->onEscapeKey = nullptr;

        if (commit && newName.isNotEmpty() && newName != name)
        {
            canvas.getSession().beginAction ("Rename");
            canvas.getSession().setNodeName (id, newName);
        }

        canvas.grabKeyboardFocus();
    };

    ed.onReturnKey = [finish] { finish (true); };
    ed.onFocusLost = [finish] { finish (true); };
    ed.onEscapeKey = [finish] { finish (false); };
}

} // namespace spm::ui
