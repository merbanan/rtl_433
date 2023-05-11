/**
 * RESTful HTTP control and WS interface
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_HTTP_SERVER_H_
#define INCLUDE_HTTP_SERVER_H_

#include "data.h"

struct mg_mgr;
struct r_cfg;

struct data_output *data_output_http_create(struct mg_mgr *mgr, const char *host, const char *port, struct r_cfg *cfg);

#endif /* INCLUDE_HTTP_SERVER_H_ */
