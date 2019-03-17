/** @file
    Definition of r_cfg application structure.
*/

#ifndef INCLUDE_RTL_433_H_
#define INCLUDE_RTL_433_H_

#include <stdint.h>
#include "list.h"
#include <time.h>

#define DEFAULT_SAMPLE_RATE     250000
#define DEFAULT_FREQUENCY       433920000
#define DEFAULT_HOP_TIME        (60*10)
#define DEFAULT_ASYNC_BUF_NUMBER    0 // Force use of default value (librtlsdr default: 15)
#define DEFAULT_BUF_LENGTH      (16 * 32 * 512) // librtlsdr default

/*
 * Theoretical high level at I/Q saturation is 128x128 = 16384 (above is ripple)
 * 0 = automatic adaptive level limit, else fixed level limit
 * 8000 = previous fixed default
 */
#define DEFAULT_LEVEL_LIMIT     0

#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)
#define MAX_FREQS               32

struct sdr_dev;
struct r_device;

typedef enum {
    CONVERT_NATIVE,
    CONVERT_SI,
    CONVERT_CUSTOMARY
} conversion_mode_t;

typedef enum {
    REPORT_TIME_DEFAULT,
    REPORT_TIME_DATE,
    REPORT_TIME_SAMPLES,
    REPORT_TIME_OFF,
} time_mode_t;

typedef struct r_cfg {
    char *dev_query;
    char *gain_str;
    char *settings_str;
    int ppm_error;
    uint32_t out_block_size;
    char const *test_data;
    list_t in_files;
    char const *in_filename;
    int do_exit;
    int do_exit_async;
    int frequencies;
    int frequency_index;
    uint32_t frequency[MAX_FREQS];
    uint32_t center_frequency;
    time_t rawtime_old;
    int duration;
    time_t stop_time;
    int stop_after_successful_events_flag;
    uint32_t samp_rate;
    uint64_t input_pos;
    uint32_t bytes_to_read;
    struct sdr_dev *dev;
    int grab_mode;
    int verbosity; ///< 0=normal, 1=verbose, 2=verbose decoders, 3=debug decoders, 4=trace decoding.
    int verbose_bits;
    conversion_mode_t conversion_mode;
    int report_meta;
    int report_protocol;
    time_mode_t report_time;
    int report_time_hires;
    int report_time_utc;
    int report_description;
    int no_default_devices;
    struct r_device *devices;
    uint16_t num_r_devices;
    char *output_tag;
    list_t output_handler;
    struct dm_state *demod;
    int new_model_keys;
} r_cfg_t;

#endif /* INCLUDE_RTL_433_H_ */
