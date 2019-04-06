/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_R_API_H_
#define INCLUDE_R_API_H_

#include <stdint.h>

struct r_cfg;

/* general */

char const *version_string(void);

struct r_cfg *r_create_cfg(void);

void r_init_cfg(struct r_cfg *cfg);

void r_free_cfg(struct r_cfg *cfg);

#endif /* INCLUDE_R_API_H_ */
