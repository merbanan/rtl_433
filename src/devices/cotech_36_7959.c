#include "decoder.h"

#define NUM_BITS 112

static unsigned parse_bits(const char *code, bitrow_t bitrow)
{
    bitbuffer_t bits = {0};
    bitbuffer_parse(&bits, code);
    if (bits.num_rows != 1) {
        fprintf(stderr, "Bad flex spec, \"match\" needs exactly one bit row (%d found)!\n", bits.num_rows);
    }
    memcpy(bitrow, bits.bb[0], sizeof(bitrow_t));
    return bits.bits_per_row[0];
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

static int cotech_36_7959_decode(r_device *decoder, bitbuffer_t *bitbuffer){
    if (decoder->verbose > 1) {
        fprintf(stderr, "%s: Decode starting\n", __func__);

        fprintf(stderr, "Decoder settings \"%s\"\n", decoder->name);
        fprintf(stderr, "\tmodulation=%u, short_width=%.0f, long_width=%.0f, reset_limit=%.0f\n",
                decoder->modulation, decoder->short_width, decoder->long_width, decoder->reset_limit);

        fprintf(stderr, "%s: Nr. of rows: %d\n", __func__, bitbuffer->num_rows);
        fprintf(stderr, "%s: Bits per row: %d\n", __func__, bitbuffer->bits_per_row[0]);
    }

    if (bitbuffer->num_rows > 2 || bitbuffer->bits_per_row[0] < NUM_BITS) {
        if (decoder->verbose > 1) 
            fprintf(stderr, "%s: Aborting because of short bit length or too few rows\n", __func__);

        return DECODE_ABORT_EARLY;
    }

    int i;
    unsigned pos;
    unsigned len;
    int match_count = 0;
    int r = -1;
    bitrow_t tmp;
    data_t *data;
    char *preamble = "{12}014";
    bitrow_t preamble_bits;
    unsigned preamble_len;

    preamble_len = parse_bits(preamble, preamble_bits);

    // for(i = 0; i < preamble_bits; i++){
    //     fprintf(stderr, "%u", preamble_bits[i]);
    // }

    // int r = bitbuffer_find_repeated_row(bitbuffer, 0, 0);

    if (decoder->verbose > 1) {
        fprintf(stderr, "%s: Nr. of repeated rows: %d\n", __func__, r);
        fprintf(stderr, "%s: preamble len: %d\n", __func__, preamble_len);
        // fprintf(stderr, "%s: preamble_bits: %x\n", __func__, preamble_bits);
    }

    for (i = 0; i < bitbuffer->num_rows; i++) {
        unsigned pos = bitbuffer_search(bitbuffer, i, 0, preamble_bits, preamble_len);

        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Bitbuffer length: %d\n", __func__, bitbuffer->bits_per_row[i]);
            fprintf(stderr, "%s: Pos: %d\n", __func__, pos);
        }

        if (pos < bitbuffer->bits_per_row[i]) {
            if (r < 0)
                r = i;
            match_count++;
            pos += preamble_len;
            unsigned len = bitbuffer->bits_per_row[i] - pos;
            bitbuffer_extract_bytes(bitbuffer, i, pos, tmp, len);
            memcpy(bitbuffer->bb[i], tmp, (len + 7) / 8);
            bitbuffer->bits_per_row[i] = len;
        }
    }

    if (!match_count){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Couldn't find any match: %d\n", __func__, match_count);
        }   
        return DECODE_FAIL_SANITY;
    }

    //We're looking for a 112 bit message
    if(bitbuffer->bits_per_row[0] != NUM_BITS){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Wrong bits per row: %d\n", __func__, bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    //Check CRC8: poly=0x31  init=0xc0  refin=false  refout=false  xorout=0x00  check=0x0d  residue=0x00
    if(crc8(bitbuffer->bb[0], NUM_BITS/8, 0x31, 0xc0)){
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: CRC8 fail: %u\n", __func__, crc8(bitbuffer->bb[0], 8, 0x31, 0xc0));
        }

        return DECODE_FAIL_MIC;
    }

    // bitrow_t row = bitbuffer->bb[0];
    //Extract data from buffer
    //int type_code = extract_number(bitbuffer->bb[0], 0, 4); //Not sure about this
    int id = extract_number(bitbuffer->bb[0], 4, 8); //Not sure about this, changes on battery change or when reset
    int batt_low = extract_number(bitbuffer->bb[0], 12, 1);
    int deg_loop = extract_number(bitbuffer->bb[0], 13, 1);
    int gust_loop = extract_number(bitbuffer->bb[0], 14, 1);
    int wind_loop = extract_number(bitbuffer->bb[0], 15, 1);
    int wind = extract_number(bitbuffer->bb[0], 16, 8);
    int gust = extract_number(bitbuffer->bb[0], 24, 8);
    int wind_dir = extract_number(bitbuffer->bb[0], 32, 8);
    //int ?? = extract_number(bitbuffer->bb[0], 40, 4);
    int rain = extract_number(bitbuffer->bb[0], 44, 12);
    //int ?? = extract_number(bitbuffer->bb[0], 56, 4);
    int temp_raw = extract_number(bitbuffer->bb[0], 60, 12);
    int humidity = extract_number(bitbuffer->bb[0], 72, 8);
    //int ?? = extract_number(bitbuffer->bb[0], 80, 24);

    data = data_make(
        "model",                                "",                 DATA_STRING, "Cotech 36-7959 wireless weather station with USB",
        //"type_code",                            "Type code",        DATA_INT, type_code,
        "id",                                   "ID",               DATA_INT, id,
        "battery_ok",                           "Battery",          DATA_INT, !batt_low,
        "temperature_F",                        "Temperature",      DATA_FORMAT, "%.1f", DATA_DOUBLE, ((temp_raw - 400) / 10.0f),
        "humidity",                             "Humidity",         DATA_INT, humidity,
        _X("rain_mm", "rain"),                  "Rain",             DATA_FORMAT, "%.1f", DATA_DOUBLE, rain / 10.0f,
        _X("wind_dir_deg", "wind_direction"),   "Wind direction",   DATA_INT, deg_loop?255+wind_dir:wind_dir,
        _X("wind_avg_m_s", "wind_speed_ms"),    "Wind",             DATA_FORMAT, "%.1f", DATA_DOUBLE, (wind_loop?255+wind:wind) / 10.0f,
        _X("wind_max_m_s", "gust_speed_ms"),    "Gust",             DATA_FORMAT, "%.1f", DATA_DOUBLE, (gust_loop?255+gust:gust) / 10.0f,
        "mic",                                  "Integrity",        DATA_STRING, "CRC",
        NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *cotech_36_7959_output_fields[] = {
    "model",
    //"type_code",
    "id",
    "battery_ok",
    "temperature_F",
    "humidity",
    "rain_mm",
    "wind_dir_deg",
    "wind_avg_m_s",
    "wind_max_m_s",
    "mic",
    NULL
};

r_device cotech_36_7959 = {
    .name           = "Cotech 36-7959 wireless weather station with USB",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width    = 488,
    .long_width     = 0, //Not used
    .reset_limit    = 1200,
    .decode_fn      = &cotech_36_7959_decode,
    .disabled       = 0,
    .fields         = cotech_36_7959_output_fields,
};
