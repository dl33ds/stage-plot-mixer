// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Console.h"

#include <iostream>
#include <string>

namespace spm::diag
{

void Console::line (const juce::String& text)
{
    endStatus();
    std::cout << text.toStdString() << std::endl;
    report << text << "\n";
}

void Console::heading (const juce::String& text)
{
    line();
    line ("== " + text + " " + juce::String::repeatedString ("=", juce::jmax (3, 72 - text.length())));
}

void Console::status (const juce::String& text)
{
    auto padded = text;
    if (padded.length() < lastStatusLength)
        padded += juce::String::repeatedString (" ", lastStatusLength - padded.length());

    std::cout << '\r' << padded.toStdString() << std::flush;
    lastStatusLength = text.length();
    statusActive = true;
}

void Console::endStatus()
{
    if (statusActive)
    {
        std::cout << std::endl;
        statusActive = false;
        lastStatusLength = 0;
    }
}

juce::String Console::ask (const juce::String& prompt, const juce::String& defaultAnswer)
{
    endStatus();

    const auto fullPrompt = prompt + (defaultAnswer.isNotEmpty() ? " [" + defaultAnswer + "]" : juce::String()) + ": ";

    if (! interactive)
    {
        report << fullPrompt << defaultAnswer << " (default)" << "\n";
        return defaultAnswer;
    }

    std::cout << fullPrompt.toStdString() << std::flush;

    std::string input;
    if (! std::getline (std::cin, input))
        input.clear();

    auto answer = juce::String (input).trim();
    if (answer.isEmpty())
        answer = defaultAnswer;

    report << fullPrompt << answer << "\n";
    return answer;
}

bool Console::confirm (const juce::String& prompt, bool defaultAnswer)
{
    const auto answer = ask (prompt + " (y/n)", defaultAnswer ? "y" : "n").toLowerCase();
    return answer.startsWith ("y");
}

bool Console::saveReport (const juce::File& file) const
{
    if (! file.getParentDirectory().createDirectory())
        return false;

    return file.replaceWithText (report, false, false, "\r\n");
}

} // namespace spm::diag
