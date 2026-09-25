// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "nodes/NodeTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui
{

/** "Add node" search: type to filter, arrows to move, Enter to add, Escape to close. */
class QuickAddPanel final : public juce::Component, private juce::KeyListener
{
public:
    using Filter = std::function<bool (const nodes::NodeType&)>;

    /** Something that can be added: a node type, or a template. */
    struct Entry
    {
        std::string id;  // a node type id, or anything the caller recognises
        juce::String name, category, icon, description;
    };

    /** The built-in node types that pass the filter. */
    static std::vector<Entry> nodeEntries (const Filter& filter);

    QuickAddPanel (juce::String title, std::vector<Entry> entries, std::function<void (const std::string& id)> onChosen,
                   std::function<void()> onDismissed);
    ~QuickAddPanel() override;

    void focusSearch() { search.grabKeyboardFocus(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int panelWidth = 280;
    int getIdealHeight() const;

private:
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    struct Row
    {
        const Entry* type = nullptr;  // nullptr: a category heading
        juce::String heading;
    };

    void refilter();
    int rowAt (juce::Point<int>) const;
    int rowY (int index) const;
    int rowHeight (int index) const { return rows[(size_t) index].type != nullptr ? 32 : 22; }
    void moveHighlight (int delta);
    void choose (int index);
    void dismiss();
    juce::Rectangle<int> listArea() const;

    juce::String title;
    std::vector<Entry> entries;
    std::function<void (const std::string&)> onChosen;
    std::function<void()> onDismissed;
    juce::TextEditor search;
    std::vector<Row> rows;
    int highlighted = -1, scroll = 0;
    bool finished = false;
};

} // namespace spm::ui
