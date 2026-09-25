// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace spm::diag
{

/** Console output that is also captured into a report file. */
class Console
{
public:
    explicit Console (bool isInteractiveMode) : interactive (isInteractiveMode) {}

    bool isInteractive() const noexcept { return interactive; }

    /** Prints a line and records it in the report. */
    void line (const juce::String& text = {});

    /** Prints a section heading. */
    void heading (const juce::String& text);

    /** Overwrites the current console line (live status). Not recorded in the report. */
    void status (const juce::String& text);

    /** Ends a run of status() updates so the next output starts on a fresh line. */
    void endStatus();

    /** Asks a question. In non-interactive mode returns defaultAnswer without waiting. */
    juce::String ask (const juce::String& prompt, const juce::String& defaultAnswer = {});

    bool confirm (const juce::String& prompt, bool defaultAnswer);

    const juce::String& getReportText() const noexcept { return report; }

    bool saveReport (const juce::File& file) const;

private:
    bool interactive;
    bool statusActive = false;
    int lastStatusLength = 0;
    juce::String report;
};

} // namespace spm::diag
