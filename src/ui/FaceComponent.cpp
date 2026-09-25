// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/FaceComponent.h"

#include "ui/DragSlider.h"
#include "ui/Icons.h"
#include "ui/ParamFormat.h"
#include "ui/Theme.h"

#include <algorithm>
#include <map>

namespace spm::ui
{

namespace
{

constexpr int rowHeight = 24, gap = 4, pad = 6;

bool isMainFader (std::string_view typeId, const nodes::ParamSpec& spec)
{
    return (typeId == nodes::types::fader || typeId == nodes::types::bus) && spec.id == "level";
}

juce::Colour toggleColour (const nodes::ParamSpec& spec)
{
    if (spec.id == "mute" || spec.id == "armed") return theme::danger;
    if (spec.id == "invert") return theme::warning;
    return theme::good;
}

juce::String shortName (const nodes::ParamSpec& spec)
{
    if (spec.id == "invert") return juce::String::fromUTF8 ("\xc3\x98");  // Ø
    return spec.name;
}

/** A small button that shows a menu of choices. */
class ChoiceButton final : public juce::Button
{
public:
    ChoiceButton() : juce::Button ({}) {}

    void setText (const juce::String& label, const juce::String& value)
    {
        if (label != labelText || value != valueText)
        {
            labelText = label;
            valueText = value;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        g.setColour ((highlighted || down) ? theme::surfaceHigh.brighter (0.08f) : theme::surfaceHigh);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (11.0f, theme::Weight::medium));
        g.drawFittedText (valueText, r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    juce::String labelText, valueText;
};

/** A latching button for a toggle parameter. */
class ToggleButton final : public juce::Button
{
public:
    explicit ToggleButton (const nodes::ParamSpec& s) : juce::Button (s.name), spec (s) {}

    juce::String text;

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
        g.drawFittedText (text, r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    const nodes::ParamSpec spec;
};

} // namespace

//==============================================================================
class FaceComponent::Knob final : public DragSlider
{
public:
    explicit Knob (const nodes::ParamSpec& s) : spec (s)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setScrollWheelEnabled (false);
        configureSlider (*this, spec);
    }

    juce::String label;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto labelArea = r.removeFromBottom (13.0f);
        auto valueArea = r.removeFromBottom (13.0f);

        const auto size = std::min (r.getWidth(), r.getHeight()) - 2.0f;
        const auto knob = juce::Rectangle<float> (size, size).withCentre (r.getCentre());
        const auto start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        const auto pos = (float) valueToProportionOfLength (getValue());
        const auto bipolar = spec.minValue < 0.0f && spec.maxValue > 0.0f && spec.unit != "dB";
        const auto origin = bipolar ? (float) valueToProportionOfLength (0.0) : 0.0f;
        const auto angle = [&] (float p) { return start + (end - start) * p; };
        const auto arcRadius = size * 0.5f - 2.0f;

        juce::Path track, value;
        track.addCentredArc (knob.getCentreX(), knob.getCentreY(), arcRadius, arcRadius, 0.0f, start, end, true);
        value.addCentredArc (knob.getCentreX(), knob.getCentreY(), arcRadius, arcRadius, 0.0f,
                             angle (std::min (pos, origin)), angle (std::max (pos, origin)), true);
        const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (theme::surfaceHigh);
        g.strokePath (track, stroke);
        g.setColour (isMouseOverOrDragging() ? theme::accent.brighter (0.2f) : theme::accent);
        g.strokePath (value, stroke);

        const auto a = angle (pos) - juce::MathConstants<float>::halfPi;
        const auto c = knob.getCentre();
        g.setColour (theme::text);
        g.drawLine ({ c + juce::Point<float> (std::cos (a), std::sin (a)) * arcRadius * 0.3f,
                      c + juce::Point<float> (std::cos (a), std::sin (a)) * (arcRadius - 3.0f) }, 2.0f);

        g.setFont (theme::font (10.5f, theme::Weight::medium, true));
        g.drawFittedText (getTextFromValue (getValue()), valueArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (10.0f));
        g.drawFittedText (label, labelArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    const nodes::ParamSpec spec;
};

class FaceComponent::Fader final : public DragSlider
{
public:
    explicit Fader (const nodes::ParamSpec& s) : spec (s)
    {
        setSliderStyle (juce::Slider::LinearVertical);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        setScrollWheelEnabled (false);
        configureSlider (*this, spec);
        setColour (juce::Slider::backgroundColourId, theme::background);
        setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.5f));
        setColour (juce::Slider::thumbColourId, theme::text);
    }

