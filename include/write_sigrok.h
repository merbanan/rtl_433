/** @file
    Sigrok Pulseview format writer.

    Copyright (C) 2020 by Christian Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_WRITE_SIGROK_
#define INCLUDE_WRITE_SIGROK_

/** Write a Sigrok file from data dump files.

    @param filename file to write
    @param samplerate sample rate for the channels
    @param probes number of binary channels, needs "logic-1-1" file
    @param analogs number of analog channels, needs "analog-1-N-1" with N starting at probes+1
    @param labels channel labels, probes+analog strings or NULL for generic labels
*/
void write_sigrok(char const *filename, unsigned samplerate, unsigned probes, unsigned analogs, char const *labels[]);

/** Open a file in a forked Pulseview.

    @param filename file to open in Pulseview
*/
void open_pulseview(char const *filename);

#endif /* INCLUDE_WRITE_SIGROK_ */
