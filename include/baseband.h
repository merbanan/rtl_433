/**
 * Baseband
 * 
 * Various functions for baseband sample processing
 *
 * Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
 * Copyright (C) 2015 Tommy Vestermark
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_BASEBAND_H_
#define INCLUDE_BASEBAND_H_

#include <stdint.h>

/** This will give a noisy envelope of OOK/ASK signals
 *  Subtract the bias (-128) and get an envelope estimation
 *  The output will be written in the input buffer
 *  @returns   pointer to the input buffer
 */
void envelope_detect(unsigned char *buf, uint32_t len, int decimate);

#define FILTER_ORDER 1

/// Lowpass filter
void low_pass_filter(uint16_t *x_buf, int16_t *y_buf, uint32_t len);

/// Initialize tables and constants
/// Should be called once at startup
void baseband_init(void);

/// Dump binary data (for debug purposes)
void baseband_dumpfile(uint8_t *buf, uint32_t len);

#endif /* INCLUDE_BASEBAND_H_ */
