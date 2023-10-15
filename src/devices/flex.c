/** @file
    Flexible general purpose decoder.

    Copyright (C) 2017 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include "optparse.h"
#include "fatal.h"
#include <stdlib.h>

static inline int bit(const uint8_t *bytes, unsigned bit)
{
    return bytes[bit >> 3] >> (7 - (bit & 7)) & 1;
}

/// extract all mask bits skipping unmasked bits of a number up to 32/64 bits
static unsigned long compact_number(uint8_t *data, unsigned bit_offset, unsigned long mask)
{
    // clz (fls) is not worth the trouble
    int top_bit = 0;
    while (mask >> top_bit)
        top_bit++;
    unsigned long val = 0;
    for (int b = top_bit - 1; b >= 0; --b) {
        if (mask & (1 << b)) {
            val <<= 1;
            val |= bit(data, bit_offset);
        }
        bit_offset++;
    }
    return val;
}

/// extract a number up to 32/64 bits from given offset with given bit length
static unsigned long extract_number(uint8_t *data, unsigned bit_offset, unsigned bit_count)
{
    unsigned pos = bit_offset / 8;            // the first byte we need
    unsigned shl = bit_offset - pos * 8;      // shift left we need to align
    unsigned len = (shl + bit_count + 7) / 8; // number of bytes we need
    unsigned shr = 8 * len - shl - bit_count; // actual shift right
//    fprintf(stderr, "pos: %d, shl: %d, len: %d, shr: %d\n", pos, shl, len, shr);
    unsigned long val = data[pos];
    val = (uint8_t)(val << shl) >> shl; // mask off top bits
    for (unsigned i = 1; i < len - 1; ++i) {
        val = val << 8 | data[pos + i];
    }
    // shift down and add the last bits, so we don't potentially loose the top bits
    if (len > 1)
        val = (val << (8 - shr)) | (data[pos + len - 1] >> shr);
    else
        val >>= shr;
    return val;
}

struct flex_map {
    unsigned key;
    const char *val;
};

#define GETTER_MAP_SLOTS 16

struct flex_get {
    unsigned bit_offset;
    unsigned bit_count;
    unsigned long mask;
    const char *name;
    struct flex_map map[GETTER_MAP_SLOTS];
    const char *format;
};

#define GETTER_SLOTS 8

struct flex_params {
    char *name;
    unsigned min_rows;
    unsigned max_rows;
    unsigned min_bits;
    unsigned max_bits;
    unsigned min_repeats;
    unsigned max_repeats;
    unsigned invert;
    unsigned reflect;
    unsigned unique;
    unsigned count_only;
    unsigned match_len;
    uint8_t match_bits[128];
    unsigned preamble_len;
    uint8_t preamble_bits[128];
    uint32_t symbol_zero;
    uint32_t symbol_one;
    uint32_t symbol_sync;
    struct flex_get getter[GETTER_SLOTS];
    unsigned decode_uart;
    unsigned decode_dm;
    char const *fields[7 + GETTER_SLOTS + 1]; // NOTE: needs to match output_fields
};

static void print_row_bytes(char *row_bytes, uint8_t *bits, int num_bits)
{
    row_bytes[0] = '\0';
    // print byte-wide
    for (int col = 0; col < (num_bits + 7) / 8; ++col) {
        sprintf(&row_bytes[2 * col], "%02x", bits[col]);
    }
    // remove last nibble if needed
    row_bytes[2 * (num_bits + 3) / 8] = '\0';
}

static void render_getters(data_t *data, uint8_t *bits, struct flex_params *params)
{
    // add a data line for each getter
    for (int g = 0; g < GETTER_SLOTS && params->getter[g].bit_count > 0; ++g) {
        struct flex_get *getter = &params->getter[g];
        unsigned long val;
        if (getter->mask)
            val = compact_number(bits, getter->bit_offset, getter->mask);
        else
            val = extract_number(bits, getter->bit_offset, getter->bit_count);
        int m;
        for (m = 0; getter->map[m].val; m++) {
            if (getter->map[m].key == val) {
                data_append(data,
                        getter->name, "", DATA_STRING, getter->map[m].val,
                        NULL);
                break;
            }
        }
        if (!getter->map[m].val) {
            if (getter->format) {
                data_append(data,
                    getter->name, "", DATA_FORMAT, getter->format, DATA_INT, val,
                    NULL);
            } else {
                data_append(data,
                        getter->name, "", DATA_INT, val,
                        NULL);
            }
        }
    }
}

/**
Generic flex decoder.
*/
static int flex_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int i;
    int match_count = 0;
    data_t *data;
    data_t *row_data[BITBUF_ROWS];
    char *row_codes[BITBUF_ROWS];
    char row_bytes[BITBUF_ROWS * BITBUF_COLS * 2 + 1]; // TODO: this is a lot of stack

    struct flex_params *params = decoder->decode_ctx;

    // discard short / unwanted bitbuffers
    if ((bitbuffer->num_rows < params->min_rows)
            || (params->max_rows && bitbuffer->num_rows > params->max_rows))
        return DECODE_ABORT_LENGTH;

    for (i = 0; i < bitbuffer->num_rows; i++) {
        if ((bitbuffer->bits_per_row[i] >= params->min_bits)
                && (!params->max_bits || bitbuffer->bits_per_row[i] <= params->max_bits))
            match_count++;
    }
    if (!match_count)
        return DECODE_ABORT_LENGTH;

    // discard unless min_repeats, min_bits
    // TODO: check max_repeats, max_bits
    int r = bitbuffer_find_repeated_row(bitbuffer, params->min_repeats, params->min_bits);
    if (r < 0)
        return DECODE_ABORT_EARLY;
    // TODO: set match_count to count of repeated rows

    if (params->invert) {
        bitbuffer_invert(bitbuffer);
    }

    if (params->reflect) {
        // TODO: refactor to utils
        for (i = 0; i < bitbuffer->num_rows; ++i) {
            reflect_bytes(bitbuffer->bb[i], (bitbuffer->bits_per_row[i] + 7) / 8);
        }
    }

    // discard unless match
    if (params->match_len) {
        r = -1;
        match_count = 0;
        for (i = 0; i < bitbuffer->num_rows; i++) {
            if (bitbuffer_search(bitbuffer, i, 0, params->match_bits, params->match_len) < bitbuffer->bits_per_row[i]) {
                if (r < 0)
                    r = i;
                match_count++;
            }
        }
        if (!match_count)
            return DECODE_FAIL_SANITY;
    }

    // discard unless match, this should be an AND condition
    if (params->preamble_len) {
        r = -1;
        match_count = 0;
        for (i = 0; i < bitbuffer->num_rows; i++) {
            unsigned pos = bitbuffer_search(bitbuffer, i, 0, params->preamble_bits, params->preamble_len);
            if (pos < bitbuffer->bits_per_row[i]) {
                if (r < 0)
                    r = i;
                match_count++;
                pos += params->preamble_len;
                // TODO: refactor to bitbuffer_shift_row()
                unsigned len = bitbuffer->bits_per_row[i] - pos;
                bitbuffer_t tmp = {0};
                bitbuffer_extract_bytes(bitbuffer, i, pos, tmp.bb[0], len);
                memcpy(bitbuffer->bb[i], tmp.bb[0], (len + 7) / 8);
                bitbuffer->bits_per_row[i] = len;
            }
        }
        if (!match_count)
            return DECODE_FAIL_SANITY;
    }

    if (params->symbol_zero) {
        uint32_t zero = params->symbol_zero;
        uint32_t one  = params->symbol_one;
        uint32_t sync = params->symbol_sync;

        for (i = 0; i < bitbuffer->num_rows; i++) {
            // TODO: refactor to bitbuffer_decode_symbol_row()
            unsigned len    = bitbuffer->bits_per_row[i];
            bitbuffer_t tmp = {0};
            len             = extract_bits_symbols(bitbuffer->bb[i], 0, len, zero, one, sync, tmp.bb[0]);
            memcpy(bitbuffer->bb[i], tmp.bb[0], len); // safe to write over: can only be shorter
            bitbuffer->bits_per_row[i] = len;
        }
        // TODO: apply min_bits, max_bits check
    }

    if (params->decode_uart) {
        for (i = 0; i < bitbuffer->num_rows; i++) {
            // TODO: refactor to bitbuffer_decode_uart_row()
            unsigned len = bitbuffer->bits_per_row[i];
            bitbuffer_t tmp = {0};
            len = extract_bytes_uart(bitbuffer->bb[i], 0, len, tmp.bb[0]);
            memcpy(bitbuffer->bb[i], tmp.bb[0], len); // safe to write over: can only be shorter
            bitbuffer->bits_per_row[i] = len * 8;
        }
    }

    if (params->decode_dm) {
        for (i = 0; i < bitbuffer->num_rows; i++) {
            // TODO: refactor to bitbuffer_decode_dm_row()
            unsigned len = bitbuffer->bits_per_row[i];
            bitbuffer_t tmp = {0};
            bitbuffer_differential_manchester_decode(bitbuffer, i, 0, &tmp, len);
            len = tmp.bits_per_row[0];
            memcpy(bitbuffer->bb[i], tmp.bb[0], (len + 7) / 8); // safe to write over: can only be shorter
            bitbuffer->bits_per_row[i] = len;
        }
    }

    if (decoder->verbose) {
        decoder_log_bitbuffer(decoder, 1, params->name, bitbuffer, "");
    }

    // discard duplicates
    if (params->unique) {
        print_row_bytes(row_bytes, bitbuffer->bb[r], bitbuffer->bits_per_row[r]);

        /* clang-format off */
        data = data_make(
                "model", "", DATA_STRING, params->name, // "User-defined"
                "count", "", DATA_INT, match_count,
                "num_rows", "", DATA_INT, bitbuffer->num_rows,
                "len", "", DATA_INT, bitbuffer->bits_per_row[r],
                "data", "", DATA_STRING, row_bytes,
                NULL);
        /* clang-format on */

        // add a data line for each getter
        render_getters(data, bitbuffer->bb[r], params);

        decoder_output_data(decoder, data);
        return 1;
    }

    if (params->count_only) {
        /* clang-format off */
        data = data_make(
                "model", "", DATA_STRING, params->name, // "User-defined"
                "count", "", DATA_INT, match_count,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    for (i = 0; i < bitbuffer->num_rows; i++) {
        print_row_bytes(row_bytes, bitbuffer->bb[i], bitbuffer->bits_per_row[i]);

        /* clang-format off */
        row_data[i] = data_make(
                "len", "", DATA_INT, bitbuffer->bits_per_row[i],
                "data", "", DATA_STRING, row_bytes,
                NULL);
        /* clang-format on */

        // add a data line for each getter
        render_getters(row_data[i], bitbuffer->bb[i], params);

        // print at least one '0'
        if (row_bytes[0] == '\0') {
            snprintf(row_bytes, sizeof(row_bytes), "0");
        }

        // a simpler representation for csv output
        row_codes[i] = malloc(8 + bitbuffer->bits_per_row[i] / 4 + 1); // "{nnnn}..\0"
        if (!row_codes[i])
            WARN_MALLOC("flex_decode()");
        else // NOTE: skipped on alloc failure.
            sprintf(row_codes[i], "{%d}%s", bitbuffer->bits_per_row[i], row_bytes);
    }
    /* clang-format off */
    data = data_make(
            "model", "", DATA_STRING, params->name, // "User-defined"
            "count", "", DATA_INT, match_count,
            "num_rows", "", DATA_INT, bitbuffer->num_rows,
            "rows", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_DATA, row_data),
            "codes", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_codes),
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    for (i = 0; i < bitbuffer->num_rows; i++) {
        free(row_codes[i]);
    }

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "count",
        "num_rows",
        "rows",
        "codes",
        // "len", // unique only
        // "data", // unique only
        NULL,
};

