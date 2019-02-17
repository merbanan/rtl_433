/** @file
    Definition of r_device struct.
*/

#ifndef INCLUDE_R_DEVICE_H_
#define INCLUDE_R_DEVICE_H_

struct bitbuffer;
struct data;

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
