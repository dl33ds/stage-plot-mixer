// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/StatusBar.h"

#include "ui/Theme.h"

namespace spm::app
{

namespace
{
double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
}

StatusBar::StatusBar (AudioEngine& e) : engine (e)
{
    addAndMakeVisible (resetButton);
    addAndMakeVisible (reportButton);

    resetButton.onClick = [this]
    {
        engine.resetCounters();
        countersSince = nowSeconds();
        peakCpu = 0.0f;
        timerCallback();
    };

    reportButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (makeReport());
        reportButton.setIcon ("circle-dot");
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr)
                safe->reportButton.setIcon ("copy");
        });
    };

    countersSince = nowSeconds();
    startTimerHz (4);
    timerCallback();
}

void StatusBar::timerCallback()
{
    const auto st = engine.getStatus();
    peakCpu = std::max (peakCpu, st.stats.peakCpuLoad);

    device = st.deviceName.isEmpty() ? "No audio device - choose one in Audio settings"
                                     : st.deviceName + "  (" + st.typeName + ")" + (st.running ? "" : "  - stopped");

    const auto ms = st.sampleRate > 0 ? 1000.0 * st.bufferSize / st.sampleRate : 0.0;
    format = juce::String (st.sampleRate / 1000.0, 1) + " kHz  " + juce::String (st.bufferSize) + " samples ("
             + juce::String (ms, 1) + " ms)  " + juce::String (st.numInputs) + " in / " + juce::String (st.numOutputs) + " out";

    cpu = "CPU " + juce::String (juce::roundToInt (st.stats.cpuLoad * 100.0f)) + "% (peak "
          + juce::String (juce::roundToInt (peakCpu * 100.0f)) + "%)";

    counters = "Late " + juce::String (st.stats.lateCallbacks) + "  Overloads " + juce::String (st.stats.overloads);
    if (st.driverXruns >= 0)
        counters << "  Driver dropouts " << st.driverXruns;
    counters << "  (" << juce::String ((nowSeconds() - countersSince) / 60.0, 1) << " min)";

    problems = st.stats.lateCallbacks + st.stats.overloads + std::max (0, st.driverXruns) > 0 || ! st.running;
    repaint();
}

juce::String StatusBar::makeReport()
{
    const auto st = engine.getStatus();
    const auto minutes = (nowSeconds() - countersSince) / 60.0;

    juce::String r;
    r << "Stage Plot Mixer " << SPM_VERSION << "  [" << SPM_GIT_HASH << "]\n"
      << "Date:        " << juce::Time::getCurrentTime().toString (true, true) << "\n"
      << "OS:          " << juce::SystemStats::getOperatingSystemName() << "\n"
      << "Device:      " << st.deviceName << " (" << st.typeName << ")" << (st.running ? "" : " - STOPPED") << "\n"
      << "Format:      " << st.sampleRate << " Hz, " << st.bufferSize << " samples, " << st.numInputs << " in / "
      << st.numOutputs << " out\n"
      << "Latency:     in " << st.inputLatency << ", out " << st.outputLatency << " samples (driver-reported)\n"
      << "Duration:    " << juce::String (minutes, 1) << " minutes since counters were reset\n"
      << "Callbacks:   " << st.stats.callbacks << "\n"
      << "Late:        " << st.stats.lateCallbacks << "\n"
      << "Overloads:   " << st.stats.overloads << "\n"
      << "Driver dropouts: " << (st.driverXruns >= 0 ? juce::String (st.driverXruns) : juce::String ("not reported")) << "\n"
      << "CPU:         " << juce::roundToInt (st.stats.cpuLoad * 100.0f) << "% now, " << juce::roundToInt (peakCpu * 100.0f)
      << "% peak\n";

    if (getReportExtras)
        r << getReportExtras();

    const auto count = st.stats.lateCallbacks + st.stats.overloads + std::max (0, st.driverXruns);
    r << "Verdict:     " << (! st.running ? "FAIL (device stopped)" : count > 0 ? "CHECK (see counts above)" : "PASS") << "\n";
    return r;
}

void StatusBar::paint (juce::Graphics& g)
{
    g.fillAll (ui::theme::surface);
    g.setColour (ui::theme::border);
    g.fillRect (0, 0, getWidth(), 1);

    auto area = getLocalBounds().reduced (12, 0).withRight (resetButton.getX() - 12);
    const auto f = ui::theme::font (11.5f, ui::theme::Weight::regular, true);
    g.setFont (f);

    auto draw = [&] (const juce::String& text, juce::Colour colour)
    {
        const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, text)) + 24;
        g.setColour (colour);
        g.drawText (text, area.removeFromLeft (w), juce::Justification::centredLeft, true);
    };

    draw (device, ui::theme::text);
    draw (format, ui::theme::textMuted);
    draw (cpu, ui::theme::textMuted);
    draw (counters, problems ? ui::theme::warning : ui::theme::textMuted);
}

void StatusBar::resized()
{
    auto area = getLocalBounds().reduced (8, 3);
    reportButton.setBounds (area.removeFromRight (reportButton.getIdealWidth (area.getHeight())));
    area.removeFromRight (4);
    resetButton.setBounds (area.removeFromRight (resetButton.getIdealWidth (area.getHeight())));
}

} // namespace spm::app
