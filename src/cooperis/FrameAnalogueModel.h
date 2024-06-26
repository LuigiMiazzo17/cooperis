//
// Copyright (C) 2022-2024 Michele Segata <segata@ccs-labs.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Base files derived from Veins VLC by Agon Memedi and contributors

#pragma once

#include <memory>
#include <vector>

#include "veins/veins.h"

#include "veins/base/utils/AntennaPosition.h"
#include "veins/base/utils/Coord.h"
#include "veins/modules/utility/HasLogProxy.h"

namespace veins {

class AirFrame;
class Signal;

/**
 * @brief Interface for the analogue models of the physical layer.
 *
 * An analogue model is a filter responsible for changing
 * the attenuation value of a Signal to simulate things like
 * shadowing, fading, pathloss or obstacles.
 *
 * @ingroup analogueModels
 */
class VEINS_API FrameAnalogueModel {

public:
    FrameAnalogueModel()
    {
    }

    virtual ~FrameAnalogueModel()
    {
    }

    /**
     * @brief Has to be overriden by every implementation.
     *
     * Filters a specified AirFrame's Signal by adding an attenuation
     * over time to the Signal.
     *
     * @param signal        The signal to filter.
     * @param frame         The frame the signal belongs to.
     * @return pointer to additional infos
     */
    virtual void filterSignal(Signal* signal, AirFrame* frame) = 0;
};

} // namespace veins