static void usage(void)
{
    fprintf(stderr,
            "Use -X <spec> to add a general purpose decoder. For usage use -X help\n");
    exit(1);
}

static void help(void)
{
    fprintf(stderr,
            "\t\t= Flex decoder spec =\n"
            "Use -X <spec> to add a flexible general purpose decoder.\n\n"
            "<spec> is \"key=value[,key=value...]\"\n"
            "Common keys are:\n"
            "\tname=<name> (or: n=<name>)\n"
            "\tmodulation=<modulation> (or: m=<modulation>)\n"
            "\tshort=<short> (or: s=<short>)\n"
            "\tlong=<long> (or: l=<long>)\n"
            "\tsync=<sync> (or: y=<sync>)\n"
            "\treset=<reset> (or: r=<reset>)\n"
            "\tgap=<gap> (or: g=<gap>)\n"
            "\ttolerance=<tolerance> (or: t=<tolerance>)\n"
            "\tpriority=<n> : run decoder only as fallback\n"
            "where:\n"
            "<name> can be any descriptive name tag you need in the output\n"
            "<modulation> is one of:\n"
            "\tOOK_MC_ZEROBIT :  Manchester Code with fixed leading zero bit\n"
            "\tOOK_PCM :         Non Return to Zero coding (Pulse Code)\n"
            "\tOOK_RZ :          Return to Zero coding (Pulse Code)\n"
            "\tOOK_PPM :         Pulse Position Modulation\n"
            "\tOOK_PWM :         Pulse Width Modulation\n"
            "\tOOK_DMC :         Differential Manchester Code\n"
            "\tOOK_PIWM_RAW :    Raw Pulse Interval and Width Modulation\n"
            "\tOOK_PIWM_DC :     Differential Pulse Interval and Width Modulation\n"
            "\tOOK_MC_OSV1 :     Manchester Code for OSv1 devices\n"
            "\tFSK_PCM :         FSK Pulse Code Modulation\n"
            "\tFSK_PWM :         FSK Pulse Width Modulation\n"
            "\tFSK_MC_ZEROBIT :  Manchester Code with fixed leading zero bit\n"
            "<short>, <long>, <sync> are nominal modulation timings in us,\n"
            "<reset>, <gap>, <tolerance> are maximum modulation timings in us:\n"
            "PCM/RZ  short: Nominal width of pulse [us]\n"
            "         long: Nominal width of bit period [us]\n"
            "PPM     short: Nominal width of '0' gap [us]\n"
            "         long: Nominal width of '1' gap [us]\n"
            "PWM     short: Nominal width of '1' pulse [us]\n"
            "         long: Nominal width of '0' pulse [us]\n"
            "         sync: Nominal width of sync pulse [us] (optional)\n"
            "common    gap: Maximum gap size before new row of bits [us]\n"
            "        reset: Maximum gap size before End Of Message [us]\n"
            "    tolerance: Maximum pulse deviation [us] (optional).\n"
            "Available options are:\n"
            "\tbits=<n> : only match if at least one row has <n> bits\n"
            "\trows=<n> : only match if there are <n> rows\n"
            "\trepeats=<n> : only match if some row is repeated <n> times\n"
            "\t\tuse opt>=n to match at least <n> and opt<=n to match at most <n>\n"
            "\tinvert : invert all bits\n"
            "\treflect : reflect each byte (MSB first to MSB last)\n"
            "\tdecode_uart : UART 8n1 (10-to-8) decode\n"
            "\tdecode_dm : Differential Manchester decode\n"
            "\tmatch=<bits> : only match if the <bits> are found\n"
            "\tpreamble=<bits> : match and align at the <bits> preamble\n"
            "\t\t<bits> is a row spec of {<bit count>}<bits as hex number>\n"
            "\tunique : suppress duplicate row output\n\n"
            "\tcountonly : suppress detailed row output\n\n"
            "E.g. -X \"n=doorbell,m=OOK_PWM,s=400,l=800,r=7000,g=1000,match={24}0xa9878c,repeats>=3\"\n\n");
    exit(0);
}

