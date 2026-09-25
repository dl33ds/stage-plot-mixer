// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/LevelMeter.h"

#include "engine/Meters.h"
#include "engine/Smoother.h"
#include "ui/Theme.h"

namespace spm::ui
{

void LevelMeter::update (engine::MeterChannel* channel, double elapsed)
{
    source = channel;

    const auto newPeak = channel != nullptr ? engine::gainToDecibels (channel->takePeak(), minDb) : minDb;
    const auto newRms = channel != nullptr ? engine::gainToDecibels (channel->getRms(), minDb) : minDb;
    clipped = channel != nullptr && channel->hasClipped();

    // Peaks rise instantly and fall at 24 dB/s; the hold line stays for 1.5 s.
    peakDb = std::max (newPeak, peakDb - (float) (24.0 * elapsed));
    rmsDb = newRms;

    holdAge += elapsed;
    if (newPeak >= holdDb || holdAge > 1.5)
    {
        holdDb = newPeak;
        holdAge = 0.0;
    }

    repaint();
}

float LevelMeter::toProportion (float db) const noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto clipArea = area.removeFromTop (6.0f);
    area.removeFromTop (2.0f);

    g.setColour (clipped ? theme::danger : theme::surfaceHigh);
    g.fillRoundedRectangle (clipArea, 2.0f);

    g.setColour (theme::surfaceHigh);
    g.fillRoundedRectangle (area, 2.0f);

    auto levelColour = [] (float db)
    {
        return db > -6.0f ? theme::danger : db > -18.0f ? theme::warning : theme::good;
    };

    const auto h = area.getHeight();
    auto bar = [&] (float db, float alpha)
    {
        const auto top = area.getBottom() - h * toProportion (db);
        g.setColour (levelColour (db).withAlpha (alpha));
        g.fillRect (area.withTop (top));
    };

    bar (peakDb, 0.45f);
    bar (rmsDb, 1.0f);

    if (holdDb > minDb)
    {
        const auto y = area.getBottom() - h * toProportion (holdDb);
        g.setColour (levelColour (holdDb));
        g.fillRect (area.getX(), y - 1.0f, area.getWidth(), 2.0f);
    }
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    if (source != nullptr)
        source->clearClip();

    holdDb = minDb;
    clipped = false;
    repaint();
}

} // namespace spm::ui
