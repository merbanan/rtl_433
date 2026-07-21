/** @file
    LoRaWAN 1.0.x MAC parsing, integrity, and decryption helpers.
*/

#ifndef INCLUDE_LORAWAN_H_
#define INCLUDE_LORAWAN_H_

#include <stdint.h>

#define LORAWAN_MAX_FRAME_LEN 255

enum lorawan_result {
    LORAWAN_OK = 1,
    LORAWAN_ERROR_LENGTH = -1,
    LORAWAN_ERROR_FORMAT = -2,
    LORAWAN_ERROR_MIC = -3,
    LORAWAN_ERROR_DEVICE = -4,
};

typedef struct {
    uint8_t nwkskey[16];
    uint8_t appskey[16];
    uint8_t appkey[16];
    uint32_t dev_addr;
    uint32_t fcnt_up;
    uint32_t fcnt_down;
    uint16_t dev_nonce;
    unsigned has_nwkskey : 1;
    unsigned has_appskey : 1;
    unsigned has_appkey : 1;
    unsigned has_dev_addr : 1;
    unsigned has_fcnt_up : 1;
    unsigned has_fcnt_down : 1;
    unsigned has_dev_nonce : 1;
} lorawan_session_t;

typedef struct {
    uint8_t const *bytes;
    unsigned len;
    unsigned message_type;
    unsigned major;
    unsigned direction;
    uint8_t const *mic;
    unsigned mic_verified;

    uint8_t const *join_eui;
    uint8_t const *dev_eui;
    uint16_t dev_nonce;
    uint8_t const *join_accept_encrypted;
    unsigned join_accept_encrypted_len;
    uint8_t join_accept[32];
    unsigned join_accept_decrypted;

    uint32_t dev_addr;
    uint8_t fctrl;
    uint32_t fcnt;
    uint8_t const *fopts;
    unsigned fopts_len;
    unsigned has_fport;
    uint8_t fport;
    uint8_t const *frm_payload_encrypted;
    unsigned frm_payload_len;
    uint8_t frm_payload[LORAWAN_MAX_FRAME_LEN];
    unsigned frm_payload_decrypted;

    uint8_t const *payload;
    unsigned payload_len;
} lorawan_frame_t;

void lorawan_session_init(lorawan_session_t *session);

/** Set a session option. Returns 1 if accepted, 0 if unknown, or -1 if invalid. */
int lorawan_session_set_option(lorawan_session_t *session,
        char const *key, char const *value);

int lorawan_parse_frame(lorawan_session_t *session, uint8_t const *bytes,
        unsigned len, lorawan_frame_t *frame);

char const *lorawan_message_type_name(unsigned message_type);

void lorawan_format_reverse(char *out, uint8_t const *bytes, unsigned len);

#endif /* INCLUDE_LORAWAN_H_ */
