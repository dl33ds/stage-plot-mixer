// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/QuickAddPanel.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

namespace spm::ui
{

namespace
{
constexpr int searchHeight = 34, titleHeight = 26, maxListHeight = 340;
const char* const categoryOrder[] = { "Sources", "Mixing", "Processing", "Routing", "Analysis", "Destinations", "Groups", "Templates" };
} // namespace

std::vector<QuickAddPanel::Entry> QuickAddPanel::nodeEntries (const Filter& filter)
{
    std::vector<Entry> result;
    for (auto& type : nodes::NodeRegistry::builtIn().all())
        if (filter == nullptr || filter (type))
            result.push_back ({ type.id, type.name, type.category, type.icon, type.description });
    return result;
}

QuickAddPanel::QuickAddPanel (juce::String t, std::vector<Entry> e, std::function<void (const std::string&)> chosen,
                              std::function<void()> dismissed)
    : title (std::move (t)), entries (std::move (e)), onChosen (std::move (chosen)), onDismissed (std::move (dismissed))
{
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (false);

    search.setTextToShowWhenEmpty ("Search nodes...", theme::textMuted);
    search.setFont (theme::font (13.0f));
    search.setIndents (28, 8);
    search.setColour (juce::TextEditor::backgroundColourId, theme::background);
    search.setColour (juce::TextEditor::outlineColourId, theme::border);
    search.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    search.setColour (juce::TextEditor::textColourId, theme::text);
    search.addKeyListener (this);
    search.onTextChange = [this] { refilter(); };
    search.onFocusLost = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr && ! safe->search.hasKeyboardFocus (true))
                safe->dismiss();
        });
    };
    addAndMakeVisible (search);

    refilter();
}

QuickAddPanel::~QuickAddPanel()
{
    search.removeKeyListener (this);
}

void QuickAddPanel::refilter()
{
    rows.clear();
    const auto query = search.getText().trim();

    std::vector<const Entry*> candidates;
    for (auto& entry : entries)
        candidates.push_back (&entry);

    if (query.isEmpty())
    {
        for (auto* category : categoryOrder)
        {
            auto first = true;
            for (auto* type : candidates)
            {
                if (type->category != category)
                    continue;
                if (first)
                    rows.push_back ({ nullptr, category });
                first = false;
                rows.push_back ({ type, {} });
            }
        }
    }
    else
    {
        // Names starting with the query first, then names containing it, then descriptions.
        std::vector<std::pair<int, const Entry*>> scored;
        for (auto* type : candidates)
        {
            const auto& name = type->name;
            auto score = -1;
            if (name.startsWithIgnoreCase (query)) score = 0;
            else if (name.containsIgnoreCase (query)) score = 1;
            else if (type->category.containsIgnoreCase (query)) score = 2;
            else if (type->description.containsIgnoreCase (query)) score = 3;
            if (score >= 0)
                scored.push_back ({ score, type });
        }
        std::stable_sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        for (auto& [score, type] : scored)
            rows.push_back ({ type, {} });
    }

    highlighted = -1;
    moveHighlight (1);
    scroll = 0;
    repaint();
}

int QuickAddPanel::getIdealHeight() const
{
    auto h = 0;
    for (int i = 0; i < (int) rows.size(); ++i)
        h += rowHeight (i);
    return titleHeight + searchHeight + 12 + juce::jlimit (32, maxListHeight, h) + 6;
}

juce::Rectangle<int> QuickAddPanel::listArea() const
{
    return getLocalBounds().withTrimmedTop (titleHeight + searchHeight + 12).reduced (6, 0).withTrimmedBottom (6);
}

void QuickAddPanel::resized()
{
    search.setBounds (getLocalBounds().withTrimmedTop (titleHeight).removeFromTop (searchHeight + 6).reduced (8, 3));
}

int QuickAddPanel::rowY (int index) const
{
    auto y = listArea().getY() - scroll;
    for (int i = 0; i < index; ++i)
        y += rowHeight (i);
    return y;
}

int QuickAddPanel::rowAt (juce::Point<int> p) const
{
    if (! listArea().contains (p))
        return -1;

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto y = rowY (i);
        if (p.y >= y && p.y < y + rowHeight (i))
            return rows[(size_t) i].type != nullptr ? i : -1;
    }
    return -1;
}

