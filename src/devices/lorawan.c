/** @file
    Generic LoRaWAN 1.0.x MAC frame reporter.
*/

#include "decoder.h"
#include "lorawan.h"

#include <stdlib.h>

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

static int lorawan_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] % 8) {
        return DECODE_ABORT_LENGTH;
    }
    unsigned const len = bitbuffer->bits_per_row[0] / 8;
    uint8_t const *b = bitbuffer->bb[0];
    lorawan_session_t *session = decoder_user_data(decoder);
    lorawan_frame_t frame;
    int const result = lorawan_parse_frame(session, b, len, &frame);
    if (result != LORAWAN_OK) {
        return lorawan_result_to_decode(result);
    }

    char hex[2 * LORAWAN_MAX_FRAME_LEN + 1];
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "LoRaWAN",
            "message_type", "Message type", DATA_STRING,
                    lorawan_message_type_name(frame.message_type),
            "major",        "Major",        DATA_INT, frame.major,
            NULL);

    if (frame.message_type == 0) {
        char join_eui[17];
        char dev_eui[17];
        lorawan_format_reverse(join_eui, frame.join_eui, 8);
        lorawan_format_reverse(dev_eui, frame.dev_eui, 8);
        data = data_str(data, "join_eui", "JoinEUI", NULL, join_eui);
        data = data_str(data, "dev_eui", "DevEUI", NULL, dev_eui);
        data = data_int(data, "dev_nonce", "DevNonce", NULL, frame.dev_nonce);
    }
    else if (frame.message_type == 1) {
        if (!frame.join_accept_decrypted) {
            data = data_hex(data, "encrypted", "Encrypted join accept", NULL,
                    frame.join_accept_encrypted, frame.join_accept_encrypted_len, hex);
        }
        else {
            char join_nonce[7];
            char net_id[7];
            char dev_addr[9];
            lorawan_format_reverse(join_nonce, &frame.join_accept[0], 3);
            lorawan_format_reverse(net_id, &frame.join_accept[3], 3);
            lorawan_format_reverse(dev_addr, &frame.join_accept[6], 4);
            data = data_str(data, "join_nonce", "JoinNonce", NULL, join_nonce);
            data = data_str(data, "net_id", "NetID", NULL, net_id);
            data = data_str(data, "dev_addr", "DevAddr", NULL, dev_addr);
            data = data_int(data, "dl_settings", "DLSettings", "0x%02x",
                    frame.join_accept[10]);
            data = data_int(data, "rx_delay", "RXDelay", NULL,
                    frame.join_accept[11]);
            if (frame.len == 33) {
                data = data_hex(data, "cf_list", "CFList", NULL,
                        &frame.join_accept[12], 16, hex);
            }
        }
    }
    else if (frame.message_type >= 2 && frame.message_type <= 5) {
        char dev_addr[9];
        char counter[11];
        lorawan_format_reverse(dev_addr, &b[1], 4);
        snprintf(counter, sizeof(counter), "%u", frame.fcnt);
        data = data_str(data, "dev_addr", "DevAddr", NULL, dev_addr);
        data = data_int(data, "fctrl", "FCtrl", "0x%02x", frame.fctrl);
        data = data_int(data, "fcnt", "FCnt", NULL, b[6] | b[7] << 8);
        data = data_str(data, "fcnt_full", "Full FCnt", NULL, counter);
        if (frame.fopts_len) {
            data = data_hex(data, "fopts", "FOpts", NULL,
                    frame.fopts, frame.fopts_len, hex);
        }
        if (frame.has_fport) {
            data = data_int(data, "fport", "FPort", NULL, frame.fport);
            if (frame.frm_payload_len) {
                data = data_hex(data, "frm_payload_encrypted", "Encrypted FRMPayload",
                        NULL, frame.frm_payload_encrypted, frame.frm_payload_len, hex);
                if (frame.frm_payload_decrypted) {
                    data = data_hex(data, "frm_payload", "Decrypted FRMPayload",
                            NULL, frame.frm_payload, frame.frm_payload_len, hex);
                }
            }
        }
    }
    else {
        data = data_hex(data, "payload", "Payload", NULL,
                frame.payload, frame.payload_len, hex);
    }

    if (frame.mic_verified) {
        data = data_str(data, "mic", "Integrity", NULL, "CMAC");
    }
    else if (frame.mic) {
        data = data_hex(data, "mic_raw", "MIC (unverified)", NULL,
                frame.mic, 4, hex);
    }
    decoder_output_data(decoder, data);
    return 1;
}

r_device const lorawan;

static char *lorawan_strtok(char *text, char const *delimiter, char **saveptr)
{
#ifdef _MSC_VER
    return strtok_s(text, delimiter, saveptr);
#else
    return strtok_r(text, delimiter, saveptr);
#endif
}

static r_device *lorawan_create(char const *args)
{
    r_device *dev = decoder_create(&lorawan, sizeof(lorawan_session_t));
    if (!dev || !args || !*args) {
        return dev;
    }
    lorawan_session_t *session = decoder_user_data(dev);
    char *work = strdup(args);
    if (!work) {
        return dev;
    }
    char *saveptr = NULL;
    for (char *token = lorawan_strtok(work, ",", &saveptr); token;
            token = lorawan_strtok(NULL, ",", &saveptr)) {
        char *value = strchr(token, '=');
        if (!value) {
            fprintf(stderr, "LoRaWAN: expected key=value option \"%s\"\n", token);
            continue;
        }
        *value++ = '\0';
        int const parsed = lorawan_session_set_option(session, token, value);
        if (parsed == 0) {
            fprintf(stderr, "LoRaWAN: unknown option \"%s\"\n", token);
        }
        else if (parsed < 0) {
            fprintf(stderr, "LoRaWAN: invalid value for \"%s\"\n", token);
        }
    }
    free(work);
    return dev;
}

static char const *const output_fields[] = {
        "model",
        "message_type",
        "major",
        "join_eui",
        "dev_eui",
        "dev_nonce",
        "join_nonce",
        "net_id",
        "dev_addr",
        "dl_settings",
        "rx_delay",
        "cf_list",
        "fctrl",
        "fcnt",
        "fcnt_full",
        "fopts",
        "fport",
        "frm_payload_encrypted",
        "frm_payload",
        "encrypted",
        "payload",
        "mic_raw",
        "mic",
        NULL,
};

r_device const lorawan = {
        .name = "LoRaWAN 1.0.x generic MAC frame",
        .modulation = LORA,
        .lora_sync_word = 0x34,
        .decode_fn = &lorawan_decode,
        .create_fn = &lorawan_create,
        .fields = output_fields,
};
