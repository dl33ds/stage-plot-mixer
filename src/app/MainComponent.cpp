// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/MainComponent.h"

#include "ui/Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace spm::app
{

namespace
{

namespace types = nodes::types;

// Fixed node ids for the test graph.
constexpr graph::NodeId inputBase = 1000, panBase = 2000;
constexpr graph::NodeId busId = 10, faderId = 11, mainOutId = 12, toneId = 13;

graph::NodeDesc makeNode (graph::NodeId id, std::string_view type, nodes::ParamValues params)
{
    const auto* t = nodes::NodeRegistry::builtIn().find (type);
    auto values = t->defaults();
    for (size_t i = 0; i < params.size() && i < values.size(); ++i)
        values[i] = params[i];
    return { id, std::string (type), values };
}

void styleLabel (juce::Label& label, float size, juce::Colour colour)
{
    label.setFont (juce::FontOptions (size));
    label.setColour (juce::Label::textColourId, colour);
}

} // namespace

MainComponent::MainComponent (AudioEngine& e) : engine (e)
{
    for (auto* label : { &title, &inputsHeading, &outputsHeading, &monitorLevelLabel,
                         &statusDevice, &statusFormat, &statusCpu, &statusCounters })
        addAndMakeVisible (*label);

    title.setText ("Stage Plot Mixer - engine test", juce::dontSendNotification);
    styleLabel (title, 20.0f, ui::theme::text);
    inputsHeading.setText ("Inputs", juce::dontSendNotification);
    outputsHeading.setText ("Outputs", juce::dontSendNotification);
    styleLabel (inputsHeading, 14.0f, ui::theme::textMuted);
    styleLabel (outputsHeading, 14.0f, ui::theme::textMuted);

    for (auto* label : { &statusDevice, &statusFormat, &statusCpu, &statusCounters })
        styleLabel (*label, 13.0f, ui::theme::textMuted);

    addAndMakeVisible (monitorToggle);
    addAndMakeVisible (toneToggle);
    monitorToggle.setToggleState (true, juce::dontSendNotification);
    monitorToggle.onClick = toneToggle.onClick = [this] { pushParameters(); };

    addAndMakeVisible (monitorLevel);
    monitorLevel.setRange (-60.0, 10.0, 0.5);
    monitorLevel.setValue (-12.0, juce::dontSendNotification);
    monitorLevel.setTextValueSuffix (" dB");
    monitorLevel.setSliderStyle (juce::Slider::LinearHorizontal);
    monitorLevel.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    monitorLevel.onValueChange = [this] { pushParameters(); };
    monitorLevelLabel.setText ("Monitor level", juce::dontSendNotification);
    styleLabel (monitorLevelLabel, 14.0f, ui::theme::text);

    for (auto* button : { &settingsButton, &resetButton, &reportButton, &muteButton })
        addAndMakeVisible (*button);

    settingsButton.onClick = [this] { showAudioSettings(); };
    resetButton.onClick = [this]
    {
        engine.resetCounters();
        countersSince = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        peakCpu = 0.0f;
    };
    reportButton.onClick = [this] { copyReport(); };
    muteButton.onClick = [this] { engine.getCore().setOutputsMuted (! engine.getCore().areOutputsMuted()); };

    engine.onDeviceChanged = [this] { rebuildGraph(); };

    rebuildGraph();
    countersSince = lastTick = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz (30);
    setSize (1100, 640);
}

MainComponent::~MainComponent()
{
    engine.onDeviceChanged = nullptr;
}

void MainComponent::rebuildGraph()
{
    const auto status = engine.getStatus();
    numInputs = std::min (status.numInputs, 64);
    numOutputs = std::min (status.numOutputs, 64);

    // Every input → its own meter strip and a centred pan into a stereo bus; the bus goes
    // through a fader to outputs 1-2. A test tone can be added to the bus.
    graph::GraphDesc d;
    graph::WireId wire = 1;

    d.nodes.push_back (makeNode (busId, types::bus, { 0, 0, 2 }));
    d.nodes.push_back (makeNode (faderId, types::fader, { (float) monitorLevel.getValue(), monitorToggle.getToggleState() ? 0.0f : 1.0f, 2 }));
    d.nodes.push_back (makeNode (toneId, types::testGenerator, { 0, 1000, -18, toneToggle.getToggleState() ? 1.0f : 0.0f, 1 }));
    d.wires.push_back ({ wire++, busId, 0, faderId, 0, 1.0f });
    d.wires.push_back ({ wire++, toneId, 0, busId, 0, 1.0f });

    if (numOutputs > 0)
    {
        d.nodes.push_back (makeNode (mainOutId, types::hardwareOutput, { 1, (float) std::min (2, numOutputs) }));
        d.wires.push_back ({ wire++, faderId, 0, mainOutId, 0, 1.0f });
    }

    for (int ch = 0; ch < numInputs; ++ch)
    {
        const auto in = inputBase + (graph::NodeId) ch, pan = panBase + (graph::NodeId) ch;
        d.nodes.push_back (makeNode (in, types::hardwareInput, { (float) ch + 1, 1 }));
        d.nodes.push_back (makeNode (pan, types::pan, { 0, 1 }));
        d.wires.push_back ({ wire++, in, 0, pan, 0, 1.0f });
        d.wires.push_back ({ wire++, pan, 0, busId, 0, 1.0f });
    }

    // The main output node's input meter shows outputs 1-2; other outputs are silent.
    engine.setGraph (d);

    strips.clear();

    auto addStrip = [this] (juce::String name, graph::NodeId node, int channel, bool isOutput)
    {
        Strip s;
        s.label = std::make_unique<juce::Label> (juce::String(), name);
        styleLabel (*s.label, 12.0f, ui::theme::textMuted);
        s.label->setJustificationType (juce::Justification::centred);
        s.meter = std::make_unique<ui::LevelMeter>();
        s.node = node;
        s.channel = channel;
        s.isOutput = isOutput;
        addAndMakeVisible (*s.label);
        addAndMakeVisible (*s.meter);
        strips.push_back (std::move (s));
    };

    for (int ch = 0; ch < numInputs; ++ch)
        addStrip (juce::String (ch + 1), inputBase + (graph::NodeId) ch, 0, false);

    for (int ch = 0; ch < std::min (2, numOutputs); ++ch)
        addStrip (juce::String (ch + 1), mainOutId, ch, true);

    resized();
}

void MainComponent::pushParameters()
{
    auto& builder = engine.getBuilder();
    builder.setParameter (faderId, 0, (float) monitorLevel.getValue());
    builder.setParameter (faderId, 1, monitorToggle.getToggleState() ? 0.0f : 1.0f);
    builder.setParameter (toneId, 3, toneToggle.getToggleState() ? 1.0f : 0.0f);
}

void MainComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto elapsed = now - lastTick;
    lastTick = now;

    auto& builder = engine.getBuilder();

    for (auto& s : strips)
    {
        engine::MeterChannel* channel = nullptr;

        if (auto* processor = builder.getProcessor (s.node))
        {
            auto& meter = s.isOutput ? processor->getInputMeter (0) : processor->getOutputMeter (0);
            if (s.channel < meter.getNumChannels())
                channel = &meter.channel (s.channel);
        }

        s.meter->update (channel, elapsed);
    }

    // Status bar, about 4 times a second.
    static int divider = 0;
    if (++divider % 8 != 0)
        return;

    const auto st = engine.getStatus();
    peakCpu = std::max (peakCpu, st.stats.peakCpuLoad);

    if (st.deviceName.isEmpty())
        statusDevice.setText ("No audio device - choose one in Audio settings", juce::dontSendNotification);
    else
        statusDevice.setText (st.deviceName + "  (" + st.typeName + ")" + (st.running ? "" : "  - stopped"),
                              juce::dontSendNotification);

    const auto ms = [&st] (int samples) { return st.sampleRate > 0 ? 1000.0 * samples / st.sampleRate : 0.0; };
    statusFormat.setText (juce::String (st.sampleRate / 1000.0, 1) + " kHz   " + juce::String (st.bufferSize) + " samples ("
                              + juce::String (ms (st.bufferSize), 1) + " ms)   " + juce::String (st.numInputs) + " in / "
                              + juce::String (st.numOutputs) + " out",
                          juce::dontSendNotification);
    statusCpu.setText ("CPU " + juce::String (juce::roundToInt (st.stats.cpuLoad * 100.0f)) + "%  (peak "
                           + juce::String (juce::roundToInt (peakCpu * 100.0f)) + "%)",
                       juce::dontSendNotification);

    const auto minutes = (now - countersSince) / 60.0;
    auto counters = "Late " + juce::String (st.stats.lateCallbacks) + "   Overloads " + juce::String (st.stats.overloads);
    if (st.driverXruns >= 0)
        counters << "   Driver dropouts " << st.driverXruns;
    counters << "   (" << juce::String (minutes, 1) << " min)";
    statusCounters.setText (counters, juce::dontSendNotification);

    const auto problems = st.stats.lateCallbacks + st.stats.overloads + std::max (0, st.driverXruns);
    statusCounters.setColour (juce::Label::textColourId, problems > 0 ? ui::theme::warning : ui::theme::textMuted);

    const auto muted = engine.getCore().areOutputsMuted();
    muteButton.setButtonText (muted ? "Unmute outputs" : "Mute outputs");
    muteButton.setColour (juce::TextButton::buttonColourId, muted ? ui::theme::danger.darker (0.3f) : ui::theme::surfaceHigh);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (ui::theme::background);

    g.setColour (ui::theme::surface);
    g.fillRect (getLocalBounds().removeFromBottom (64));
    g.setColour (ui::theme::border);
    g.fillRect (0, getHeight() - 64, getWidth(), 1);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16, 0);

    // Status bar.
    auto status = getLocalBounds().removeFromBottom (64).reduced (16, 8);
    auto buttons = status.removeFromRight (520);
    for (auto* b : { &muteButton, &reportButton, &resetButton, &settingsButton })
    {
        b->setBounds (buttons.removeFromRight (122).reduced (4, 8));
    }
    auto top = status.removeFromTop (status.getHeight() / 2);
    statusDevice.setBounds (top.removeFromLeft (top.getWidth() / 2));
    statusFormat.setBounds (top);
    statusCpu.setBounds (status.removeFromLeft (160));
    statusCounters.setBounds (status);

    area.removeFromBottom (64);
    area.removeFromTop (12);
    title.setBounds (area.removeFromTop (32));
    area.removeFromTop (8);

    auto controls = area.removeFromTop (32);
    monitorToggle.setBounds (controls.removeFromLeft (280));
    toneToggle.setBounds (controls.removeFromLeft (180));
    monitorLevelLabel.setBounds (controls.removeFromLeft (110));
    monitorLevel.setBounds (controls.removeFromLeft (320));
    area.removeFromTop (16);

    auto headings = area.removeFromTop (24);
    const auto stripWidth = 28;
    const auto inputsWidth = std::max (80, numInputs * stripWidth);
    inputsHeading.setBounds (headings.removeFromLeft (inputsWidth));
    headings.removeFromLeft (32);
    outputsHeading.setBounds (headings);

    auto meters = area.reduced (0, 8);
    auto x = meters.getX();

    for (auto& s : strips)
    {
        if (s.isOutput && ! strips.empty() && &s == &*std::find_if (strips.begin(), strips.end(), [] (auto& t) { return t.isOutput; }))
            x = meters.getX() + inputsWidth + 32;

        auto column = juce::Rectangle<int> (x, meters.getY(), stripWidth, meters.getHeight());
        s.label->setBounds (column.removeFromBottom (18));
        s.meter->setBounds (column.reduced (6, 0));
        x += stripWidth;
    }
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager(), 0, 256, 0, 256,
                                                                           false, false, false, false);
    selector->setSize (520, 440);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = ui::theme::surface;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

juce::String MainComponent::makeReport()
{
    const auto st = engine.getStatus();
    const auto minutes = (juce::Time::getMillisecondCounterHiRes() / 1000.0 - countersSince) / 60.0;

    juce::String r;
    r << "Stage Plot Mixer engine test  [" << SPM_GIT_HASH << "]\n"
      << "Date:        " << juce::Time::getCurrentTime().toString (true, true) << "\n"
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

    const auto problems = st.stats.lateCallbacks + st.stats.overloads + std::max (0, st.driverXruns);
    r << "Verdict:     " << (! st.running ? "FAIL (device stopped)" : problems > 0 ? "CHECK (see counts above)" : "PASS") << "\n";
    return r;
}

void MainComponent::copyReport()
{
    const auto report = makeReport();
    juce::SystemClipboard::copyTextToClipboard (report);
    reportButton.setButtonText ("Copied");
    juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer (this)]
    {
        if (safe != nullptr)
            safe->reportButton.setButtonText ("Copy report");
    });
}

} // namespace spm::app
