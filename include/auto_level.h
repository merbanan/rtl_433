/** @file
    Auto-tracked detection floor for the pulse detector.

    Copyright (C) 2026 Andrew Berry <andrew@furrypaws.ca>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_AUTO_LEVEL_H_
#define INCLUDE_AUTO_LEVEL_H_

/// Lower bound for the auto-tracked detection floor.
///
/// Measured on real hardware: letting the tracked floor fall below -30 dB
/// raises CPU usage sharply (~3.9x at -35 dB vs -30 dB) for negligible
/// additional decode benefit, so auto-level tracking is clamped here.
#define MIN_LEVEL_AUTO_FLOOR -30.0f

/// Headroom the estimated noise must sit below the minimum detection level
/// before the floor is tracked down, and the offset the floor is placed at.
#define AUTO_LEVEL_HEADROOM_DB 3.0f

/// Smallest floor movement worth applying, to avoid thrashing on noise jitter.
#define AUTO_LEVEL_HYSTERESIS_DB 1.0f

/// Decide whether the auto-tracked detection floor should move.
///
/// The floor tracks @p noise_level plus ::AUTO_LEVEL_HEADROOM_DB, clamped to
/// ::MIN_LEVEL_AUTO_FLOOR, or to @p min_level when that was configured lower
/// still. It only moves when the estimated noise sits at least
/// ::AUTO_LEVEL_HEADROOM_DB below @p min_level and the move is larger than
/// ::AUTO_LEVEL_HYSTERESIS_DB.
///
/// Comparing against the clamped target, rather than the unclamped
/// noise_level + headroom, is what stops the floor re-adjusting on every frame
/// once the clamp is reached.
///
/// @param noise_level current estimated noise level in dB
/// @param min_level configured minimum detection level in dB
/// @param current currently applied auto-tracked floor in dB
/// @param[out] out receives the new floor in dB, only written when 1 is returned
/// @return 1 if the floor should be adjusted, 0 to leave it unchanged
int auto_level_next(float noise_level, float min_level, float current, float *out);

#endif /* INCLUDE_AUTO_LEVEL_H_ */
