// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DeviceTests.h"

#include "core/CallbackStats.h"
#include "core/DelayFinder.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace spm::diag
{

juce::String toString (TestResult r)
{
    switch (r)
    {
        case TestResult::pass:    return "PASS";
        case TestResult::warning: return "WARNING";
        case TestResult::fail:    return "FAIL";
    }

    return {};
}

namespace
{

double nowSeconds() noexcept
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

juce::String formatDb (float gain)
{
    if (gain <= 0.0f)
        return "-inf";

    return juce::String (juce::Decibels::gainToDecibels (gain, -200.0f), 1);
}

juce::String msText (double samples, double rate)
{
    return juce::String (1000.0 * samples / rate, 2) + " ms";
}

/** Keeps the message loop running while waiting (some drivers post notifications to it). */
void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

double chooseSampleRate (Console& console, juce::AudioIODevice& device, double wanted)
{
    const auto rates = device.getAvailableSampleRates();

    if (rates.isEmpty() || rates.contains (wanted))
        return wanted;

    auto best = rates.getFirst();
    for (auto r : rates)
        if (std::abs (r - wanted) < std::abs (best - wanted))
            best = r;

    console.line ("  WARNING: " + juce::String ((int) wanted) + " Hz not supported; using "
                  + juce::String ((int) best) + " Hz.");
    return best;
}

/** Creates and opens a device with every channel enabled. Returns nullptr on failure. */
std::unique_ptr<juce::AudioIODevice> openDevice (Console& console, juce::AudioIODeviceType& type,
                                                 const DeviceChoice& choice, const TestSettings& settings)
{
    console.line ("  Opening " + choice.describe() + " ...");

    std::unique_ptr<juce::AudioIODevice> device (type.createDevice (choice.outputDevice, choice.inputDevice));

    if (device == nullptr)
    {
        console.line ("  ERROR: the driver could not create this device.");
        return nullptr;
    }

    const auto rate = chooseSampleRate (console, *device, settings.sampleRate);
    const auto buffer = settings.bufferSize > 0 ? settings.bufferSize : device->getDefaultBufferSize();

    juce::BigInteger inputs, outputs;
    inputs.setRange (0, device->getInputChannelNames().size(), true);
    outputs.setRange (0, device->getOutputChannelNames().size(), true);

    const auto error = device->open (inputs, outputs, rate, buffer);

    if (error.isNotEmpty())
    {
        console.line ("  ERROR opening device: " + error);
        return nullptr;
    }

    const auto actualRate = device->getCurrentSampleRate();
    const auto inLatency = device->getInputLatencyInSamples();
    const auto outLatency = device->getOutputLatencyInSamples();

    console.line ("  Sample rate:      " + juce::String ((int) actualRate) + " Hz");
    console.line ("  Buffer size:      " + juce::String (device->getCurrentBufferSizeSamples()) + " samples ("
                  + msText (device->getCurrentBufferSizeSamples(), actualRate) + ")"
                  + (settings.bufferSize > 0 && device->getCurrentBufferSizeSamples() != settings.bufferSize
                         ? "  (requested " + juce::String (settings.bufferSize) + ")" : juce::String()));
    if (settings.bufferSize > 0 && device->getCurrentBufferSizeSamples() != settings.bufferSize
        && device->getAvailableBufferSizes().size() <= 1)
        console.line ("                    This driver type has a fixed buffer size; the request was ignored.");
    console.line ("  Bit depth:        " + juce::String (device->getCurrentBitDepth()));
    console.line ("  Active channels:  " + juce::String (device->getActiveInputChannels().countNumberOfSetBits()) + " in, "
                  + juce::String (device->getActiveOutputChannels().countNumberOfSetBits()) + " out");
    console.line ("  Reported latency: input " + juce::String (inLatency) + " + output " + juce::String (outLatency)
                  + " = " + juce::String (inLatency + outLatency) + " samples ("
                  + msText (inLatency + outLatency, actualRate) + ")");
    console.line ("  Control panel:    " + juce::String (device->hasControlPanel() ? "available" : "none"));

    return device;
}

char levelGlyph (float peak)
{
    const auto db = juce::Decibels::gainToDecibels (peak, -200.0f);

    if (peak >= CallbackStats::clipLevel) return 'X';
    if (db > -6.0f)  return '#';
    if (db > -20.0f) return '+';
    if (db > -40.0f) return '=';
    if (db > -60.0f) return '-';
    return '.';
}

//==============================================================================
class StabilityCallback final : public juce::AudioIODeviceCallback
{
public:
    CallbackStats stats;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        stats.reset (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                     device->getActiveInputChannels().countNumberOfSetBits());
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const auto t = nowSeconds();

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);

        stats.process (t, inputs, numInputs, numSamples);
    }

    void audioDeviceStopped() override {}

    void audioDeviceError (const juce::String& message) override
    {
        const juce::SpinLock::ScopedLockType lock (errorLock);
        lastError = message;
    }

    juce::String takeError()
    {
        const juce::SpinLock::ScopedLockType lock (errorLock);
        return std::exchange (lastError, {});
    }

private:
    juce::SpinLock errorLock;
    juce::String lastError;
};