    const nodes::ParamSpec spec;
};

//==============================================================================
FaceComponent::FaceComponent (model::Session& s, const MeterCache& m, juce::ValueTree f) : session (s), meters (m), face (f)
{
    setRepaintsOnMouseActivity (false);
    update();
}

FaceComponent::~FaceComponent() = default;

graph::NodeId FaceComponent::getNode() const
{
    return (graph::NodeId) (juce::int64) face[model::ids::node];
}

model::FaceSize FaceComponent::getSize() const
{
    return (model::FaceSize) juce::jlimit (0, 2, (int) face.getProperty (model::ids::size, 1));
}

int FaceComponent::widthFor (model::FaceSize size)
{
    switch (size)
    {
        case model::FaceSize::compact: return 64;
        case model::FaceSize::large:   return 110;
        case model::FaceSize::standard: break;
    }
    return 84;
}

int FaceComponent::knobHeight() const
{
    return widthFor (getSize()) - 2 * pad - 14 + 26;
}

int FaceComponent::faderHeight() const
{
    switch (getSize())
    {
        case model::FaceSize::compact: return 130;
        case model::FaceSize::large:   return 240;
        case model::FaceSize::standard: break;
    }
    return 180;
}

void FaceComponent::collect (graph::NodeId node, std::vector<Item>& out, int depth) const
{
    const auto* type = session.getNodeType (node);
    if (type == nullptr || depth > 16)
        return;

    if (type->id == nodes::types::group)
    {
        auto children = session.getChildren (node);
        std::vector<std::pair<juce::Point<float>, graph::NodeId>> sorted;
        for (auto child : children)
            if (const auto* t = session.getNodeType (child); t != nullptr && ! nodes::isGroupPin (t->id))
                sorted.push_back ({ session.getNodePosition (child), child });
        std::sort (sorted.begin(), sorted.end(), [] (auto& a, auto& b)
        {
            return std::tie (a.first.x, a.first.y, a.second) < std::tie (b.first.x, b.first.y, b.second);
        });
        for (auto& [pos, child] : sorted)
            collect (child, out, depth + 1);
        return;
    }

    const auto isHardware = type->id == nodes::types::hardwareInput || type->id == nodes::types::hardwareOutput;

    for (int i = 0; i < (int) type->params.size(); ++i)
    {
        const auto& spec = type->params[(size_t) i];
        if (spec.structural || ! spec.onFace || (isHardware && spec.id == "first"))
            continue;
        out.push_back ({ node, i });
    }
}

std::vector<FaceComponent::Item> FaceComponent::collectItems() const
{
    std::vector<Item> result;
    collect (getNode(), result, 0);
    return result;
}

const nodes::ParamSpec* FaceComponent::specOf (const Item& item) const
{
    const auto* type = session.getNodeType (item.node);
    if (type == nullptr || item.param < 0 || item.param >= (int) type->params.size())
        return nullptr;
    return &type->params[(size_t) item.param];
}

void FaceComponent::setParam (const Item& item, float value, bool newGesture)
{
    if (newGesture)
        if (const auto* spec = specOf (item))
            session.beginAction ("Change " + juce::String (spec->name));
    session.setParam (item.node, item.param, value);
}

void FaceComponent::rebuild (const std::vector<Item>& newItems)
{
    items = newItems;
    controls.clear();
    fader = nullptr;

    // The last level fader becomes the big fader at the bottom.
    int faderIndex = -1;
    for (int i = (int) items.size(); --i >= 0;)
        if (const auto* type = session.getNodeType (items[(size_t) i].node))
            if (isMainFader (type->id, type->params[(size_t) items[(size_t) i].param]))
            {
                faderIndex = i;
                break;
            }

    // Labels: the parameter name, with the node's name where two would look the same.
    std::map<juce::String, int> nameCounts;
    for (auto& item : items)
        if (const auto* spec = specOf (item))
            ++nameCounts[shortName (*spec)];

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto item = items[(size_t) i];
        const auto* specPtr = specOf (item);
        if (specPtr == nullptr)
            continue;

        const auto& spec = *specPtr;
        auto label = shortName (spec);
        if (nameCounts[label] > 1 && item.node != getNode())
            label = session.getNodeName (item.node) + " " + label;
        const auto tooltip = session.getNodeName (item.node) + ": " + juce::String (spec.name)
                             + (spec.tooltip.empty() ? juce::String() : " - " + juce::String (spec.tooltip));

        Control control { item, nullptr, label };

        if (i == faderIndex)
        {
            auto f = std::make_unique<Fader> (spec);
            auto* s = f.get();
            s->onDragStart = [this, s] { session.beginAction ("Change " + juce::String (s->spec.name)); };
            s->onValueChange = [this, s, item] { setParam (item, (float) s->getValue(), ! s->isMouseButtonDown()); };
            s->setTooltip (tooltip + ". Drag to change (Shift for fine); double-click to reset.");
            control.component = std::move (f);
        }
        else switch (spec.kind)
        {
            case nodes::ParamKind::toggle:
            {
                auto t = std::make_unique<ToggleButton> (spec);
                auto* b = t.get();
                b->text = label;
                b->onClick = [this, b, item] { setParam (item, b->getToggleState() ? 0.0f : 1.0f, true); };
                b->setTooltip (tooltip);
                control.component = std::move (t);
                break;
            }

            case nodes::ParamKind::choice:
            {
                auto c = std::make_unique<ChoiceButton>();
                auto* b = c.get();
                b->onClick = [this, b, item, spec]
                {
                    juce::PopupMenu menu;
                    const auto current = juce::roundToInt (session.getParam (item.node, item.param));
                    for (int k = 0; k < (int) spec.choices.size(); ++k)
                        menu.addItem (k + 1, spec.choices[(size_t) k], true, k == current);
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b),
                                        [this, safe = juce::Component::SafePointer<FaceComponent> (this), item] (int result)
                                        {
                                            if (safe != nullptr && result != 0)
                                                setParam (item, (float) (result - 1), true);
                                        });
                };
                b->setTooltip (tooltip);
                control.component = std::move (c);
                break;
            }

