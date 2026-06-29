/** @file
    Definition of r_private state structure.
*/

#ifndef INCLUDE_R_PRIVATE_H_
#define INCLUDE_R_PRIVATE_H_

#include <stdint.h>
#include <time.h>
#include "list.h"
#include "baseband.h"
#include "pulse_detect.h"
#include "fileformat.h"
#include "samp_grab.h"
#include "am_analyze.h"
#include "rtl_433.h"
#include "compat_time.h"

struct dm_state {
    float auto_level;
    float squelch_offset;
    float level_limit;
    float noise_level;
    float min_level_auto;
    float min_level;
    float min_snr;
    float low_pass;
    int use_mag_est;
    int detect_verbosity;

    int16_t am_buf[MAXIMAL_BUF_LENGTH];  // AM demodulated signal (for OOK decoding)
    union {
        // These buffers aren't used at the same time, so let's use a union to save some memory
        int16_t fm[MAXIMAL_BUF_LENGTH];  // FM demodulated signal (for FSK decoding)
        uint16_t temp[MAXIMAL_BUF_LENGTH];  // Temporary buffer (to be optimized out..)
    } buf;
    uint8_t u8_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    float f32_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    int sample_size; // CU8: 2, CS16: 4
    pulse_detect_t *pulse_detect;
    filter_state_t lowpass_filter_state;
    demodfm_state_t demod_FM_state;
    int enable_FM_demod;
    samp_grab_t *samp_grab;
    am_analyze_t *am_analyze;
    int analyze_pulses;
    file_info_t load_info;
    list_t dumper;

    /* Protocol states */
    list_t r_devs;

    pulse_data_t    pulse_data;
    pulse_data_t    fsk_pulse_data;
    uint64_t input_pos;
    unsigned frame_event_count;
    int frame_quality;
    unsigned frame_start_ago;
    unsigned frame_end_ago;
    struct timeval now;
    float sample_file_pos;

    /* parameters copied in for each frame  */
    uint32_t center_frequency;
    uint32_t samp_rate;
    int fsk_pulse_detect_mode;
    int grab_mode; ///< Signal grabber mode: 0=off, 1=all, 2=unknown, 3=known
    int raw_mode; ///< Raw pulses printing mode: 0=off, 1=all, 2=unknown, 3=known
    int verbosity; ///< 0=normal, 1=verbose, 2=verbose decoders, 3=debug decoders, 4=trace decoding.
    int report_noise;
    list_t *raw_handler;

    /* global stats */
    time_t running_since;          ///< program start time statistic
    unsigned total_frames_count;   ///< total frames recieved statistic
    unsigned total_frames_squelch; ///< total frames with noise only statistic
    unsigned total_frames_ook;     ///< total frames with ook demod statistic
    unsigned total_frames_fsk;     ///< total frames with fsk demod statistic
    unsigned total_frames_events;  ///< total frames with decoder events statistic

    /* per report interval stats */
    time_t frames_since;    ///< time at start of report interval statistic
    unsigned frames_ook;    ///< counter of ook demods for report interval statistic
    unsigned frames_fsk;    ///< counter of fsk demods for report interval statistic
    unsigned frames_events; ///< counter of decoder events for report interval statistic
};

#endif /* INCLUDE_R_PRIVATE_H_ */
