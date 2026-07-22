/** @file
    ELSYS ERS Series LoRaWAN indoor sensors.

    ELSYS application payload reference:
    https://elsys.se/public/app_notes/AppNote_ELSYS_uplink_payload.pdf
*/

#include "decoder.h"
#include "lorawan.h"

#include <stdlib.h>

typedef struct {
    lorawan_session_t lorawan;
    unsigned port;
} elsys_ers_ctx_t;

typedef struct {
    uint8_t const *value;
    uint32_t age;
    unsigned present;
} elsys_measurement_t;

static unsigned read_be16(uint8_t const *bytes)
{
    return bytes[0] << 8 | bytes[1];
}

static unsigned measurement_size(unsigned type)
{
    switch (type) {
        case 0x01: return 2;  /* Temperature */
        case 0x02: return 1;  /* Humidity */
        case 0x03: return 3;  /* Acceleration */
        case 0x04: return 2;  /* Light */
        case 0x05: return 1;  /* PIR motion events */
        case 0x06: return 2;  /* CO2 */
        case 0x07: return 2;  /* Internal battery voltage */
        case 0x08: return 2;  /* Analog 1 */
        case 0x09: return 6;  /* Deprecated GPS */
        case 0x0a: return 2;  /* Relative pulse count 1 */
        case 0x0b: return 4;  /* Absolute pulse count 1 */
        case 0x0c: return 2;  /* External temperature 1 */
        case 0x0d: return 1;  /* External digital 1 */
        case 0x0e: return 2;  /* External distance */
        case 0x0f: return 1;  /* Acceleration events */
        case 0x10: return 4;  /* External IR temperatures */
        case 0x11: return 1;  /* Occupancy */
        case 0x12: return 1;  /* External water leak */
        case 0x13: return 65; /* Room IR temperature matrix */
        case 0x14: return 4;  /* Pressure */
        case 0x15: return 2;  /* Peak and average sound */
        case 0x16: return 2;  /* Relative pulse count 2 */
        case 0x17: return 4;  /* Absolute pulse count 2 */
        case 0x18: return 2;  /* Analog 2 */
        case 0x19: return 2;  /* External temperature 2 */
        case 0x1a: return 1;  /* External digital 2 */
        case 0x1b: return 4;  /* External analog in microvolts */
        case 0x1c: return 2;  /* VOC */
        case 0x3d: return 4;  /* Debug information */
        default: return 0;
    }
}

static int is_ers_measurement(unsigned type)
{
    return type == 0x01 || type == 0x02 || type == 0x04 || type == 0x05
            || type == 0x06 || type == 0x07 || type == 0x11
            || type == 0x13 || type == 0x15 || type == 0x1c;
}

static int parse_measurements(uint8_t const *payload, unsigned len,
        elsys_measurement_t measurements[64], unsigned *common_age,
        int *has_common_age)
{
    unsigned recognized = 0;
    for (unsigned offset = 0; offset < len;) {
        uint8_t const tag = payload[offset++];
        unsigned const type = tag & 0x3f;
        unsigned const timestamp_code = tag >> 6;
        static unsigned const timestamp_sizes[4] = {0, 1, 2, 4};
        unsigned const value_size = measurement_size(type);
        unsigned const timestamp_size = timestamp_sizes[timestamp_code];
        if (!value_size || value_size + timestamp_size > len - offset) {
            return 0;
        }
        uint8_t const *value = &payload[offset];
        offset += value_size;
        uint32_t age = 0;
        for (unsigned i = 0; i < timestamp_size; ++i) {
            age = age << 8 | payload[offset++];
        }
        if (!measurements[type].present || age < measurements[type].age) {
            measurements[type].value = value;
            measurements[type].age = age;
            measurements[type].present = 1;
        }
        if (is_ers_measurement(type)) {
            recognized += 1;
            if (!*has_common_age) {
                *common_age = age;
                *has_common_age = 1;
            }
            else if (*common_age != age) {
                *has_common_age = -1;
            }
        }
    }
    return recognized > 0;
}