            case nodes::ParamKind::continuous:
            case nodes::ParamKind::integer:
            {
                auto k = std::make_unique<Knob> (spec);
                auto* s = k.get();
                s->label = label;
                s->onDragStart = [this, s] { session.beginAction ("Change " + juce::String (s->spec.name)); };
                s->onValueChange = [this, s, item] { setParam (item, (float) s->getValue(), ! s->isMouseButtonDown()); };
                s->setTooltip (tooltip + ". Drag up or down to change (Shift for fine); double-click to reset.");
                control.component = std::move (k);
                break;
            }
        }

        addAndMakeVisible (*control.component);
        controls.push_back (std::move (control));
    }

    for (auto& c : controls)
        if (dynamic_cast<Fader*> (c.component.get()) != nullptr)
            fader = &c;

    // Height: header, controls, then the fader/meter section.
    auto height = headerHeight + pad;
    for (auto& c : controls)
    {
        if (&c == fader)
            continue;
        height += (dynamic_cast<Knob*> (c.component.get()) != nullptr ? knobHeight() : rowHeight) + gap;
    }

    if (fader != nullptr || meterPort().has_value())
        height += (fader != nullptr ? faderHeight() : faderHeight() / 2) + 18;

    setSize (widthFor (getSize()), height + pad);
    resized();
    lastMeterStep = std::numeric_limits<int>::min();
}

