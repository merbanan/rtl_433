/** @file
    Security+ 1.0 rolling code

    Copyright (C) 2020 Peter Shipley <peter.shipley@gmail.com>
    Based on code by Clayton Smith https://github.com/argilo/secplus

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Freq 310, 315 and 390 MHz.

Security+ 1.0  is described in [US patent application US6980655B2](https://patents.google.com/patent/US6980655B2/)

*/

#include "decoder.h"
#include "compat_time.h"

/** @fn int _decode_v1_half(uint8_t *bits, uint8_t *result)

data comes in two bursts/packets, each bursts/packet is then separately passed to _decode_v1_half

Decodes transmitted binary into trinary data

Binary Bits are read from bits and stored as an array of uint8_t in result[]

The trinary value of the first nibble is also returned

The trinary conversion is accomplished done by counting the number of '1' in a group

Binary | Trinary
--- | ---
`0 0 0 0` | invalid
`0 0 0 1` | 0
`0 0 1 1` | 1
`0 1 1 1` | 2
`1 1 1 1` | invalid

000100110111011100110001 -> 0001 0011 0111 0111 0011 0001 -> 1 11 111 111 11 1 -> [0, 1,2, 2, 1, 0]

The patterns `1 1 1 1` or `0 0 0 0` should never happen

note: due to implementation this needs 44 bytes output in worst case of invalid data.
*/

static int _decode_v1_half(uint8_t *bits, uint8_t *result, int verbose)
{
    uint8_t *r;
    int x = 0;

    r = result;

    for (int i = 0; i < 11; i++) {
        // fprintf(stderr, "\nbin X = {%ld} %s\n", strlen(binstr), binstr);
        for (int j = 0; j < 8; j++) {
            int k = (bits[i] << j) & 0x80;
            // fprintf(stderr, "k == %d\n", k);
            if (k) {
                x++;
            }
            else {
                if (x == 0) {
                    continue;
                }
                else if (x == 1) {
                    *r++ = 0;
                    // fprintf(stderr, "\nbin 0 = {%ld} %s\n", strlen(binstr), binstr);
                }
                else if (x == 2) {
                    *r++ = 1;
                    // fprintf(stderr, "\nbin 1 = {%ld} %s\n", strlen(binstr), binstr);
                }
                else if (x == 3) {
                    *r++ = 2;
                    // fprintf(stderr, "\nbin 2 = {%ld} %s\n", strlen(binstr), binstr);
                }
                else { // x > 3
                    if (verbose)
                        fprintf(stderr, "Error x == %d\n", x);
                    return -1; // DECODE_FAIL_SANITY
                }
                x = 0;
            }
        }
    }

    return (int)result[0];
}

static const uint8_t preamble_1[1] = {0x02};
static const uint8_t preamble_2[1] = {0x07};

/** @fn static int find_next(bitbuffer_t *bitbuffer, int cur_index)

    find index of next bursts/packets in bitbuffer

    The transmissions do not have a magic number or preamble.

    They all start with a '0' or a '2'  represented at 0001. and 0111.
    since all nibbles start with 0 we can look for bytes
    000 + 0001 + 0 and 000 + 0111 + 0 for the start of a transmission
    (or just the 0001 and 0111 at the start of a bitbuffer)
*/

static int find_next(bitbuffer_t *bitbuffer, int cur_index)
{

    // int search_index;
    int search_index_1;
    int search_index_2;

    if (cur_index == 0 && ((bitbuffer->bb[0][0] & 0xf0) == 0x10 || (bitbuffer->bb[0][0] & 0xf0) == 0x70))
        return 0;

    if (cur_index == 0 && ((bitbuffer->bb[0][0] & 0xE0) == 0xe0 || (bitbuffer->bb[0][0] & 0xc0) == 0x80))
        return 0;

    search_index_1 = bitbuffer_search(bitbuffer, 0, cur_index, preamble_1, 8);
    search_index_1 += 3;

    search_index_2 = bitbuffer_search(bitbuffer, 0, cur_index, preamble_2, 8);
    search_index_2 += 3;

    // return first match in buffer
    return (search_index_1 < search_index_2 ? search_index_1 : search_index_2);
}

// max age for cache in us
#define CACHE_MAX_AGE 800000

static uint8_t cached_result[24] = {0};
static struct timeval cached_tv  = {0};