static unsigned parse_modulation(char const *str)
{
    if (!strcasecmp(str, "OOK_MC_ZEROBIT"))
        return OOK_PULSE_MANCHESTER_ZEROBIT;
    else if (!strcasecmp(str, "OOK_PCM"))
        return OOK_PULSE_PCM;
    else if (!strcasecmp(str, "OOK_RZ"))
        return OOK_PULSE_RZ;
    else if (!strcasecmp(str, "OOK_PPM"))
        return OOK_PULSE_PPM;
    else if (!strcasecmp(str, "OOK_PWM"))
        return OOK_PULSE_PWM;
    else if (!strcasecmp(str, "OOK_DMC"))
        return OOK_PULSE_DMC;
    else if (!strcasecmp(str, "OOK_PIWM_RAW"))
        return OOK_PULSE_PIWM_RAW;
    else if (!strcasecmp(str, "OOK_PIWM_DC"))
        return OOK_PULSE_PIWM_DC;
    else if (!strcasecmp(str, "OOK_MC_OSV1"))
        return OOK_PULSE_PWM_OSV1;
    else if (!strcasecmp(str, "FSK_PCM"))
        return FSK_PULSE_PCM;
    else if (!strcasecmp(str, "FSK_PWM"))
        return FSK_PULSE_PWM;
    else if (!strcasecmp(str, "FSK_MC_ZEROBIT"))
        return FSK_PULSE_MANCHESTER_ZEROBIT;
    else {
        fprintf(stderr, "Bad flex spec, unknown modulation!\n");
        usage();
    }
    return 0;
}

