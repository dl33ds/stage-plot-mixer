// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "model/Session.h"

namespace spm::model
{

/** Fills an empty session with a starting mix: each input through a fader and pan into a
    stereo master bus, then a master fader to outputs 1–2.
*/
void createDefaultSession (Session& session, int numInputs);

} // namespace spm::model
