// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/ParamFormat.h"

#include <cmath>

namespace spm::ui
{

namespace
{

bool isMinusInfinity (const nodes::ParamSpec& spec, float value)
{
    return spec.minusInfinityAtMinimum && value <= spec.minValue;
}

juce::String formatPan (float value)
{
    const auto amount = juce::roundToInt (std::abs (value) * 100.0f);
    if (amount == 0)
        return "C";
    return (value < 0.0f ? "L " : "R ") + juce::String (amount);
}

/** Fader law: the top 75% of the travel covers -30 dB..max, the rest min..-30 dB. */
juce::NormalisableRange<double> faderRange (double minDb, double maxDb, double interval)
{
    constexpr double knee = -30.0, kneePos = 0.25;

    juce::NormalisableRange<double> range (
        minDb, maxDb,
        [] (double start, double end, double x)
        {
            return x >= kneePos ? knee + (x - kneePos) / (1.0 - kneePos) * (end - knee)
                                : start + x / kneePos * (knee - start);
        },
        [] (double start, double end, double db)
        {
            return db >= knee ? kneePos + (db - knee) / (end - knee) * (1.0 - kneePos)
                              : (db - start) / (knee - start) * kneePos;
        },
        [interval] (double start, double end, double db)
        {
            return juce::jlimit (start, end, interval > 0.0 ? std::round (db / interval) * interval : db);
        });
    return range;
}

} // namespace

juce::String formatParam (const nodes::ParamSpec& spec, float value)
{
    switch (spec.kind)
    {
        case nodes::ParamKind::toggle:
            return value >= 0.5f ? "On" : "Off";

        case nodes::ParamKind::choice:
        {
            const auto index = (size_t) juce::jlimit (0, (int) spec.choices.size() - 1, juce::roundToInt (value));
            return spec.choices.empty() ? juce::String (value) : juce::String (spec.choices[index]);
        }

        case nodes::ParamKind::integer:
            return juce::String (juce::roundToInt (value)) + (spec.unit.empty() ? "" : " " + spec.unit);

        case nodes::ParamKind::continuous:
            break;
    }

    if (spec.id == "pan")
        return formatPan (value);

    if (isMinusInfinity (spec, value))
        return juce::String::fromUTF8 ("\xe2\x88\x92") + "inf " + spec.unit;

    if (spec.unit == "Hz")
        return value >= 1000.0f ? juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz"
                                : juce::String (juce::roundToInt (value)) + " Hz";

    if (spec.unit == "dB")
    {
        // A real minus sign; "+" for boosts so 0 stands out.
        const auto rounded = std::round (value * 10.0f) / 10.0f;
        auto text = juce::String (std::abs (rounded), 1);
        if (rounded < 0.0f)
            text = juce::String::fromUTF8 ("\xe2\x88\x92") + text;
        else if (rounded > 0.0f)
            text = "+" + text;
        return text + " dB";
    }

    if (spec.unit == "ms")
        return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " s"
                                : juce::String (value, value < 10.0f ? 1 : 0) + " ms";

    if (spec.unit == "%")
        return juce::String (juce::roundToInt (value)) + "%";

    if (spec.unit == ":1")
        return juce::String (value, value < 10.0f ? 1 : 0) + ":1";

    return juce::String (value, 2) + (spec.unit.empty() ? "" : " " + spec.unit);
}

float parseParam (const nodes::ParamSpec& spec, const juce::String& rawText)
{
    auto text = rawText.trim().toLowerCase().replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-");

    if (spec.kind == nodes::ParamKind::toggle)
        return (text == "on" || text == "1" || text == "yes") ? 1.0f : 0.0f;

    if (spec.kind == nodes::ParamKind::choice)
    {
        for (size_t i = 0; i < spec.choices.size(); ++i)
            if (juce::String (spec.choices[i]).equalsIgnoreCase (text))
                return (float) i;
    }

    if (text.contains ("inf"))
        return spec.minValue;

    if (spec.id == "pan")
    {
        if (text == "c" || text == "centre" || text == "center")
            return 0.0f;
        const auto amount = text.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
        if (text.startsWith ("l"))
            return -amount;
        if (text.startsWith ("r"))
            return amount;
        return text.getFloatValue() / 100.0f;
    }

    auto value = text.getFloatValue();
    if (spec.unit == "Hz" && text.contains ("k"))
        value *= 1000.0f;
    if (spec.unit == "ms" && text.endsWith ("s") && ! text.endsWith ("ms"))
        value *= 1000.0f;

    return juce::jlimit (spec.minValue, spec.maxValue, value);
}

void configureSlider (juce::Slider& slider, const nodes::ParamSpec& spec)
{
    const auto integer = spec.kind != nodes::ParamKind::continuous;

    if (spec.unit == "dB" && spec.minValue < -30.0f && ! spec.linearDb)
        slider.setNormalisableRange (faderRange (spec.minValue, spec.maxValue, 0.1));
    else
    {
        juce::NormalisableRange<double> range (spec.minValue, spec.maxValue, integer ? 1.0 : 0.0);
        if (spec.logarithmic)
            range.setSkewForCentre (std::sqrt ((double) spec.minValue * spec.maxValue));
        slider.setNormalisableRange (range);
    }

    slider.setDoubleClickReturnValue (true, spec.defaultValue);
    slider.textFromValueFunction = [spec] (double v) { return formatParam (spec, (float) v); };
    slider.valueFromTextFunction = [spec] (const juce::String& t) { return (double) parseParam (spec, t); };
    slider.setTooltip (spec.tooltip.empty() ? juce::String (spec.name) : juce::String (spec.tooltip));
    slider.updateText();  // the text box was filled before the formatter existed
}

void configureWireGainSlider (juce::Slider& slider)
{
    nodes::ParamSpec spec;
    spec.id = "wireGain";
    spec.name = "Wire gain";
    spec.unit = "dB";
    spec.minValue = -60.0f;
    spec.maxValue = 12.0f;
    spec.defaultValue = 0.0f;
    spec.minusInfinityAtMinimum = true;
    spec.tooltip = "Level of the signal through this wire";
    configureSlider (slider, spec);
}

} // namespace spm::ui