//==============================================================================
class LatencyCallback final : public juce::AudioIODeviceCallback
{
public:
    LatencyCallback (std::vector<float> probeSignal, int inputIndex, int outputIndex, int preRollSamples, int maxLagSamples)
        : probe (std::move (probeSignal)),
          inChannel (inputIndex),
          outChannel (outputIndex),
          preRoll (preRollSamples),
          capture ((size_t) (preRollSamples + maxLagSamples + (int) probe.size()), 0.0f)
    {
    }

    /** Message thread: starts one measurement. Only call when not running. */
    void arm()
    {
        jassert (! isRunning());
        position = 0;
        std::fill (capture.begin(), capture.end(), 0.0f);
        state.store (running, std::memory_order_release);
    }

    bool isRunning() const noexcept { return state.load (std::memory_order_acquire) == running; }

    /** Safe to read once isRunning() returns false after arm(). */
    const std::vector<float>& getCapture() const noexcept { return capture; }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);

        if (state.load (std::memory_order_acquire) != running)
            return;

        auto* out = outChannel < numOutputs ? outputs[outChannel] : nullptr;
        const auto* in = inChannel < numInputs ? inputs[inChannel] : nullptr;
        const auto probeLength = (std::int64_t) probe.size();
        const auto captureLength = (std::int64_t) capture.size();

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = position + i;
            const auto p = t - preRoll;

            if (out != nullptr && p >= 0 && p < probeLength)
                out[i] = probe[(size_t) p];

            if (in != nullptr && t < captureLength)
                capture[(size_t) t] = in[i];
        }

        position += numSamples;

        if (position >= captureLength)
            state.store (done, std::memory_order_release);
    }

private:
    enum State { idle, running, done };

    const std::vector<float> probe;
    const int inChannel, outChannel;
    const std::int64_t preRoll;

    std::vector<float> capture;
    std::int64_t position = 0;
    std::atomic<int> state { idle };
};

float rms (const std::vector<float>& data, size_t start, size_t length)
{
    const auto end = std::min (data.size(), start + length);
    if (end <= start)
        return 0.0f;

    double sum = 0.0;
    for (auto i = start; i < end; ++i)
        sum += (double) data[i] * data[i];

    return (float) std::sqrt (sum / (double) (end - start));
}

} // namespace

