/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "r_api.h"
#include "rtl_433.h"
#include "r_private.h"
#include "sdr.h"
#include "data.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifndef _MSC_VER
#include <getopt.h>
#else
#include "getopt/getopt.h"
#endif

char const *version_string(void)
{
    return "rtl_433"
#ifdef GIT_VERSION
#define STR_VALUE(arg) #arg
#define STR_EXPAND(s) STR_VALUE(s)
            " version " STR_EXPAND(GIT_VERSION)
            " branch " STR_EXPAND(GIT_BRANCH)
            " at " STR_EXPAND(GIT_TIMESTAMP)
#undef STR_VALUE
#undef STR_EXPAND
#else
            " version unknown"
#endif
            " inputs file rtl_tcp"
#ifdef RTLSDR
            " RTL-SDR"
#endif
#ifdef SOAPYSDR
            " SoapySDR"
#endif
            ;
}

void r_init_cfg(r_cfg_t *cfg)
{
    cfg->out_block_size  = DEFAULT_BUF_LENGTH;
    cfg->samp_rate       = DEFAULT_SAMPLE_RATE;
    cfg->conversion_mode = CONVERT_NATIVE;

    list_ensure_size(&cfg->in_files, 100);
    list_ensure_size(&cfg->output_handler, 16);

    cfg->demod = calloc(1, sizeof(*cfg->demod));
    if (!cfg->demod) {
        fprintf(stderr, "Could not create demod!\n");
        exit(1);
    }

    cfg->demod->level_limit = DEFAULT_LEVEL_LIMIT;
    cfg->demod->hop_time    = DEFAULT_HOP_TIME;

    list_ensure_size(&cfg->demod->r_devs, 100);
    list_ensure_size(&cfg->demod->dumper, 32);
}

r_cfg_t *r_create_cfg(void)
{
    r_cfg_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        fprintf(stderr, "Could not create cfg!\n");
        exit(1);
    }

    r_init_cfg(cfg);

    return cfg;
}

void r_free_cfg(r_cfg_t *cfg)
{
    if (cfg->dev)
        sdr_deactivate(cfg->dev);
    if (cfg->dev)
        sdr_close(cfg->dev);

    for (void **iter = cfg->demod->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        if (dumper->file && (dumper->file != stdout))
            fclose(dumper->file);
    }
    list_free_elems(&cfg->demod->dumper, free);

    list_free_elems(&cfg->demod->r_devs, free);

    if (cfg->demod->am_analyze)
        am_analyze_free(cfg->demod->am_analyze);

    pulse_detect_free(cfg->demod->pulse_detect);

    free(cfg->demod);

    list_free_elems(&cfg->output_handler, (list_elem_free_fn)data_output_free);

    list_free_elems(&cfg->in_files, NULL);

    //free(cfg);
}