void FaceComponent::update()
{
    if (! session.findNode (getNode()).isValid())
        return;

    // Labels that include node names can change with a rename, so that rebuilds too.
    const auto newItems = collectItems();
    const auto newName = session.getNodeName (getNode());
    if (! built || newItems != items || newName != name || getWidth() != widthFor (getSize()))
    {
        built = true;
        name = newName;
        rebuild (newItems);
        repaint();
    }

    for (auto& c : controls)
    {
        const auto value = session.getParam (c.item.node, c.item.param);
        const auto* spec = specOf (c.item);

        if (auto* slider = dynamic_cast<juce::Slider*> (c.component.get()))
        {
            if (! slider->isMouseButtonDown())
                slider->setValue (value, juce::dontSendNotification);
        }
        else if (auto* toggle = dynamic_cast<ToggleButton*> (c.component.get()))
        {
            toggle->setToggleState (value >= 0.5f, juce::dontSendNotification);
        }
        else if (auto* choice = dynamic_cast<ChoiceButton*> (c.component.get()); choice != nullptr && spec != nullptr)
        {
            choice->setText (c.label, formatParam (*spec, value));
        }
    }

    if (fader != nullptr)
        repaint (0, getHeight() - pad - 16, getWidth(), 16);
}

std::optional<std::pair<bool, int>> FaceComponent::meterPort() const
{
    const auto layout = session.getLayout (getNode());
    if (layout.outputs.size() == 1 || (layout.inputs.empty() && ! layout.outputs.empty()))
        return std::pair { false, 0 };
    if (! layout.inputs.empty())
        return std::pair { true, 0 };
    return {};
}

const PortLevels* FaceComponent::meterLevels() const
{
    const auto port = meterPort();
    return port ? meters.get (getNode(), port->first, port->second) : nullptr;
}

juce::Rectangle<int> FaceComponent::meterArea() const
{
    auto area = getLocalBounds().reduced (pad).withTrimmedBottom (18);
    area = area.removeFromBottom (fader != nullptr ? faderHeight() : faderHeight() / 2);
    if (fader != nullptr)
        return area.removeFromRight (area.getWidth() / 2 - 4).withTrimmedLeft (4).withWidth (std::min (16, area.getWidth() / 2 - 8));
    return area.withSizeKeepingCentre (16, area.getHeight());
}

void FaceComponent::updateMeter()
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

void FaceComponent::resized()
{
    auto area = getLocalBounds().reduced (pad).withTrimmedTop (headerHeight);

    for (auto& c : controls)
    {
        if (&c == fader)
            continue;
        const auto h = dynamic_cast<Knob*> (c.component.get()) != nullptr ? knobHeight() : rowHeight;
        c.component->setBounds (area.removeFromTop (h));
        area.removeFromTop (gap);
    }

    if (fader != nullptr)
    {
        auto section = getLocalBounds().reduced (pad).withTrimmedBottom (18).removeFromBottom (faderHeight());
        fader->component->setBounds (section.removeFromLeft (section.getWidth() / 2 + 4));
    }
}