//==============================================================================
TestResult runStabilityTest (Console& console, juce::AudioIODeviceType& type,
                             const DeviceChoice& choice, const TestSettings& settings)
{
    console.heading ("Stability & input level test");
    console.line ("  Outputs are silent during this test. Play or speak into inputs to check their levels.");

    auto device = openDevice (console, type, choice, settings);
    if (device == nullptr)
        return TestResult::fail;

    const auto inputNames = device->getInputChannelNames();
    const auto rate = device->getCurrentSampleRate();
    const auto buffer = device->getCurrentBufferSizeSamples();
    const auto xrunsBefore = device->getXRunCount();

    StabilityCallback callback;
    device->start (&callback);

    console.line ("  Running for " + juce::String (juce::roundToInt (settings.seconds)) + " s.  Legend:  . <-60 dB   - <-40   = <-20   + <-6   # <0   X clip");

    const auto numChannels = juce::jmin (callback.stats.getNumInputChannels(), 64);
    const auto start = nowSeconds();
    std::vector<float> heldPeaks ((size_t) numChannels, 0.0f);
    int tick = 0;

    while (nowSeconds() - start < settings.seconds && device->isPlaying())
    {
        pump (100);

        for (int ch = 0; ch < numChannels; ++ch)
            heldPeaks[(size_t) ch] = std::max (heldPeaks[(size_t) ch], callback.stats.takeInputPeak (ch));

        if (++tick % 3 != 0)
            continue;

        juce::String meters;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch > 0 && ch % 8 == 0)
                meters << ' ';
            meters << levelGlyph (heldPeaks[(size_t) ch]);
            heldPeaks[(size_t) ch] = 0.0f;
        }

        const auto s = callback.stats.snapshot();
        console.status ("  " + juce::String (juce::roundToInt (nowSeconds() - start)).paddedLeft (' ', 4) + "s  late "
                        + juce::String (s.lateCallbacks) + "  in: " + (meters.isEmpty() ? juce::String ("none") : meters));

        if (const auto error = callback.takeError(); error.isNotEmpty())
            console.line ("  DEVICE ERROR: " + error);
    }

    const auto stillPlaying = device->isPlaying();
    device->stop();
    const auto xrunsAfter = device->getXRunCount();
    device->close();
    console.endStatus();

    const auto s = callback.stats.snapshot();
    const auto elapsed = nowSeconds() - start;
    const auto expectedCallbacks = elapsed * rate / juce::jmax (1, buffer);
    const auto xruns = (xrunsBefore >= 0 && xrunsAfter >= 0) ? xrunsAfter - xrunsBefore : -1;

    console.line();
    console.line ("  Results");
    console.line ("    Duration:           " + juce::String (elapsed, 1) + " s");
    console.line ("    Callbacks:          " + juce::String (s.callbacks) + " (expected about "
                  + juce::String (juce::roundToInt (expectedCallbacks)) + ")");
    console.line ("    Block size:         " + juce::String (s.minBlockSize)
                  + (s.minBlockSize != s.maxBlockSize ? " to " + juce::String (s.maxBlockSize) : juce::String()) + " samples");
    console.line ("    Callback interval:  min " + juce::String (s.minIntervalMs, 2) + " / mean "
                  + juce::String (s.meanIntervalMs, 2) + " / max " + juce::String (s.maxIntervalMs, 2)
                  + " ms (buffer period " + juce::String (s.expectedPeriodMs, 2) + " ms)");
    console.line ("    Late callbacks:     " + juce::String (s.lateCallbacks) + " (gap > "
                  + juce::String (CallbackStats::lateThreshold, 1) + "x buffer period)");
    console.line ("    Driver xruns:       " + (xruns >= 0 ? juce::String (xruns) : juce::String ("not reported by this driver")));

    console.line ("    Input peaks:");
    for (int ch = 0; ch < callback.stats.getNumInputChannels(); ++ch)
    {
        const auto clips = callback.stats.getClipCount (ch);
        console.line ("      " + juce::String (ch + 1).paddedLeft (' ', 3) + " "
                      + inputNames[ch].quoted().paddedRight (' ', 24)
                      + formatDb (callback.stats.getInputPeakHold (ch)).paddedLeft (' ', 7) + " dBFS"
                      + (clips > 0 ? "   CLIPPED in " + juce::String (clips) + " blocks" : juce::String()));
    }

    juce::StringArray clipped;
    for (int ch = 0; ch < callback.stats.getNumInputChannels(); ++ch)
        if (callback.stats.getClipCount (ch) > 0)
            clipped.add (juce::String (ch + 1));

    if (! clipped.isEmpty())
    {
        console.line ("    Input " + clipped.joinIntoString (", ") + " clipped. That's a level problem, not a driver one:");
        console.line ("    turn the gain down until loud peaks stay around -6 dBFS (the '#' glyph, never 'X').");
    }

    console.line();
    auto result = TestResult::pass;

    if (! stillPlaying || s.callbacks == 0)
    {
        console.line ("  Reason: the device stopped during the test.");
        result = TestResult::fail;
    }
    else if (xruns > 0)
    {
        console.line ("  Reason: the driver reported " + juce::String (xruns) + " dropout" + (xruns == 1 ? "" : "s")
                      + " (xruns). Each one is an audible click or gap.");
        result = TestResult::fail;
    }
    else if (s.lateCallbacks > 0)
    {
        console.line ("  Reason: " + juce::String (s.lateCallbacks) + " callback" + (s.lateCallbacks == 1 ? "" : "s")
                      + " arrived late, but the driver reported no dropouts.");
        console.line ("  If audio clicked, try a larger buffer size, the 'High performance' power plan,");
        console.line ("  or disabling Wi-Fi/Bluetooth during the test.");
        result = TestResult::warning;
    }

    if (result != TestResult::pass && (type.getTypeName() == "Windows Audio" || type.getTypeName() == "DirectSound"))
    {
        console.line ("  This is common for " + type.getTypeName() + ", which shares the Windows mixer with other apps.");
        console.line ("  Stage Plot Mixer uses ASIO for live audio; repeat this test with the interface's ASIO driver.");
    }

    console.line ("  Stability test: " + toString (result));
    return result;
}

