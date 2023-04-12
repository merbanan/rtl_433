/** @file
    Definition of r_cfg application structure.
*/

#ifndef INCLUDE_RTL_433_H_
#define INCLUDE_RTL_433_H_

#include <stdint.h>
#include "list.h"
#include <time.h>
#include <signal.h>

#define DEFAULT_SAMPLE_RATE     250000
#define DEFAULT_FREQUENCY       433920000
#define DEFAULT_HOP_TIME        (60*10)
#define DEFAULT_ASYNC_BUF_NUMBER    0 // Force use of default value (librtlsdr default: 15)
#define DEFAULT_BUF_LENGTH      (16 * 32 * 512) // librtlsdr default
#define FSK_PULSE_DETECTOR_LIMIT 800000000

#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)
#define MAX_FREQS               32

#define INPUT_LINE_MAX 8192 /**< enough for a complete textual bitbuffer (25*256) */

struct sdr_dev;
struct r_device;
struct mg_mgr;

typedef enum {
    CONVERT_NATIVE,
    CONVERT_SI,
    CONVERT_CUSTOMARY,
} conversion_mode_t;

typedef enum {
    REPORT_TIME_DEFAULT,
    REPORT_TIME_DATE,
    REPORT_TIME_SAMPLES,
    REPORT_TIME_UNIX,
    REPORT_TIME_ISO,
    REPORT_TIME_OFF,
} time_mode_t;

typedef enum {
    DEVICE_MODE_QUIT,
    DEVICE_MODE_RESTART,
    DEVICE_MODE_PAUSE,
    DEVICE_MODE_MANUAL,
} device_mode_t;

typedef enum {
    DEVICE_STATE_STOPPED,
    DEVICE_STATE_STARTING,
    DEVICE_STATE_GRACE,
    DEVICE_STATE_STARTED,
} device_state_t;

typedef struct r_cfg {
    device_mode_t dev_mode; ///< Input device run mode
    device_state_t dev_state; ///< Input device run state
    char *dev_query;
    char const *dev_info;
    char *gain_str;
    char *settings_str;
    int ppm_error;
    uint32_t out_block_size;
    char const *test_data;
    list_t in_files;
    char const *in_filename;
    int in_replay;
    volatile sig_atomic_t hop_now;
    volatile sig_atomic_t exit_async;
    volatile sig_atomic_t exit_code; ///< 0=no err, 1=params or cmd line err, 2=sdr device read error, 3=usb init error, 5=USB error (reset), other=other error
    int frequencies;
    int frequency_index;
    uint32_t frequency[MAX_FREQS];
    uint32_t center_frequency;
    int fsk_pulse_detect_mode;
    int hop_times;
    int hop_time[MAX_FREQS];
    time_t hop_start_time;
    int duration;
    time_t stop_time;
    int after_successful_events_flag;
    uint32_t samp_rate;
    uint64_t input_pos;
    uint32_t bytes_to_read;
    struct sdr_dev *dev;
    int grab_mode; ///< Signal grabber mode: 0=off, 1=all, 2=unknown, 3=known
    int raw_mode; ///< Raw pulses printing mode: 0=off, 1=all, 2=unknown, 3=known
    int verbosity; ///< 0=normal, 1=verbose, 2=verbose decoders, 3=debug decoders, 4=trace decoding.
    int verbose_bits;
    conversion_mode_t conversion_mode;
    int report_meta;
    int report_noise;
    int report_protocol;
    time_mode_t report_time;
    int report_time_hires;
    int report_time_tz;
    int report_time_utc;
    int report_description;
    int report_stats;
    int stats_interval;
    volatile sig_atomic_t stats_now;
    time_t stats_time;
    int no_default_devices;
    struct r_device *devices;
    uint16_t num_r_devices;
    list_t data_tags;
    list_t output_handler;
    list_t raw_handler;
    int has_logout;
    struct dm_state *demod;
    char const *sr_filename;
    int sr_execopen;
    int watchdog; ///< SDR acquire stall watchdog
    /* stats*/
    time_t frames_since; ///< stats start time
    unsigned frames_count; ///< stats counter for interval
    unsigned frames_fsk; ///< stats counter for interval
    unsigned frames_events; ///< stats counter for interval
    struct mg_mgr *mgr;
} r_cfg_t;

#endif /* INCLUDE_RTL_433_H_ */