void FaceComponent::paint (juce::Graphics& g)
{
    const auto body = getLocalBounds().toFloat();
    const auto* type = session.getNodeType (getNode());
    const auto colour = theme::categoryColour (type != nullptr ? juce::String (type->category) : juce::String ("Unknown"));

    g.setColour (theme::surface);
    g.fillRoundedRectangle (body, theme::radius);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (body, theme::radius);
    g.reduceClipRegion (clip);
    g.setColour (colour);
    g.fillRect (body.withHeight (3.0f));
    g.setColour (theme::surfaceHigh.withAlpha (0.5f));
    g.fillRect (body.withTop (3.0f).withHeight ((float) headerHeight - 3.0f));
    g.restoreState();

    g.setColour (theme::text);
    g.setFont (theme::font (12.0f, theme::Weight::semiBold));
    g.drawFittedText (name, juce::Rectangle<int> (4, 3, getWidth() - 8, headerHeight - 3), juce::Justification::centred, 1, 0.75f);

    if (controls.empty() && ! meterPort())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawFittedText ("No controls", getLocalBounds().withTrimmedTop (headerHeight), juce::Justification::centred, 2);
    }

    // Meter: one bar per channel (up to 8), green → amber → red.
    if (meterPort())
    {
        const auto area = meterArea().toFloat();
        const auto* levels = meterLevels();
        const auto channels = levels != nullptr ? juce::jlimit (1, 8, (int) levels->peakDb.size()) : 1;
        auto bars = area.withTrimmedTop (8.0f);
        auto clipLight = area.withHeight (5.0f);

        const auto toY = [&bars] (float db)
        {
            return bars.getBottom() - bars.getHeight() * juce::jlimit (0.0f, 1.0f, (db - MeterCache::floorDb) / -MeterCache::floorDb);
        };

        juce::ColourGradient gradient (theme::good, 0.0f, toY (-60.0f), theme::danger, 0.0f, toY (0.0f), false);
        const auto at = [&] (float db) { return (bars.getBottom() - toY (db)) / bars.getHeight(); };
        gradient.addColour (at (-18.0f), theme::good);
        gradient.addColour (at (-12.0f), theme::warning);
        gradient.addColour (at (-6.0f), theme::warning);
        gradient.addColour (at (-3.0f), theme::danger);

        const auto w = (bars.getWidth() - (float) (channels - 1)) / (float) channels;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto bar = juce::Rectangle<float> (bars.getX() + (float) ch * (w + 1.0f), bars.getY(), w, bars.getHeight());
            g.setColour (theme::background);
            g.fillRect (bar);
            if (levels == nullptr || ch >= (int) levels->peakDb.size())
                continue;
            g.setGradientFill (gradient);
            g.setOpacity (0.45f);
            g.fillRect (bar.withTop (toY (levels->peakDb[(size_t) ch])));
            g.setOpacity (1.0f);
            g.fillRect (bar.withTop (toY (levels->rmsDb[(size_t) ch])));
        }

        g.setColour (theme::text.withAlpha (0.25f));
        g.fillRect (bars.getX(), toY (-18.0f), bars.getWidth(), 1.0f);
        g.setColour (levels != nullptr && levels->clipped ? theme::danger : theme::surfaceHigh);
        g.fillRoundedRectangle (clipLight, 1.5f);
    }

    // The fader's value, under it.
    if (fader != nullptr)
        if (const auto* spec = specOf (fader->item))
        {
            g.setColour (theme::text);
            g.setFont (theme::font (11.0f, theme::Weight::medium, true));
            g.drawFittedText (formatParam (*spec, session.getParam (fader->item.node, fader->item.param)),
                              juce::Rectangle<int> (pad, getHeight() - pad - 16, getWidth() - 2 * pad, 16),
                              juce::Justification::centred, 1, 0.8f);
        }
}

void FaceComponent::mouseDown (const juce::MouseEvent& e)
{
    if (onHeaderDown)
        onHeaderDown (*this, e);
}

void FaceComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (onHeaderDrag)
        onHeaderDrag (*this, e);
}

void FaceComponent::mouseUp (const juce::MouseEvent& e)
{
    if (onHeaderUp)
        onHeaderUp (*this, e);
}

} // namespace spm::ui
