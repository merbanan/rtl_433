/** @file
    Auto-tracked detection floor for the pulse detector.

    Copyright (C) 2026 Andrew Berry <andrew@furrypaws.ca>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "auto_level.h"

int auto_level_next(float noise_level, float min_level, float current, float *out)
{
    // Track the noise, but never below the clamp. A minimum level configured
    // below the clamp is an explicit request for that sensitivity, so it wins:
    // the clamp is there to stop the floor drifting down on its own, not to
    // overrule what was asked for.
    float clamp = MIN_LEVEL_AUTO_FLOOR;
    if (min_level < clamp) {
        clamp = min_level;
    }
    float target = noise_level + AUTO_LEVEL_HEADROOM_DB;
    if (target < clamp) {
        target = clamp;
    }

    // only track down when the noise is well below the configured minimum level
    if (noise_level >= min_level - AUTO_LEVEL_HEADROOM_DB) {
        return 0;
    }

    // ignore movements too small to be worth re-arming the pulse detector
    float delta = current - target;
    if (delta < 0.0f) {
        delta = -delta;
    }
    if (delta <= AUTO_LEVEL_HYSTERESIS_DB) {
        return 0;
    }

    *out = target;
    return 1;
}

#ifdef _TEST
#include <stdio.h>

#define ASSERT_ADJUSTS(noise, min_level, current, expected) \
    do { \
        float out_ = 0.0f; \
        int r_      = auto_level_next((noise), (min_level), (current), &out_); \
        if (r_ == 1 && out_ == (float)(expected)) \
            ++passed; \
        else { \
            ++failed; \
            fprintf(stderr, "FAIL: auto_level_next(%.2f, %.2f, %.2f) = %d, %.2f, want 1, %.2f\n", \
                    (double)(noise), (double)(min_level), (double)(current), r_, (double)out_, (double)(expected)); \
        } \
    } while (0)

#define ASSERT_HOLDS(noise, min_level, current) \
    do { \
        float out_ = 0.0f; \
        int r_      = auto_level_next((noise), (min_level), (current), &out_); \
        if (r_ == 0) \
            ++passed; \
        else { \
            ++failed; \
            fprintf(stderr, "FAIL: auto_level_next(%.2f, %.2f, %.2f) = 1, %.2f, want 0\n", \
                    (double)(noise), (double)(min_level), (double)(current), (double)out_); \
        } \
    } while (0)

int main(void)
{
    unsigned passed = 0;
    unsigned failed = 0;

    // the default minimum detection level, i.e. what -Y minlevel starts at
    float const def_min = -12.1442f;

    fprintf(stderr, "auto_level:: test\n");

    fprintf(stderr, "auto_level::tracking:\n");
    // noise well below the minimum level tracks to noise + 3 dB
    ASSERT_ADJUSTS(-25.0f, def_min, def_min, -22.0f);
    // a rising noise floor tracks back up just the same
    ASSERT_ADJUSTS(-18.0f, def_min, -22.0f, -15.0f);

    fprintf(stderr, "auto_level::gate:\n");
    // noise not far enough below the minimum level, leave the floor alone
    ASSERT_HOLDS(-14.0f, def_min, def_min);
    // noise above the minimum level entirely
    ASSERT_HOLDS(-5.0f, def_min, def_min);
    // exactly at the gate is not "well below", the comparison is strict
    ASSERT_HOLDS(def_min - AUTO_LEVEL_HEADROOM_DB, def_min, def_min);
    // Just past the gate the target is still within hysteresis of the starting
    // floor, so nothing moves: from a standing start the noise has to drop
    // roughly a full hysteresis step past the gate before the floor tracks.
    ASSERT_HOLDS(-16.0f, def_min, def_min);
    // far enough past the gate to clear hysteresis too
    ASSERT_ADJUSTS(-16.5f, def_min, def_min, -13.5f);

    fprintf(stderr, "auto_level::hysteresis:\n");
    // a 0.5 dB move is not worth re-arming the detector for
    ASSERT_HOLDS(-25.5f, def_min, -22.0f);
    // exactly at the hysteresis limit still holds, the comparison is inclusive
    ASSERT_HOLDS(-26.0f, def_min, -22.0f);
    // just past it adjusts
    ASSERT_ADJUSTS(-26.5f, def_min, -22.0f, -23.5f);

    fprintf(stderr, "auto_level::clamp:\n");
    // a quiet band would target -37 dB, the clamp holds it at -30 dB
    ASSERT_ADJUSTS(-40.0f, def_min, -22.0f, MIN_LEVEL_AUTO_FLOOR);
    // an absurdly quiet band clamps to the same place
    ASSERT_ADJUSTS(-99.0f, def_min, -22.0f, MIN_LEVEL_AUTO_FLOOR);
    // the last value that is not clamped
    ASSERT_ADJUSTS(-33.0f, def_min, -22.0f, MIN_LEVEL_AUTO_FLOOR);
    // A minimum level configured below the clamp must not be overruled by it:
    // -Y minlevel=-35 asks for -35 dB and must not be raised back to -30 dB.
    ASSERT_ADJUSTS(-40.0f, -35.0f, -32.0f, -35.0f);
    // and the floor still may not pass that lower clamp
    ASSERT_ADJUSTS(-99.0f, -35.0f, -32.0f, -35.0f);
    // a minimum level above the clamp leaves the clamp in charge
    ASSERT_ADJUSTS(-40.0f, -20.0f, -25.0f, MIN_LEVEL_AUTO_FLOOR);

    fprintf(stderr, "auto_level::convergence:\n");
    // Regression: once the floor is clamped, a noise level that keeps
    // targeting below the clamp must not re-adjust on every single frame.
    // Comparing against the unclamped noise + 3 dB made this loop forever,
    // re-arming the pulse detector and logging a warning per frame.
    {
        float level = def_min;
        float out   = 0.0f;
        int adjusts = 0;
        for (int i = 0; i < 100; ++i) {
            if (auto_level_next(-50.0f, def_min, level, &out)) {
                level = out;
                ++adjusts;
            }
        }
        if (adjusts == 1 && level == MIN_LEVEL_AUTO_FLOOR)
            ++passed;
        else {
            ++failed;
            fprintf(stderr, "FAIL: steady -50.0 dB noise adjusted %d times to %.2f, want 1 to %.2f\n",
                    adjusts, (double)level, (double)MIN_LEVEL_AUTO_FLOOR);
        }
    }

    // The same must hold for an unclamped level, hysteresis alone settles it.
    {
        float level = def_min;
        float out   = 0.0f;
        int adjusts = 0;
        for (int i = 0; i < 100; ++i) {
            if (auto_level_next(-25.0f, def_min, level, &out)) {
                level = out;
                ++adjusts;
            }
        }
        if (adjusts == 1 && level == -22.0f)
            ++passed;
        else {
            ++failed;
            fprintf(stderr, "FAIL: steady -25.0 dB noise adjusted %d times to %.2f, want 1 to -22.00\n",
                    adjusts, (double)level);
        }
    }

    // ------------- add test above this line -----------------------------------------------------
    // Show result of the tests, this line must stay the last line before return failed;
    fprintf(stderr, "auto_level:: test (%u/%u) passed, (%u) failed.\n", passed, passed + failed, failed);

    return failed;
}
#endif /* _TEST */
