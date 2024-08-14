/** @file
    Definition of r_device struct.
*/

#ifndef INCLUDE_R_DEVICE_H_
#define INCLUDE_R_DEVICE_H_

/**
    Supported Modulation and Coding types.

    Note that Modulation is a term used usually to refer to the analog domain.
    We refer to Modulation for the process of (de-)modulating a digital line code,
    represented as pulses and gaps (OOK) or mark and space (FSK) onto a RF carrier signal.
    The line code is a coding of the bitstream data and referred to as the Coding of the data.

    We however use the well known terms to refer to the combinations of this.
    E.g. the term PWM is well known as analog or discrete range modulation, but here used
    to refer to a binary Coding of bits to on and off states (or mark and space) of the carrier.
    It should be thought of as Pulse-Width-Coding, then modulated on OOK or FSK.
    I.e. it is not truly Pulse-Width-Modulation but Pulse-Width-Coding then OOK or FSK modulation.
    This might be especially confusing with PCM, where there is no true Pulse-Code-Modulation,
    but rather NRZ (or RZ) pulse code with then OOK or FSK modulation.
*/
enum modulation_types {
    OOK_PULSE_MANCHESTER_ZEROBIT = 3,  ///< OOK Modulation, Manchester Coding. Hardcoded zerobit. Rising Edge = 0, Falling edge = 1.
    OOK_PULSE_PCM                = 4,  ///< OOK Modulation, Non-Return-to-Zero coding, Pulse = 1, No pulse = 0.
    OOK_PULSE_RZ                 = 4,  ///< OOK Modulation, Return-to-Zero coding, Pulse = 1, No pulse = 0.
    OOK_PULSE_PPM                = 5,  ///< OOK Modulation, Pulse Position Coding. Short gap = 0, Long = 1.
    OOK_PULSE_PWM                = 6,  ///< OOK Modulation, Pulse Width Coding. Short interval = 1, Long = 0.
    OOK_PULSE_PIWM_RAW           = 8,  ///< OOK Modulation, Level shift for each bit. Short interval = 1, Long = 0.
    OOK_PULSE_PIWM_DC            = 11, ///< OOK Modulation, Level shift for each bit. Short interval = 1, Long = 0.
    OOK_PULSE_DMC                = 9,  ///< OOK Modulation, Differential Manchester, Level shift within the clock cycle.
    OOK_PULSE_PWM_OSV1           = 10, ///< OOK Modulation, Pulse Width Coding. Oregon Scientific v1.
    OOK_PULSE_NRZS               = 12, ///< OOK Modulation, NRZS Coding
    FSK_DEMOD_MIN_VAL            = 16, ///< Dummy. FSK demodulation must start at this value.
    FSK_PULSE_PCM                = 16, ///< FSK Modulation, Non-Return-to-Zero coding, Pulse = 1, No pulse = 0.
    FSK_PULSE_PWM                = 17, ///< FSK Modulation, Pulse Width Coding. Short pulses = 1, Long = 0.
    FSK_PULSE_MANCHESTER_ZEROBIT = 18, ///< FSK Modulation, Manchester coding.
};

/** Decoders should return n>0 for n packets successfully decoded,
    an ABORT code if the bitbuffer is no applicable,
    or a FAIL code if the message is malformed. */
enum decode_return_codes {
    DECODE_FAIL_OTHER   = 0, ///< legacy, do not use
    /** Bitbuffer row count or row length is wrong for this sensor. */
    DECODE_ABORT_LENGTH = -1,
    DECODE_ABORT_EARLY  = -2,
    /** Message Integrity Check failed: e.g. checksum/CRC doesn't validate. */
    DECODE_FAIL_MIC     = -3,
    DECODE_FAIL_SANITY  = -4,
};

struct bitbuffer;
struct data;

/** Device protocol decoder struct. */
typedef struct r_device {
    unsigned protocol_num; ///< fixed sequence number, assigned in main().

    /* information provided by each decoder */
    char const *name;
    unsigned modulation;
    float short_width;
    float long_width;
    float reset_limit;
    float gap_limit;
    float sync_width;
    float tolerance;
    int (*decode_fn)(struct r_device *decoder, struct bitbuffer *bitbuffer);
    struct r_device *(*create_fn)(char *args);
    unsigned priority; ///< Run later and only if no previous events were produced
    unsigned disabled; ///< 0: default enabled, 1: default disabled, 2: disabled, 3: disabled and hidden
    char const *const *fields; ///< List of fields this decoder produces; required for CSV output. NULL-terminated.

    /* public for each decoder */
    int verbose;
    int verbose_bits;
    void (*log_fn)(struct r_device *decoder, int level, struct data *data);
    void (*output_fn)(struct r_device *decoder, struct data *data);

    /* Decoder results / statistics */
    unsigned decode_events;
    unsigned decode_ok;
    unsigned decode_messages;
    unsigned decode_fails[5];

    /* private for flex decoder and output callback */
    void *decode_ctx;
    void *output_ctx;
} r_device;

#endif /* INCLUDE_R_DEVICE_H_ */
