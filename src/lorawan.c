/** @file
    LoRaWAN 1.0.x MAC parsing, integrity, and decryption.
*/

#include "lorawan.h"
#include "aes.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static unsigned hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return (unsigned)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (unsigned)(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return (unsigned)(value - 'A' + 10);
    }
    return 16;
}

static int parse_key(char const *text, uint8_t key[16])
{
    if (!text || strlen(text) != 32) {
        return 0;
    }
    for (unsigned i = 0; i < 16; ++i) {
        unsigned const high = hex_nibble(text[2 * i]);
        unsigned const low = hex_nibble(text[2 * i + 1]);
        if (high > 15 || low > 15) {
            return 0;
        }
        key[i] = (uint8_t)(high << 4 | low);
    }
    return 1;
}

static int ascii_equal(char const *left, char const *right)
{
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') {
            a += 'a' - 'A';
        }
        if (b >= 'A' && b <= 'Z') {
            b += 'a' - 'A';
        }
        if (a != b) {
            return 0;
        }
    }
    return *left == *right;
}

static int parse_u32(char const *text, uint32_t *value, int base)
{
    char *end;
    unsigned long const parsed = strtoul(text, &end, base);
    if (!*text || *end || parsed > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

void lorawan_session_init(lorawan_session_t *session)
{
    if (session) {
        memset(session, 0, sizeof(*session));
    }
}

int lorawan_session_set_option(lorawan_session_t *session,
        char const *key, char const *value)
{
    if (!session || !key || !value) {
        return -1;
    }
    int valid;
    if (ascii_equal(key, "nwkskey")) {
        valid = parse_key(value, session->nwkskey);
        session->has_nwkskey = valid;
    }
    else if (ascii_equal(key, "appskey")) {
        valid = parse_key(value, session->appskey);
        session->has_appskey = valid;
    }
    else if (ascii_equal(key, "appkey")) {
        valid = parse_key(value, session->appkey);
        session->has_appkey = valid;
    }
    else if (ascii_equal(key, "devaddr")) {
        valid = parse_u32(value, &session->dev_addr, 16);
        session->has_dev_addr = valid;
    }
    else if (ascii_equal(key, "fcnt") || ascii_equal(key, "fcnt_up")) {
        valid = parse_u32(value, &session->fcnt_up, 0);
        session->has_fcnt_up = valid;
    }
    else if (ascii_equal(key, "fcnt_down")) {
        valid = parse_u32(value, &session->fcnt_down, 0);
        session->has_fcnt_down = valid;
    }
    else {
        return 0;
    }
    return valid ? 1 : -1;
}

void lorawan_format_reverse(char *out, uint8_t const *bytes, unsigned len)
{
    static char const hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < len; ++i) {
        uint8_t const value = bytes[len - 1 - i];
        out[2 * i] = hex[value >> 4];
        out[2 * i + 1] = hex[value & 0x0f];
    }
    out[2 * len] = '\0';
}

char const *lorawan_message_type_name(unsigned message_type)
{
    static char const *const names[8] = {
            "Join Request", "Join Accept", "Unconfirmed Data Up", "Unconfirmed Data Down",
            "Confirmed Data Up", "Confirmed Data Down", "Rejoin Request", "Proprietary",
    };
    return message_type < 8 ? names[message_type] : "Unknown";
}

static uint32_t load_le32(uint8_t const bytes[4])
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8
            | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void store_le32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int mic_matches(uint8_t const calculated[16], uint8_t const received[4])
{
    uint8_t difference = 0;
    for (unsigned i = 0; i < 4; ++i) {
        difference |= calculated[i] ^ received[i];
    }
    return difference == 0;
}

static uint32_t expand_counter(uint32_t previous, int initialized, uint16_t low)
{
    if (!initialized) {
        return low;
    }
    uint32_t candidate = (previous & 0xffff0000U) | low;
    if (candidate < previous && previous - candidate > 0x8000U) {
        candidate += 0x10000U;
    }
    else if (candidate > previous && candidate - previous > 0x8000U
            && candidate >= 0x10000U) {
        candidate -= 0x10000U;
    }
    return candidate;
}

static void crypt_payload(uint8_t const key[16], uint8_t const dev_addr[4],
        uint32_t counter, unsigned direction, uint8_t const *input,
        unsigned len, uint8_t *output)
{
    uint8_t block[16] = {0};
    uint8_t stream[16];
    block[0] = 0x01;
    block[5] = (uint8_t)direction;
    memcpy(&block[6], dev_addr, 4);
    store_le32(&block[10], counter);
    for (unsigned offset = 0, index = 1; offset < len; offset += 16, ++index) {
        block[15] = (uint8_t)index;
        aes128_encrypt_block(key, block, stream);
        unsigned const block_len = len - offset < 16 ? len - offset : 16;
        for (unsigned i = 0; i < block_len; ++i) {
            output[offset + i] = input[offset + i] ^ stream[i];
        }
    }
}

static void calculate_data_mic(uint8_t const key[16], uint8_t const *message,
        unsigned message_len, uint8_t const dev_addr[4], uint32_t counter,
        unsigned direction, uint8_t mic[16])
{
    uint8_t input[16 + LORAWAN_MAX_FRAME_LEN] = {0};
    input[0] = 0x49;
    input[5] = (uint8_t)direction;
    memcpy(&input[6], dev_addr, 4);
    store_le32(&input[10], counter);
    input[15] = (uint8_t)message_len;
    memcpy(&input[16], message, message_len);
    aes128_cmac(key, input, 16 + message_len, mic);
}

static int verify_join_mic(uint8_t const key[16], uint8_t const *message,
        unsigned message_len, uint8_t const received[4])
{
    uint8_t mic[16];
    aes128_cmac(key, message, message_len, mic);
    return mic_matches(mic, received);
}

static void derive_session_key(uint8_t const appkey[16], uint8_t type,
        uint8_t const join_accept[16], uint16_t dev_nonce, uint8_t key[16])
{
    uint8_t input[16] = {0};
    input[0] = type;
    memcpy(&input[1], join_accept, 6);
    input[7] = (uint8_t)dev_nonce;
    input[8] = (uint8_t)(dev_nonce >> 8);
    aes128_encrypt_block(appkey, input, key);
}

static int parse_join_request(lorawan_session_t *session, uint8_t const *bytes,
        unsigned len, lorawan_frame_t *frame)
{
    if (len != 23) {
        return LORAWAN_ERROR_LENGTH;
    }
    frame->join_eui = &bytes[1];
    frame->dev_eui = &bytes[9];
    frame->dev_nonce = bytes[17] | bytes[18] << 8;
    frame->mic = &bytes[len - 4];
    if (session->has_appkey) {
        if (!verify_join_mic(session->appkey, bytes, len - 4, frame->mic)) {
            return LORAWAN_ERROR_MIC;
        }
        frame->mic_verified = 1;
        session->dev_nonce = frame->dev_nonce;
        session->has_dev_nonce = 1;
    }
    return LORAWAN_OK;
}

static int parse_join_accept(lorawan_session_t *session, uint8_t const *bytes,
        unsigned len, lorawan_frame_t *frame)
{
    if (len != 17 && len != 33) {
        return LORAWAN_ERROR_LENGTH;
    }
    frame->join_accept_encrypted = &bytes[1];
    frame->join_accept_encrypted_len = len - 1;
    if (!session->has_appkey) {
        return LORAWAN_OK;
    }
    for (unsigned offset = 0; offset < len - 1; offset += 16) {
        aes128_encrypt_block(session->appkey, &bytes[1 + offset],
                &frame->join_accept[offset]);
    }
    uint8_t mic_input[1 + 28];
    mic_input[0] = bytes[0];
    memcpy(&mic_input[1], frame->join_accept, len - 5);
    frame->mic = &frame->join_accept[len - 5];
    if (!verify_join_mic(session->appkey, mic_input, len - 4, frame->mic)) {
        return LORAWAN_ERROR_MIC;
    }
    frame->join_accept_decrypted = 1;
    frame->mic_verified = 1;
    session->dev_addr = load_le32(&frame->join_accept[6]);
    session->has_dev_addr = 1;
    if (session->has_dev_nonce) {
        derive_session_key(session->appkey, 0x01, frame->join_accept,
                session->dev_nonce, session->nwkskey);
        derive_session_key(session->appkey, 0x02, frame->join_accept,
                session->dev_nonce, session->appskey);
        session->has_nwkskey = 1;
        session->has_appskey = 1;
        session->has_fcnt_up = 0;
        session->has_fcnt_down = 0;
    }
    return LORAWAN_OK;
}

static int parse_data_frame(lorawan_session_t *session, uint8_t const *bytes,
        unsigned len, lorawan_frame_t *frame)
{
    if (len < 12) {
        return LORAWAN_ERROR_LENGTH;
    }
    frame->dev_addr = load_le32(&bytes[1]);
    if (session->has_dev_addr && frame->dev_addr != session->dev_addr) {
        return LORAWAN_ERROR_DEVICE;
    }
    frame->fctrl = bytes[5];
    frame->fopts_len = bytes[5] & 0x0f;
    frame->fopts = &bytes[8];
    unsigned const payload_end = len - 4;
    unsigned const port_offset = 8 + frame->fopts_len;
    if (port_offset > payload_end) {
        return LORAWAN_ERROR_LENGTH;
    }
    frame->direction = frame->message_type == 3 || frame->message_type == 5;
    uint16_t const counter_low = bytes[6] | bytes[7] << 8;
    uint32_t *counter_state = frame->direction
            ? &session->fcnt_down : &session->fcnt_up;
    int const counter_valid = frame->direction
            ? session->has_fcnt_down : session->has_fcnt_up;
    frame->fcnt = expand_counter(*counter_state, counter_valid, counter_low);
    frame->mic = &bytes[len - 4];
    if (session->has_nwkskey) {
        uint8_t mic[16];
        calculate_data_mic(session->nwkskey, bytes, len - 4, &bytes[1],
                frame->fcnt, frame->direction, mic);
        if (!mic_matches(mic, frame->mic)) {
            return LORAWAN_ERROR_MIC;
        }
        frame->mic_verified = 1;
        *counter_state = frame->fcnt;
        if (frame->direction) {
            session->has_fcnt_down = 1;
        }
        else {
            session->has_fcnt_up = 1;
        }
    }
    if (port_offset < payload_end) {
        frame->has_fport = 1;
        frame->fport = bytes[port_offset];
        frame->frm_payload_encrypted = &bytes[port_offset + 1];
        frame->frm_payload_len = payload_end - port_offset - 1;
        uint8_t const *key = frame->fport == 0
                ? (session->has_nwkskey ? session->nwkskey : NULL)
                : (session->has_appskey ? session->appskey : NULL);
        if (key && frame->frm_payload_len) {
            crypt_payload(key, &bytes[1], frame->fcnt, frame->direction,
                    frame->frm_payload_encrypted, frame->frm_payload_len,
                    frame->frm_payload);
            frame->frm_payload_decrypted = 1;
        }
    }
    return LORAWAN_OK;
}

int lorawan_parse_frame(lorawan_session_t *session, uint8_t const *bytes,
        unsigned len, lorawan_frame_t *frame)
{
    if (!session || !bytes || !frame || len < 5 || len > LORAWAN_MAX_FRAME_LEN) {
        return LORAWAN_ERROR_LENGTH;
    }
    memset(frame, 0, sizeof(*frame));
    frame->bytes = bytes;
    frame->len = len;
    frame->message_type = bytes[0] >> 5;
    frame->major = bytes[0] & 0x03;
    if (frame->major != 0) {
        return LORAWAN_ERROR_FORMAT;
    }
    if (frame->message_type == 0) {
        return parse_join_request(session, bytes, len, frame);
    }
    if (frame->message_type == 1) {
        return parse_join_accept(session, bytes, len, frame);
    }
    if (frame->message_type >= 2 && frame->message_type <= 5) {
        return parse_data_frame(session, bytes, len, frame);
    }
    frame->payload = &bytes[1];
    frame->payload_len = len - 5;
    frame->mic = &bytes[len - 4];
    return LORAWAN_OK;
}