//==============================================================================
TestResult runLatencyTest (Console& console, juce::AudioIODeviceType& type,
                           const DeviceChoice& choice, const TestSettings& settings)
{
    console.heading ("Round-trip latency test");
    console.line ("  Before starting:");
    console.line ("   1. Connect a cable from OUTPUT " + juce::String (settings.outputChannel)
                  + " to INPUT " + juce::String (settings.inputChannel) + ".");
    console.line ("   2. Turn DIRECT MONITOR off, and set the input gain low (line level, INST off).");
    console.line ("   3. Turn speakers/headphones DOWN: a short burst of noise is played at "
                  + juce::String (settings.probeLevelDb, 0) + " dBFS.");

    if (! console.confirm ("  Ready?", true))
    {
        console.line ("  Skipped.");
        return TestResult::warning;
    }

    auto device = openDevice (console, type, choice, settings);
    if (device == nullptr)
        return TestResult::fail;

    const auto numIn = device->getActiveInputChannels().countNumberOfSetBits();
    const auto numOut = device->getActiveOutputChannels().countNumberOfSetBits();

    if (settings.inputChannel < 1 || settings.inputChannel > numIn
        || settings.outputChannel < 1 || settings.outputChannel > numOut)
    {
        console.line ("  ERROR: this device has " + juce::String (numIn) + " inputs and "
                      + juce::String (numOut) + " outputs; choose channels within that range.");
        device->close();
        return TestResult::fail;
    }

    console.line ("  Output " + juce::String (settings.outputChannel) + ": "
                  + device->getOutputChannelNames()[settings.outputChannel - 1].quoted()
                  + "  ->  input " + juce::String (settings.inputChannel) + ": "
                  + device->getInputChannelNames()[settings.inputChannel - 1].quoted());

    const auto rate = device->getCurrentSampleRate();
    const auto reported = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    const auto preRoll = juce::roundToInt (rate * 0.25);
    const auto maxLag = juce::roundToInt (rate * 0.5);
    const auto probeLength = 8192;

    LatencyCallback callback (makeProbeSignal (probeLength, juce::Decibels::decibelsToGain (settings.probeLevelDb)),
                              settings.inputChannel - 1, settings.outputChannel - 1, preRoll, maxLag);

    std::vector<float> probe = makeProbeSignal (probeLength, 1.0f);
    std::vector<int> measurements;

    device->start (&callback);
    pump (300); // let the driver settle

    for (int run = 1; run <= settings.latencyRuns; ++run)
    {
        callback.arm();

        const auto timeout = nowSeconds() + 5.0;
        while (callback.isRunning() && nowSeconds() < timeout)
            pump (20);

        if (callback.isRunning())
        {
            console.line ("  Run " + juce::String (run) + ": timed out (no audio callbacks).");
            break;
        }

        const auto& captured = callback.getCapture();
        const auto estimate = findDelay (probe, captured, preRoll + maxLag);
        const auto noise = rms (captured, 0, (size_t) preRoll / 2);

        juce::String line = "  Run " + juce::String (run) + ": ";

        if (! estimate.isReliable())
        {
            line << "no clear signal (correlation " << juce::String (estimate.correlation, 2)
                 << ", input peak " << formatDb (*std::max_element (captured.begin(), captured.end(),
                                                                     [] (float a, float b) { return std::abs (a) < std::abs (b); }))
                 << " dBFS)";
            console.line (line);
            continue;
        }

        const auto roundTrip = estimate.delaySamples - preRoll;
        const auto level = rms (captured, (size_t) estimate.delaySamples, (size_t) probeLength);

        line << juce::String (roundTrip) << " samples (" << msText (roundTrip, rate) << ")"
             << "  match " << juce::String (estimate.correlation, 2)
             << "  level " << formatDb (level) << " dBFS"
             << "  noise " << formatDb (noise) << " dBFS"
             << (estimate.inverted ? "  POLARITY INVERTED" : "");
        console.line (line);

        if (roundTrip <= 0)
            console.line ("    Signal arrived before it was sent - Direct Monitor or an internal mixer route is probably on.");
        else
            measurements.push_back (roundTrip);

        pump (100);
    }

    device->stop();
    device->close();

    console.line();

    if (measurements.empty())
    {
        console.line ("  No valid measurements. Check the cable, the channel numbers, input gain, and that");
        console.line ("  Direct Monitor is off. Use the stability test to confirm the input shows signal.");
        console.line ("  Latency test: FAIL");
        return TestResult::fail;
    }

    std::sort (measurements.begin(), measurements.end());
    const auto median = measurements[measurements.size() / 2];
    const auto spread = measurements.back() - measurements.front();

    console.line ("  Measured round trip: " + juce::String (median) + " samples (" + msText (median, rate) + ")"
                  + ", spread " + juce::String (spread) + " samples over " + juce::String ((int) measurements.size()) + " runs");
    console.line ("  Driver reports:      " + juce::String (reported) + " samples (" + msText (reported, rate) + ")");
    console.line ("  Unreported latency:  " + juce::String (median - reported) + " samples ("
                  + msText (median - reported, rate) + ")  (converters, safety buffers; the mixer will compensate)");

    const auto result = spread <= 1 && (int) measurements.size() == settings.latencyRuns ? TestResult::pass
                                                                                          : TestResult::warning;
    if (spread > 1)
        console.line ("  The latency changed between runs. A stable driver gives the same value every time.");

    console.line ("  Latency test: " + toString (result));
    return result;
}

} // namespace spm::diag