static int secplus_v1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t result_1[24] = {0};
    uint8_t result_2[24] = {0};
    int status           = 0;
    int search_index;

    // the max of 130 is just a guess
    if (bitbuffer->bits_per_row[0] < 84 || bitbuffer->bits_per_row[0] > 130) {
        if (decoder->verbose)
            (void)fprintf(stderr, "%s:return  DECODE_ABORT_LENGTH\n", __func__);
        return DECODE_ABORT_LENGTH;
    }

    if (decoder->verbose) {
        (void)fprintf(stderr, "%s : num rows = %u len %u\n", __func__, bitbuffer->num_rows, bitbuffer->bits_per_row[0]);
    }

    search_index = 0;
    while (search_index < bitbuffer->bits_per_row[0] && status == 0) {
        int dr            = 0;
        uint8_t buffy[44] = {0}; // actually we expect 22 bytes on valid decode
        uint8_t buffi[11] = {0};

        search_index = find_next(bitbuffer, search_index);

        if (decoder->verbose > 1)
            fprintf(stderr, "%s: find_next return : bits_per_row - search_index = %d\n", __func__, bitbuffer->bits_per_row[0] - search_index);

        // nothing found
        if (search_index == -1 || (search_index + 84) > bitbuffer->bits_per_row[0]) {
            break;
        }

        bitbuffer_extract_bytes(bitbuffer, 0, search_index, buffi, 84);

        dr = _decode_v1_half(buffi, buffy, decoder->verbose);

        if (dr < 0 || dr == 1) {
            // fprintf(stderr, "decode error\n");
            search_index += 4;
            continue;
        }
        else if (dr == 0) {
            // fprintf(stderr, "decode result_1\n");
            memcpy(result_1, buffy, 22);
            status ^= 0x001;
            search_index += 88;
        }
        else if (dr == 2) {
            // fprintf(stderr, "decode result_2\n");
            memcpy(result_2, buffy, 22);
            status ^= 0x002;
            search_index += 88;
        }

        // this should not happen
        if (status == 3)
            break;

    } // while

    if (decoder->verbose > 1)
        (void)fprintf(stderr, "%s: exited  loop status = %02X\n", __func__, status);

    // if we have both parts, move on and report data
    // if have only one part cache it for later.

    // if we have no parts, quit
    if (status == 0) {
        return -1; // found nothing
    }

    // is there data in cache?
    if (cached_tv.tv_sec) {
        struct timeval cur_tv;
        struct timeval res_tv;
        gettimeofday(&cur_tv, NULL);
        timeval_subtract(&res_tv, &cur_tv, &cached_tv);

        if (decoder->verbose > 1)
            fprintf(stderr, "%s res    %12ld %8ld\n", __func__, res_tv.tv_sec, (long)res_tv.tv_usec);

        // is the data not expired
        if (res_tv.tv_sec == 0 && res_tv.tv_usec < CACHE_MAX_AGE) {

            // if we have part 2 AND part 1 cached
            if (status == 2 && cached_result[0] == 0) {
                memcpy(result_1, cached_result, 21);
                status = 3;
                if (decoder->verbose)
                    fprintf(stderr, "%s: Load cache  part 1\n", __func__);
            }
            // if we have part 1 AND part 2 cached
            else if (status == 1 && cached_result[0] == 2) {
                memcpy(result_2, cached_result, 21);
                status = 3;
                if (decoder->verbose)
                    fprintf(stderr, "%s: Load cache  part 2\n", __func__);
            }
        }

        // clear cache because it is expired or used
        memset(cached_result, 0, sizeof(cached_result));
        timerclear(&cached_tv);

    } // if cache contains data

    if (status == 1) {
        gettimeofday(&cached_tv, NULL);
        memcpy(cached_result, result_1, 21);
        if (decoder->verbose)
            fprintf(stderr, "%s: caching part 1\n", __func__);
        return -2; // found only 1st part
    }
    else if (status == 2) {
        gettimeofday(&cached_tv, NULL);
        memcpy(cached_result, result_2, 21);
        if (decoder->verbose)
            fprintf(stderr, "%s: caching part 2\n", __func__);
        return -2; // found only 2st part
    }
    else if (status == 3) {
        // fprintf(stderr, "%s: got both\n", __func__);
    }
    else {
        return -1; // should never get here
    }

    // if we are here we have received both packets, stored in result_1 & result_2
    // we now generate values for rolling_temp & fixed
    // using the trinary data stored in result_1 & result_2

    uint32_t rolling;          // max 2**32
    uint32_t rolling_temp = 0; // max 2**32
    uint32_t fixed        = 0; // max 3^20 ( ~32 bits )

    uint8_t *res;
    res = result_1;
    res++;

    uint32_t acc = 0;
    for (int i = 0; i < 20; i += 2) {
        uint8_t digit = 0;

        digit        = res[i];
        rolling_temp = (rolling_temp * 3) + digit;
        acc += digit;

        digit = (60 + res[i + 1] - acc) % 3;
        fixed = (fixed * 3) + digit;
        acc += digit;
    }

    res = result_2;
    res++;

    acc = 0;
    for (int i = 0; i < 20; i += 2) {
        uint8_t digit = 0;

        digit        = res[i];
        rolling_temp = (rolling_temp * 3) + digit;
        acc += digit;

        digit = (60 + res[i + 1] - acc) % 3;
        fixed = (fixed * 3) + digit;
        acc += digit;
    }

    rolling = reverse32(rolling_temp);

    /*
        we now have values for rolling & fixed
        next we extract status info stored in the value for 'fixed'
    */
    int switch_id  = fixed % 3;
    int id;
    int id0        = (fixed / 3) % 3;
    int id1        = (int)(fixed / 9) % 3;
    int pad_id     = 0;
    int pin        = 0;
    char pin_s[24] = {0};

    int remote_id = 0;
    char *button  = "";

    if (id1 == 0) {
        //  pad_id = (fixed // 3**3) % (3**7)     27  3^72187
        pad_id = (fixed / 27) % 2187;
        id = pad_id;
        // pin = (fixed // 3**10) % (3**9)  3^10= 59049 3^9=19683
        pin = (fixed / 59049) % 19683;

        if (0 <= pin && pin <= 9999) {
            snprintf(pin_s, sizeof(pin_s), "%04d", pin);
        }
        else if (10000 <= pin && pin <= 11029) {
            strcat(pin_s, "enter");
        }

        int pin_suffix = 0;
        // pin_suffix = (fixed // 3**19) % 3   3^19=1162261467
        pin_suffix = (fixed / 1162261467) % 3;

        if (pin_suffix == 1)
            strcat(pin_s, "#");
        else if (pin_suffix == 2)
            strcat(pin_s, "*");

        // if (decoder->verbose)
        //     fprintf(stderr, "pad_id=%d\tpin=%d\tpin_s=%s\n", pad_id, pin, pin_s);
    }
    else {
        remote_id = (int)fixed / 27;
        id = remote_id;
        if (switch_id == 1)
            button = "left";
        else if (switch_id == 0)
            button = "middle";
        else if (switch_id == 2)
            button = "right";

        // if (decoder->verbose)
        //     fprintf(stderr, "remote_id=%d\tbutton=%s\n", remote_id, button);
    }

    // preformat unsigned int
    char rolling_str[16];
    snprintf(rolling_str, sizeof(rolling_str), "%u", rolling);

    // preformat unsigned int
    char fixed_str[16]; // should be 10 chars max
    snprintf(fixed_str, sizeof(fixed_str), "%u", fixed);

    // fprintf(stderr,  "# Security+:  rolling=2320615320  fixed=1846948897  (id1=2 id0=0 switch=1 remote_id=68405514 button=left)\n");
    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Secplus_v1",
            "id",           "",             DATA_INT,    id,
            "id0",          "ID_0",         DATA_INT,    id0,
            "id1",          "ID_1",         DATA_INT,    id1,
            "switch_id",    "Switch-ID",    DATA_INT,    switch_id,
            "pad_id",       "Pad-ID",       DATA_COND,   pad_id,    DATA_INT,    pad_id,
            "pin",          "Pin",          DATA_COND,   pin,       DATA_STRING, pin_s,
            "remote_id",    "Remote-ID",    DATA_COND,   remote_id, DATA_INT,    remote_id,
            "button_id",    "Button-ID",    DATA_COND,   remote_id, DATA_STRING, button,
            // "fixed",        "Fixed_Code",   DATA_INT,    fixed,
            "fixed",        "Fixed_Code",   DATA_STRING, fixed_str,
            // "rolling",      "Rolling_Code", DATA_INT,    rolling,
            "rolling",      "Rolling_Code", DATA_STRING, rolling_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {

        // Common fields
        "model",

        "id0",
        "id1",
        "switch_id",

        "pad_id",
        "pin",

        "remote_id",
        "button_id",

        "fixed",
        "rolling",
        NULL,
};

//      Freq 310.01M
//   -X "n=v1,m=OOK_PCM,s=500,l=500,t=40,r=10000,g=7400"

r_device secplus_v1 = {
        .name        = "Security+ (Keyfob)",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 500,
        .long_width  = 500,
        .tolerance   = 20,
        .gap_limit   = 15000,

        .reset_limit = 80000,
        .decode_fn   = &secplus_v1_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
