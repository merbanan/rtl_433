/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "r_version.h"

/* Include version.h if it exists, otherwise there is simply no version information. */
#if defined __has_include
#if __has_include("version.h")
#include "version.h"
#endif
#endif

char const *version_string(void)
{
    return "rtl_433"
#ifdef GIT_VERSION
           " version " GIT_VERSION
#ifdef GIT_BRANCH
           " branch " GIT_BRANCH
#endif
#ifdef GIT_TIMESTAMP
           " at " GIT_TIMESTAMP
#endif
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
#ifdef OPENSSL
           " with TLS"
#endif
            ;
}
