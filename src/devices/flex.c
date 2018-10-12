/* Flexible general purpose decoder.
 *
 * Copyright (C) 2017 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "data.h"
#include "rtl_433.h"
#include "util.h"
#include "pulse_demod.h"
#include "optparse.h"

/// extract a number up to 32/64 bits from given offset with given bit length
unsigned long extract_number(uint8_t *data, unsigned bit_offset, unsigned bit_count)
{
    unsigned pos = bit_offset / 8;            // the first byte we need
    unsigned shl = bit_offset - pos * 8;      // shift left we need to align
    unsigned len = (shl + bit_count + 7) / 8; // number of bytes we need
    unsigned shr = 8 * len - shl - bit_count; // actual shift right
//    printf("pos: %d, shl: %d, len: %d, shr: %d\n", pos, shl, len, shr);
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
    unsigned count_only;
    unsigned match_len;
    bitrow_t match_bits;
    unsigned preamble_len;
    bitrow_t preamble_bits;
    struct flex_get getter[GETTER_SLOTS];
};

static int flex_callback(bitbuffer_t *bitbuffer, struct flex_params *params)
{
    int i;
    int match_count = 0;
    data_t *data;
    data_t *row_data[BITBUF_ROWS];
    char *row_codes[BITBUF_ROWS];
    char row_bytes[BITBUF_COLS * 2 + 1];
    char time_str[LOCAL_TIME_BUFLEN];
    bitrow_t tmp;

    // discard short / unwanted bitbuffers
    if ((bitbuffer->num_rows < params->min_rows)
            || (params->max_rows && bitbuffer->num_rows > params->max_rows))
        return 0;

    for (i = 0; i < bitbuffer->num_rows; i++) {
        if ((bitbuffer->bits_per_row[i] >= params->min_bits)
                && (!params->max_bits || bitbuffer->bits_per_row[i] <= params->max_bits))
            match_count++;
    }
    if (!match_count)
        return 0;

    // discard unless min_repeats, min_bits
    // TODO: check max_repeats, max_bits
    int r = bitbuffer_find_repeated_row(bitbuffer, params->min_repeats, params->min_bits);
    if (r < 0)
        return 0;
    // TODO: set match_count to count of repeated rows

    if (params->invert) {
        bitbuffer_invert(bitbuffer);
    }

    // discard unless match
    if (params->match_len) {
        match_count = 0;
        for (i = 0; i < bitbuffer->num_rows; i++) {
            if (bitbuffer_search(bitbuffer, i, 0, params->match_bits, params->match_len) < bitbuffer->bits_per_row[i]) {
                match_count++;
            }
        }
        if (!match_count)
            return 0;
    }

    // discard unless match, this should be an AND condition
    if (params->preamble_len) {
        match_count = 0;
        for (i = 0; i < bitbuffer->num_rows; i++) {
            unsigned pos = bitbuffer_search(bitbuffer, i, 0, params->preamble_bits, params->preamble_len);
            if (pos < bitbuffer->bits_per_row[i]) {
                match_count++;
                pos += params->preamble_len;
                unsigned len = bitbuffer->bits_per_row[i] - pos;
                bitbuffer_extract_bytes(bitbuffer, i, pos, tmp, len);
                memcpy(bitbuffer->bb[i], tmp, (len + 7) / 8);
                bitbuffer->bits_per_row[i] = len;
            }
        }
        if (!match_count)
            return 0;
    }

    if (debug_output >= 1) {
        fprintf(stderr, "%s: ", params->name);
        bitbuffer_print(bitbuffer);
    }

    local_time_str(0, time_str);
    if (params->count_only) {
        data = data_make(
                "time", "", DATA_STRING, time_str,
                "model", "", DATA_STRING, params->name,
                "count", "", DATA_INT, match_count,
                NULL);
        data_acquired_handler(data);

        return 0;
    }

    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = '\0';
        // print byte-wide
        for (int col = 0; col < (bitbuffer->bits_per_row[i] + 7) / 8; ++col) {
            sprintf(&row_bytes[2 * col], "%02x", bitbuffer->bb[i][col]);
        }
        // remove last nibble if needed
        row_bytes[2 * (bitbuffer->bits_per_row[i] + 3) / 8] = '\0';

        row_data[i] = data_make(
                "len", "", DATA_INT, bitbuffer->bits_per_row[i],
                "data", "", DATA_STRING, row_bytes,
                NULL);
        // add a data line for each getter
        for (int g = 0; g < GETTER_SLOTS && params->getter[g].bit_count > 0; ++g) {
            struct flex_get *getter = &params->getter[g];
            unsigned long val = extract_number(bitbuffer->bb[i], getter->bit_offset, getter->bit_count);
            if (getter->mask)
                val &= getter->mask;
            int m;
            for (m = 0; getter->map[m].val; m++) {
                if (getter->map[m].key == val) {
                    data_append(row_data[i],
                            getter->name, "", DATA_STRING, getter->map[m].val,
                            NULL);
                    break;
                }
            }
            if (!getter->map[m].val) {
                data_append(row_data[i],
                        getter->name, "", DATA_INT, val,
                        NULL);
            }
        }
        // a simpler representation for csv output
        row_codes[i] = malloc(5 + BITBUF_COLS * 2 + 1); // "{nnn}..\0"
        sprintf(row_codes[i], "{%d}%s", bitbuffer->bits_per_row[i], row_bytes);
    }
    data = data_make(
            "time", "", DATA_STRING, time_str,
            "model", "", DATA_STRING, params->name,
            "count", "", DATA_INT, match_count,
            "num_rows", "", DATA_INT, bitbuffer->num_rows,
            "rows", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_DATA, row_data),
            "codes", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_STRING, row_codes),
            NULL);
    data_acquired_handler(data);
    for (i = 0; i < bitbuffer->num_rows; i++) {
        free(row_codes[i]);
    }

    return 0;
}

static char *output_fields[] = {
        "time",
        "model",
        "count",
        "num_rows",
        "rows",
        "codes",
        NULL
};

static void usage()
{
    fprintf(stderr,
            "Use -X <spec> to add a general purpose decoder. For usage use -X help\n");
    exit(1);
}

static void help()
{
    fprintf(stderr,
            "Use -X <spec> to add a flexible general purpose decoder.\n\n"
            "<spec> is \"name:modulation:short:long:reset[,key=value...]\"\n"
            "where:\n"
            "<name> can be any descriptive name tag you need in the output\n"
            "<modulation> is one of:\n"
            "\tOOK_MC_ZEROBIT :  Manchester Code with fixed leading zero bit\n"
            "\tOOK_PCM :         Pulse Code Modulation (RZ or NRZ)\n"
            "\tOOK_PPM_RAW :     Pulse Position Modulation\n"
            "\tOOK_PWM :         Pulse Width Modulation\n"
            "\tOOK_DMC :         Differential Manchester Code\n"
            "\tOOK_PIWM_RAW :    Raw Pulse Interval and Width Modulation\n"
            "\tOOK_PIWM_DC :     Differential Pulse Interval and Width Modulation\n"
            "\tOOK_MC_OSV1 :     Manchester Code for OSv1 devices\n"
            "\tFSK_PCM :         FSK Pulse Code Modulation\n"
            "\tFSK_PWM_RAW :     FSK Pulse Width Modulation\n"
            "\tFSK_MC_ZEROBIT :  Manchester Code with fixed leading zero bit\n"
            "<short>, <long>, and <reset> are the timings for the decoder in µs\n"
            "PCM     short: Nominal width of pulse [us]\n"
            "         long: Nominal width of bit period [us]\n"
            "PPM_RAW short: Threshold between short and long gap [us]\n"
            "         long: Maximum gap size before new row of bits [us]\n"
            "PWM_RAW short: Threshold between short and long pulse [us]\n"
            "         long: Maximum gap size before new row of bits [us]\n"
            "PWM     short: Nominal width of '1' pulse [us]\n"
            "         long: Nominal width of '0' pulse [us]\n"
            "          gap: Maximum gap size before new row of bits [us]\n"
            "reset: Maximum gap size before End Of Message [us].\n"
            "for PWM use short:long:reset:gap[:tolerance[:syncwidth]]\n"
            "for DMC use short:long:reset:tolerance\n"
            "Available options are:\n"
            "\tdemod=<n> : the demod argument needed for some modulations\n"
            "\tbits=<n> : only match if at least one row has <n> bits\n"
            "\trows=<n> : only match if there are <n> rows\n"
            "\trepeats=<n> : only match if some row is repeated <n> times\n"
            "\t\tuse opt>=n to match at least <n> and opt<=n to match at most <n>\n"
            "\tinvert : invert all bits\n"
            "\tmatch=<bits> : only match if the <bits> are found\n"
            "\tpreamble=<bits> : match and align at the <bits> preamble\n"
            "\t\t<bits> is a row spec of {<bit count>}<bits as hex number>\n"
            "\tcountonly : suppress detailed row output\n\n"
            "E.g. -X \"doorbell:OOK_PWM_RAW:400:800:7000,match={24}0xa9878c,repeats>=3\"\n\n");
    exit(0);
}

#define FLEX_SLOTS 8
static struct flex_params *params_slot[FLEX_SLOTS];
static int cb_slot0(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[0]); }
static int cb_slot1(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[1]); }
static int cb_slot2(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[2]); }
static int cb_slot3(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[3]); }
static int cb_slot4(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[4]); }
static int cb_slot5(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[5]); }
static int cb_slot6(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[6]); }
static int cb_slot7(bitbuffer_t *bitbuffer) { return flex_callback(bitbuffer, params_slot[7]); }
static unsigned next_slot = 0;
int (*callback_slot[])(bitbuffer_t *bitbuffer) = {cb_slot0, cb_slot1, cb_slot2, cb_slot3, cb_slot4, cb_slot5, cb_slot6, cb_slot7};

static unsigned parse_bits(const char *code, bitrow_t bitrow)
{
    bitbuffer_t bits = {0};
    bitbuffer_parse(&bits, code);
    if (bits.num_rows != 1) {
        fprintf(stderr, "Bad flex spec, \"match\" needs exactly one bit row (%d found)!\n", bits.num_rows);
        usage();
    }
    memcpy(bitrow, bits.bb[0], sizeof(bitrow_t));
    return bits.bits_per_row[0];
}

const char *parse_map(const char *arg, struct flex_get *getter)
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
        while (*e != ' ' && *e != ']') e++;
        val = malloc(e - c + 1);
        memcpy(val, c, e - c);
        val[e - c] = '\0';
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
    bitrow_t bitrow;
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
        else
            getter->name = strdup(arg);
        arg = p;
    }
    if (debug_output)
        fprintf(stderr, "parse_getter() bit_offset: %d bit_count: %d mask: %lx name: %s\n",
                getter->bit_offset, getter->bit_count, getter->mask, getter->name);
}

r_device *flex_create_device(char *spec)
{
    if (next_slot >= FLEX_SLOTS) {
        fprintf(stderr, "Maximum number of flex decoders reached!\n");
        exit(1);
    }

    if (!spec || !*spec || *spec == '?' || !strncasecmp(spec, "help", strlen(spec))) {
        help();
    }

    struct flex_params *params = (struct flex_params *)calloc(1, sizeof(struct flex_params));
    params_slot[next_slot] = params;
    r_device *dev = (r_device *)calloc(1, sizeof(r_device));
    char *c, *o;
    int get_count = 0;

    spec = strdup(spec);
    // locate optional args and terminate mandatory args
    char *args = strchr(spec, ',');
    if (args) {
        *args++ = '\0';
    }

    c = strtok(spec, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad flex spec, missing name!\n");
        usage();
    }
    params->name = strdup(c);
    snprintf(dev->name, sizeof(dev->name), "General purpose decoder '%s'", c);

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad flex spec, missing modulation!\n");
        usage();
    }
    // TODO: add demod_arg where needed
    if (!strcasecmp(c, "OOK_MC_ZEROBIT"))
        dev->modulation = OOK_PULSE_MANCHESTER_ZEROBIT;
    else if (!strcasecmp(c, "OOK_PCM"))
        dev->modulation = OOK_PULSE_PCM_RZ;
    else if (!strcasecmp(c, "OOK_PPM_RAW"))
        dev->modulation = OOK_PULSE_PPM_RAW;
    else if (!strcasecmp(c, "OOK_PWM"))
        dev->modulation = OOK_PULSE_PWM_PRECISE;
    else if (!strcasecmp(c, "OOK_DMC"))
        dev->modulation = OOK_PULSE_DMC;
    else if (!strcasecmp(c, "OOK_PIWM_RAW"))
        dev->modulation = OOK_PULSE_PIWM_RAW;
    else if (!strcasecmp(c, "OOK_PIWM_DC"))
        dev->modulation = OOK_PULSE_PIWM_DC;
    else if (!strcasecmp(c, "OOK_MC_OSV1"))
        dev->modulation = OOK_PULSE_PWM_OSV1;
    else if (!strcasecmp(c, "FSK_PCM"))
        dev->modulation = FSK_PULSE_PCM;
    else if (!strcasecmp(c, "FSK_PWM_RAW"))
        dev->modulation = FSK_PULSE_PWM_RAW;
    else if (!strcasecmp(c, "FSK_MC_ZEROBIT"))
        dev->modulation = FSK_PULSE_MANCHESTER_ZEROBIT;
    else {
        fprintf(stderr, "Bad flex spec, unknown modulation!\n");
        usage();
    }

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad flex spec, missing short limit!\n");
        usage();
    }
    dev->short_limit = atoi(c);

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad flex spec, missing long limit!\n");
        usage();
    }
    dev->long_limit = atoi(c);

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad flex spec, missing reset limit!\n");
        usage();
    }
    dev->reset_limit = atoi(c);

    if (dev->modulation == OOK_PULSE_PWM_PRECISE) {
        c = strtok(NULL, ":");
        if (c == NULL) {
            fprintf(stderr, "Bad flex spec, missing gap limit!\n");
            usage();
        }
        dev->gap_limit = atoi(c);

        o = strtok(NULL, ":");
        if (o != NULL) {
            c = o;
            dev->tolerance = atoi(c);
        }

        o = strtok(NULL, ":");
        if (o != NULL) {
            c = o;
            dev->sync_width = atoi(c);
        }
    }

    if (dev->modulation == OOK_PULSE_DMC
            || dev->modulation == OOK_PULSE_PIWM_RAW
            || dev->modulation == OOK_PULSE_PIWM_DC) {
        c = strtok(NULL, ":");
        if (c == NULL) {
            fprintf(stderr, "Bad flex spec, missing tolerance limit!\n");
            usage();
        }
        dev->tolerance = atoi(c);
    }

    dev->json_callback = callback_slot[next_slot];
    dev->fields = output_fields;

    char *key, *val;
    while (getkwargs(&args, &key, &val)) {
        if (!strcasecmp(key, "demod"))
            dev->demod_arg = val ? atoi(val) : 0;

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

        else if (!strcasecmp(key, "match"))
            params->match_len = parse_bits(val, params->match_bits);

        else if (!strcasecmp(key, "preamble"))
            params->preamble_len = parse_bits(val, params->preamble_bits);

        else if (!strcasecmp(key, "countonly"))
            params->count_only = val ? atoi(val) : 1;

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

    if (debug_output >= 1) {
        fprintf(stderr, "Adding flex decoder \"%s\"\n", params->name);
        fprintf(stderr, "\tmodulation=%u, short_limit=%.0f, long_limit=%.0f, reset_limit=%.0f, demod_arg=%u\n",
                dev->modulation, dev->short_limit, dev->long_limit, dev->reset_limit, (unsigned)dev->demod_arg);
        fprintf(stderr, "\tmin_rows=%u, min_bits=%u, min_repeats=%u, invert=%u, match_len=%u, preamble_len=%u\n",
                params->min_rows, params->min_bits, params->min_repeats, params->invert, params->match_len, params->preamble_len);
    }

    free(spec);
    next_slot++;
    return dev;
}