// used for match, preamble, getter, limited to 1024 bits (128 byte).
static unsigned parse_bits(const char *code, uint8_t *bitrow)
{
    bitbuffer_t bits = {0};
    bitbuffer_parse(&bits, code);
    if (bits.num_rows != 1) {
        fprintf(stderr, "Bad flex spec, \"match\", \"preamble\", and getter mask need exactly one bit row (%d found)!\n", bits.num_rows);
        usage();
    }
    unsigned len = bits.bits_per_row[0];
    if (len > 1024) {
        fprintf(stderr, "Bad flex spec, \"match\", \"preamble\", and getter mask may have up to 1024 bits (%u found)!\n", len);
        usage();
    }
    memcpy(bitrow, bits.bb[0], (len + 7) / 8);
    return len;
}

// used for symbol decode, limited to 27 bits (32 - 5).
static uint32_t parse_symbol(const char *code)
{
    bitbuffer_t bits = {0};
    bitbuffer_parse(&bits, code);
    if (bits.num_rows != 1) {
        fprintf(stderr, "Bad flex spec, \"symbol\" needs exactly one bit row (%d found)!\n", bits.num_rows);
        usage();
    }
    unsigned len = bits.bits_per_row[0];
    if (len > 27) {
        fprintf(stderr, "Bad flex spec, \"symbol\" may have up to 27 bits (%u found)!\n", len);
        usage();
    }
    uint8_t *b = bits.bb[0];
    return ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3] << 0) | len;
}