static int lorawan_result_to_decode(int result)
{
    if (result == LORAWAN_ERROR_LENGTH) {
        return DECODE_ABORT_LENGTH;
    }
    if (result == LORAWAN_ERROR_MIC) {
        return DECODE_FAIL_MIC;
    }
    return DECODE_ABORT_EARLY;
}

static int elsys_ers_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] % 8) {
        return DECODE_ABORT_LENGTH;
    }
    unsigned const len = bitbuffer->bits_per_row[0] / 8;
    elsys_ers_ctx_t *ctx = decoder_user_data(decoder);
    lorawan_frame_t frame;
    int const result = lorawan_parse_frame(&ctx->lorawan,
            bitbuffer->bb[0], len, &frame);
    if (result != LORAWAN_OK) {
        return lorawan_result_to_decode(result);
    }
    if ((frame.message_type != 2 && frame.message_type != 4)
            || !frame.has_fport || frame.fport != ctx->port
            || !frame.frm_payload_decrypted || !frame.frm_payload_len) {
        return DECODE_ABORT_EARLY;
    }

    elsys_measurement_t measurements[64] = {{0}};
    unsigned common_age = 0;
    int has_common_age = 0;
    if (!parse_measurements(frame.frm_payload, frame.frm_payload_len,
                measurements, &common_age, &has_common_age)) {
        return DECODE_ABORT_EARLY;
    }
    if ((measurements[0x02].present && measurements[0x02].value[0] > 100)
            || (measurements[0x06].present && read_be16(measurements[0x06].value) > 10000)
            || (measurements[0x11].present && measurements[0x11].value[0] > 2)
            || (measurements[0x1c].present && read_be16(measurements[0x1c].value) > 60000)) {
        return DECODE_FAIL_SANITY;
    }

    char const *model = "ELSYS-ERS";
    if (measurements[0x13].present || measurements[0x11].present) {
        model = "ELSYS-ERS-Eye";
    }
    else if (measurements[0x1c].present) {
        model = "ELSYS-ERS-VOC";
    }
    else if (measurements[0x15].present) {
        model = "ELSYS-ERS-Sound";
    }
    else if (measurements[0x06].present) {
        model = "ELSYS-ERS-CO2";
    }

    char dev_addr[9];
    char counter[11];
    lorawan_format_reverse(dev_addr, &bitbuffer->bb[0][1], 4);
    snprintf(counter, sizeof(counter), "%u", frame.fcnt);
    data_t *data = data_make(
            "model",    "",        DATA_STRING, model,
            "id",       "DevAddr", DATA_STRING, dev_addr,
            "fport",    "FPort",   DATA_INT, frame.fport,
            "fcnt",     "FCnt",    DATA_STRING, counter,
            NULL);
    if (measurements[0x01].present) {
        int16_t const raw = (int16_t)read_be16(measurements[0x01].value);
        float const value = raw * 0.1f;
        data = data_dbl(data, "temperature_C", "Temperature", "%.1f C", (double)value);
    }
    if (measurements[0x02].present) {
        data = data_int(data, "humidity", "Humidity", "%d %%",
                measurements[0x02].value[0]);
    }
    if (measurements[0x04].present) {
        data = data_int(data, "light_lux", "Light", "%d lux",
                read_be16(measurements[0x04].value));
    }
    if (measurements[0x05].present) {
        data = data_int(data, "motion_count", "PIR motion events", NULL,
                measurements[0x05].value[0]);
    }
    if (measurements[0x06].present) {
        data = data_int(data, "co2_ppm", "Carbon dioxide", "%d ppm",
                read_be16(measurements[0x06].value));
    }
    if (measurements[0x07].present) {
        data = data_int(data, "battery_mV", "Battery voltage", "%d mV",
                read_be16(measurements[0x07].value));
    }
    if (measurements[0x11].present) {
        data = data_int(data, "occupancy", "Occupancy", NULL,
                measurements[0x11].value[0]);
    }
    if (measurements[0x13].present) {
        uint8_t const *value = measurements[0x13].value;
        unsigned minimum = value[1];
        unsigned maximum = value[1];
        for (unsigned i = 2; i < 65; ++i) {
            if (value[i] < minimum) minimum = value[i];
            if (value[i] > maximum) maximum = value[i];
        }
        float const min_temp = value[0] + minimum * 0.1f;
        float const max_temp = value[0] + maximum * 0.1f;
        data = data_dbl(data, "room_ir_min_C", "Room IR minimum", "%.1f C",
                (double)min_temp);
        data = data_dbl(data, "room_ir_max_C", "Room IR maximum", "%.1f C",
                (double)max_temp);
    }
    if (measurements[0x15].present) {
        data = data_int(data, "sound_peak_dB", "Peak sound", "%d dB",
                measurements[0x15].value[0]);
        data = data_int(data, "sound_avg_dB", "Average sound", "%d dB",
                measurements[0x15].value[1]);
    }
    if (measurements[0x1c].present) {
        data = data_int(data, "voc_ppb", "Volatile organic compounds", "%d ppb",
                read_be16(measurements[0x1c].value));
    }
    if (has_common_age > 0 && common_age) {
        data = data_int(data, "age_s", "Measurement age", "%d s", common_age);
    }
    if (frame.mic_verified) {
        data = data_str(data, "mic", "Integrity", NULL, "CMAC");
    }
    decoder_output_data(decoder, data);
    return 1;
}

