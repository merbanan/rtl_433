/** @file
    Definition of r_device struct.
*/

#ifndef INCLUDE_R_DEVICE_H_
#define INCLUDE_R_DEVICE_H_

/** Supported modulation types. */
enum modulation_types {
    OOK_PULSE_MANCHESTER_ZEROBIT = 3,  ///< Manchester encoding. Hardcoded zerobit. Rising Edge = 0, Falling edge = 1.
    OOK_PULSE_PCM_RZ             = 4,  ///< Pulse Code Modulation with Return-to-Zero encoding, Pulse = 0, No pulse = 1.
    OOK_PULSE_PPM                = 5,  ///< Pulse Position Modulation. Short gap = 0, Long = 1.
    OOK_PULSE_PWM                = 6,  ///< Pulse Width Modulation with precise timing parameters.
    OOK_PULSE_PIWM_RAW           = 8,  ///< Level shift for each bit. Short interval = 1, Long = 0.
    OOK_PULSE_PIWM_DC            = 11, ///< Level shift for each bit. Short interval = 1, Long = 0.
    OOK_PULSE_DMC                = 9,  ///< Level shift within the clock cycle.
    OOK_PULSE_PWM_OSV1           = 10, ///< Pulse Width Modulation. Oregon Scientific v1.
    FSK_DEMOD_MIN_VAL            = 16, ///< Dummy. FSK demodulation must start at this value.
    FSK_PULSE_PCM                = 16, ///< FSK, Pulse Code Modulation.
    FSK_PULSE_PWM                = 17, ///< FSK, Pulse Width Modulation. Short pulses = 1, Long = 0.
    FSK_PULSE_MANCHESTER_ZEROBIT = 18, ///< FSK, Manchester encoding.
};

struct bitbuffer;
struct data;

/** Device protocol decoder struct. */
typedef struct r_device {
    unsigned protocol_num; ///< fixed sequence number, assigned in main().

    /* information provided by each decoder */
    char *name;
    unsigned modulation;
    float short_width;
    float long_width;
    float reset_limit;
    float gap_limit;
    float sync_width;
    float tolerance;
    int (*decode_fn)(struct r_device *decoder, struct bitbuffer *bitbuffer);
    struct r_device *(*create_fn)(char *args);
    unsigned disabled;
    char **fields; ///< List of fields this decoder produces; required for CSV output. NULL-terminated.

    /* public for each decoder */
    int new_model_keys; ///< TODO: temporary allow to change to new style model keys
    int verbose;
    int verbose_bits;
    void (*output_fn)(struct r_device *decoder, struct data *data);

    /* private for flex decoder and output callback */
    void *decode_ctx;
    void *output_ctx;

    /* private pulse limits (converted to count of samples) */
    float f_short_width; ///< precision reciprocal for PCM.
    float f_long_width;  ///< precision reciprocal for PCM.
    int s_short_width;
    int s_long_width;
    int s_reset_limit;
    int s_gap_limit;
    int s_sync_width;
    int s_tolerance;
} r_device;

#endif /* INCLUDE_R_DEVICE_H_ */