static const char *parse_map(const char *arg, struct flex_get *getter)
{
    const char *c = arg;
    int i = 0;

    while (*c == ' ') c++;
    if (*c == '[') c++;

    while (*c) {
        unsigned long key;
        char *val;

        while (*c == ' ') c++;
        if (*c == ']') return c + 1;

        // first parse a number
        key = strtol(c, (char **)&c, 0); // hex, oct, or dec

        while (*c == ' ') c++;
        if (*c == ':') c++;
        while (*c == ' ') c++;

        // then parse a string
        const char *e = c;
        while (*e && *e != ' ' && *e != ']') e++;
        val = malloc(e - c + 1);
        if (!val)
            WARN_MALLOC("parse_map()");
        else { // NOTE: skipped on alloc failure.
            memcpy(val, c, e - c);
            val[e - c] = '\0';
        }
        c = e;

        // store result
        getter->map[i].key = key;
        getter->map[i].val = val;
        i++;
    }
    return c;
}

static void parse_getter(const char *arg, struct flex_get *getter)
{
    uint8_t bitrow[128];
    while (arg && *arg) {
        if (*arg == '[') {
            arg = parse_map(arg, getter);
            continue;
        }
        char *p = strchr(arg, ':');
        if (p)
            *p++ = '\0';
        if (*arg == '@')
            getter->bit_offset = strtol(++arg, NULL, 0);
        else if (*arg == '{' || (*arg >= '0' && *arg <= '9')) {
            getter->bit_count = parse_bits(arg, bitrow);
            getter->mask = extract_number(bitrow, 0, getter->bit_count);
        }
        else if (*arg == '%') {
            getter->format = strdup(arg);
            if (!getter->format)
                FATAL_STRDUP("parse_getter()");
        }
        else {
            getter->name = strdup(arg);
            if (!getter->name)
                FATAL_STRDUP("parse_getter()");
        }
        arg = p;
    }
    if (!getter->name) {
        fprintf(stderr, "Bad flex spec, \"get\" missing name!\n");
        usage();
    }
    /*
    if (decoder->verbose)
        fprintf(stderr, "parse_getter() bit_offset: %d bit_count: %d mask: %lx name: %s\n",
                getter->bit_offset, getter->bit_count, getter->mask, getter->name);
    */
}