r_device const elsys_ers;

static char *elsys_strtok(char *text, char const *delimiter, char **saveptr)
{
#ifdef _MSC_VER
    return strtok_s(text, delimiter, saveptr);
#else
    return strtok_r(text, delimiter, saveptr);
#endif
}

static int parse_port(char const *value, unsigned *port)
{
    char *end;
    unsigned long const parsed = strtoul(value, &end, 0);
    if (!*value || *end || parsed < 1 || parsed > 254) {
        return 0;
    }
    *port = (unsigned)parsed;
    return 1;
}

static r_device *elsys_ers_create(char const *args)
{
    r_device *dev = decoder_create(&elsys_ers, sizeof(elsys_ers_ctx_t));
    if (!dev) {
        return NULL;
    }
    elsys_ers_ctx_t *ctx = decoder_user_data(dev);
    ctx->port = 5;
    if (!args || !*args) {
        return dev;
    }
    char *work = strdup(args);
    if (!work) {
        return dev;
    }
    char *saveptr = NULL;
    for (char *token = elsys_strtok(work, ",", &saveptr); token;
            token = elsys_strtok(NULL, ",", &saveptr)) {
        char *value = strchr(token, '=');
        if (!value) {
            fprintf(stderr, "ELSYS ERS: expected key=value option \"%s\"\n", token);
            continue;
        }
        *value++ = '\0';
        int parsed;
        if (!strcasecmp(token, "port")) {
            parsed = parse_port(value, &ctx->port) ? 1 : -1;
        }
        else {
            parsed = lorawan_session_set_option(&ctx->lorawan, token, value);
        }
        if (parsed == 0) {
            fprintf(stderr, "ELSYS ERS: unknown option \"%s\"\n", token);
        }
        else if (parsed < 0) {
            fprintf(stderr, "ELSYS ERS: invalid value for \"%s\"\n", token);
        }
    }
    free(work);
    return dev;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "fport",
        "fcnt",
        "temperature_C",
        "humidity",
        "light_lux",
        "motion_count",
        "co2_ppm",
        "battery_mV",
        "occupancy",
        "room_ir_min_C",
        "room_ir_max_C",
        "sound_peak_dB",
        "sound_avg_dB",
        "voc_ppb",
        "age_s",
        "mic",
        NULL,
};

r_device const elsys_ers = {
        .name = "ELSYS ERS Series LoRaWAN sensors",
        .modulation = LORA,
        .lora_sync_word = 0x34,
        .decode_fn = &elsys_ers_decode,
        .create_fn = &elsys_ers_create,
        .fields = output_fields,
};