void QuickAddPanel::moveHighlight (int delta)
{
    if (rows.empty())
    {
        highlighted = -1;
        return;
    }

    auto i = highlighted;
    for (int tries = 0; tries < (int) rows.size(); ++tries)
    {
        i = juce::jlimit (0, (int) rows.size() - 1, i + delta);
        if (rows[(size_t) i].type != nullptr)
            break;
        if (i == 0 || i == (int) rows.size() - 1)
            delta = -delta;
    }

    if (i >= 0 && rows[(size_t) i].type != nullptr)
        highlighted = i;

    // Keep it in view.
    const auto list = listArea();
    const auto top = rowY (highlighted) + scroll - list.getY();
    if (top < scroll)
        scroll = std::max (0, top - (highlighted > 0 && rows[(size_t) highlighted - 1].type == nullptr ? 22 : 0));
    else if (top + rowHeight (highlighted) > scroll + list.getHeight())
        scroll = top + rowHeight (highlighted) - list.getHeight();

    repaint();
}

void QuickAddPanel::choose (int index)
{
    if (finished || index < 0 || index >= (int) rows.size() || rows[(size_t) index].type == nullptr)
        return;

    finished = true;
    const auto typeId = rows[(size_t) index].type->id;
    auto callback = onChosen;
    callback (typeId);  // may delete this
}

void QuickAddPanel::dismiss()
{
    if (finished)
        return;

    finished = true;
    auto callback = onDismissed;
    callback();  // may delete this
}

bool QuickAddPanel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::upKey)        { moveHighlight (-1); return true; }
    if (key == juce::KeyPress::downKey)      { moveHighlight (1); return true; }
    if (key == juce::KeyPress::returnKey)    { choose (highlighted); return true; }
    if (key == juce::KeyPress::escapeKey)    { dismiss(); return true; }
    if (key == juce::KeyPress::tabKey)       { moveHighlight (key.getModifiers().isShiftDown() ? -1 : 1); return true; }
    return false;
}

void QuickAddPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto row = rowAt (e.getPosition());
    if (row >= 0 && row != highlighted)
    {
        highlighted = row;
        repaint();
    }
}

void QuickAddPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto row = rowAt (e.getPosition());
    if (row >= 0)
        choose (row);
}

void QuickAddPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    auto total = 0;
    for (int i = 0; i < (int) rows.size(); ++i)
        total += rowHeight (i);

    scroll = juce::jlimit (0, std::max (0, total - listArea().getHeight()), scroll - juce::roundToInt (wheel.deltaY * 200.0f));
    repaint();
}

void QuickAddPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 3.0f), theme::radius + 2.0f);
    g.setColour (theme::surface);
    g.fillRoundedRectangle (bounds.reduced (0.5f), theme::radius);
    g.setColour (theme::border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::radius, 1.0f);

    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.0f, theme::Weight::semiBold));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (titleHeight).reduced (12, 0).withTrimmedTop (6),
                juce::Justification::centredLeft, true);

    const auto searchBounds = search.getBounds().toFloat();
    drawIcon (g, "search", { searchBounds.getX() + 8.0f, searchBounds.getCentreY() - 7.0f, 14.0f, 14.0f }, theme::textMuted);

    const auto list = listArea();
    g.reduceClipRegion (list);

    if (rows.empty())
    {
        g.setFont (theme::font (12.0f));
        g.drawText ("No matching nodes", list.withHeight (32), juce::Justification::centred, false);
        return;
    }

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto r = juce::Rectangle<int> (list.getX(), rowY (i), list.getWidth(), rowHeight (i));
        if (r.getBottom() < list.getY() || r.getY() > list.getBottom())
            continue;

        const auto& row = rows[(size_t) i];

        if (row.type == nullptr)
        {
            g.setColour (theme::categoryColour (row.heading));
            g.setFont (theme::font (10.5f, theme::Weight::semiBold));
            g.drawText (row.heading.toUpperCase(), r.reduced (8, 0).withTrimmedTop (6), juce::Justification::centredLeft, false);
            continue;
        }

        if (i == highlighted)
        {
            g.setColour (theme::accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
        }

        const auto colour = theme::categoryColour (row.type->category);
        drawIcon (g, row.type->icon, r.removeFromLeft (30).toFloat().withSizeKeepingCentre (15.0f, 15.0f), colour);

        auto top = r.removeFromTop (r.getHeight() / 2 + 2);
        g.setColour (theme::text);
        g.setFont (theme::font (12.5f, theme::Weight::medium));
        g.drawText (row.type->name, top.withTrimmedTop (2), juce::Justification::bottomLeft, true);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (10.5f));
        g.drawText (row.type->description, r.withTrimmedRight (6), juce::Justification::topLeft, true);
    }
}

} // namespace spm::ui