// NOTE: this is declared in rtl_433.c also.
r_device *flex_create_device(char *spec);

r_device *flex_create_device(char *spec)
{
    if (!spec || !*spec || *spec == '?' || !strncasecmp(spec, "help", strlen(spec))) {
        help();
    }

    struct flex_params *params = calloc(1, sizeof(*params));
    if (!params) {
        WARN_CALLOC("flex_create_device()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }
    r_device *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        WARN_CALLOC("flex_create_device()");
        free(params);
        return NULL; // NOTE: returns NULL on alloc failure.
    }
    dev->decode_ctx = params;
    int get_count = 0;

    spec = strdup(spec);
    if (!spec)
        FATAL_STRDUP("flex_create_device()");

    dev->decode_fn = flex_callback;
    dev->fields = output_fields;

    char *key, *val;
    while (getkwargs(&spec, &key, &val)) {
        key = remove_ws(key);
        val = trim_ws(val);

        if (!key || !*key)
            continue;
        else if (!strcasecmp(key, "n") || !strcasecmp(key, "name")) {
            params->name = strdup(val);
            if (!params->name)
                FATAL_STRDUP("flex_create_device()");
            int name_size = strlen(val) + 27;
            char* flex_name = malloc(name_size);
            if (!flex_name)
                FATAL_MALLOC("flex_create_device()");
            snprintf(flex_name, name_size, "General purpose decoder '%s'", val);
            dev->name = flex_name;
        }

        else if (!strcasecmp(key, "m") || !strcasecmp(key, "modulation"))
            dev->modulation = parse_modulation(val);
        else if (!strcasecmp(key, "s") || !strcasecmp(key, "short"))
            dev->short_width = atoi(val);
        else if (!strcasecmp(key, "l") || !strcasecmp(key, "long"))
            dev->long_width = atoi(val);
        else if (!strcasecmp(key, "y") || !strcasecmp(key, "sync"))
            dev->sync_width = atoi(val);
        else if (!strcasecmp(key, "g") || !strcasecmp(key, "gap"))
            dev->gap_limit = atoi(val);
        else if (!strcasecmp(key, "r") || !strcasecmp(key, "reset"))
            dev->reset_limit = atoi(val);
        else if (!strcasecmp(key, "t") || !strcasecmp(key, "tolerance"))
            dev->tolerance = atoi(val);
        else if (!strcasecmp(key, "prio") || !strcasecmp(key, "priority"))
            dev->priority = atoi(val);

        else if (!strcasecmp(key, "bits>"))
            params->min_bits = val ? atoi(val) : 0;
        else if (!strcasecmp(key, "bits<"))
            params->max_bits = val ? atoi(val) : 0;
        else if (!strcasecmp(key, "bits"))
            params->min_bits = params->max_bits = val ? atoi(val) : 0;

        else if (!strcasecmp(key, "rows>"))
            params->min_rows = val ? atoi(val) : 0;
        else if (!strcasecmp(key, "rows<"))
            params->max_rows = val ? atoi(val) : 0;
        else if (!strcasecmp(key, "rows"))
            params->min_rows = params->max_rows = val ? atoi(val) : 0;

        else if (!strcasecmp(key, "repeats>"))
            params->min_repeats = val ? atoi(val) : 0;
        else if (!strcasecmp(key, "repeats<"))
            params->max_repeats = val ? atoi(val) : 0;
        else if (!strcasecmp(key, "repeats"))
            params->min_repeats = params->max_repeats = val ? atoi(val) : 0;

        else if (!strcasecmp(key, "invert"))
            params->invert = val ? atoi(val) : 1;
        else if (!strcasecmp(key, "reflect"))
            params->reflect = val ? atoi(val) : 1;

        else if (!strcasecmp(key, "match"))
            params->match_len = parse_bits(val, params->match_bits);

        else if (!strcasecmp(key, "preamble"))
            params->preamble_len = parse_bits(val, params->preamble_bits);

        else if (!strcasecmp(key, "countonly"))
            params->count_only = val ? atoi(val) : 1;

        else if (!strcasecmp(key, "unique"))
            params->unique = val ? atoi(val) : 1;

        else if (!strcasecmp(key, "decode_uart"))
            params->decode_uart = val ? atoi(val) : 1;
        else if (!strcasecmp(key, "decode_dm"))
            params->decode_dm = val ? atoi(val) : 1;

        else if (!strcasecmp(key, "symbol_zero"))
            params->symbol_zero = parse_symbol(val);
        else if (!strcasecmp(key, "symbol_one"))
            params->symbol_one = parse_symbol(val);
        else if (!strcasecmp(key, "symbol_sync"))
            params->symbol_sync = parse_symbol(val);

        else if (!strcasecmp(key, "get")) {
            if (get_count < GETTER_SLOTS)
                parse_getter(val, &params->getter[get_count++]);
            else {
                fprintf(stderr, "Maximum getter slots exceeded (%d)!\n", GETTER_SLOTS);
                usage();
            }

        } else {
            fprintf(stderr, "Bad flex spec, unknown keyword (%s)!\n", key);
            usage();
        }
    }

    if (params->min_bits < params->match_len)
        params->min_bits = params->match_len;

    if (params->min_bits > 0 && params->min_repeats < 1)
        params->min_repeats = 1;

    // add getter fields if unique requested
    if (params->unique) {
        int i = 0;
        for (int f = 0; output_fields[f]; ++f) {
            params->fields[i++] = output_fields[f];
        }
        params->fields[i++] = "len";
        params->fields[i++] = "data";
        for (int g = 0; g < GETTER_SLOTS && params->getter[g].name; ++g) {
            params->fields[i++] = params->getter[g].name;
        }
        dev->fields = params->fields;
    }

    // sanity checks

    if (!params->name || !*params->name) {
        fprintf(stderr, "Bad flex spec, missing name!\n");
        usage();
    }

    if (!dev->modulation) {
        fprintf(stderr, "Bad flex spec, missing modulation!\n");
        usage();
    }

    if (!dev->short_width) {
        fprintf(stderr, "Bad flex spec, missing short width!\n");
        usage();
    }

    if (dev->modulation != OOK_PULSE_MANCHESTER_ZEROBIT
            && dev->modulation != FSK_PULSE_MANCHESTER_ZEROBIT) {
        if (!dev->long_width) {
            fprintf(stderr, "Bad flex spec, missing long width!\n");
            usage();
        }
    }

    if (!dev->reset_limit) {
        fprintf(stderr, "Bad flex spec, missing reset limit!\n");
        usage();
    }

    if (dev->modulation == OOK_PULSE_DMC
            || dev->modulation == OOK_PULSE_PIWM_RAW
            || dev->modulation == OOK_PULSE_PIWM_DC) {
        if (!dev->tolerance) {
            fprintf(stderr, "Bad flex spec, missing tolerance limit!\n");
            usage();
        }
    }

    if (params->symbol_zero && !params->symbol_one) {
        fprintf(stderr, "Bad flex spec, symbol-one missing!\n");
        usage();
    }
    if (params->symbol_one && !params->symbol_zero) {
        fprintf(stderr, "Bad flex spec, symbol-zero missing!\n");
        usage();
    }

    /*
    if (decoder->verbose) {
        fprintf(stderr, "Adding flex decoder \"%s\"\n", params->name);
        fprintf(stderr, "\tmodulation=%u, short_width=%.0f, long_width=%.0f, reset_limit=%.0f\n",
                dev->modulation, dev->short_width, dev->long_width, dev->reset_limit);
        fprintf(stderr, "\tmin_rows=%u, min_bits=%u, min_repeats=%u, invert=%u, reflect=%u, match_len=%u, preamble_len=%u\n",
                params->min_rows, params->min_bits, params->min_repeats, params->invert, params->reflect, params->match_len, params->preamble_len);
    }
    */

    free(spec);
    return dev;
}
