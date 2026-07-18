/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_R_FLOW_H_
#define INCLUDE_R_FLOW_H_

#include <stdint.h>

struct r_cfg;

int flush_sdr_flow(struct r_cfg *cfg);

void reset_sdr_flow(struct r_cfg *cfg);

int push_sdr_flow(struct r_cfg *cfg, unsigned char *iq_buf, uint32_t len);

#endif /* INCLUDE_R_FLOW_H_ */
