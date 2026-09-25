// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/Dialogs.h"

namespace spm::ui
{

void askForText (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& initial,
                 const juce::String& okText, std::function<void (const juce::String&)> onDone)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, parent);
    window->addTextEditor ("text", initial);
    window->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* editor = window->getTextEditor ("text"))
        editor->selectAll();

    window->enterModalState (true, juce::ModalCallbackFunction::create (
                                       [safe = juce::Component::SafePointer<juce::AlertWindow> (window), onDone] (int result)
                                       {
                                           if (result != 1 || safe == nullptr)
                                               return;

                                           const auto text = safe->getTextEditorContents ("text").trim();
                                           if (text.isNotEmpty())
                                               juce::MessageManager::callAsync ([onDone, text] { onDone (text); });
                                       }),
                             true);
}

} // namespace spm::ui
