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
    int32_t level_limit;
    int16_t am_buf[MAXIMAL_BUF_LENGTH];  // AM demodulated signal (for OOK decoding)
    union {
        // These buffers aren't used at the same time, so let's use a union to save some memory
        int16_t fm[MAXIMAL_BUF_LENGTH];  // FM demodulated signal (for FSK decoding)
        uint16_t temp[MAXIMAL_BUF_LENGTH];  // Temporary buffer (to be optimized out..)
    } buf;
    uint8_t u8_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    float f32_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    int sample_size; // CU8: 1, CS16: 2
    pulse_detect_t *pulse_detect;
    filter_state_t lowpass_filter_state;
    demodfm_state_t demod_FM_state;
    int enable_FM_demod;
    unsigned fsk_pulse_detect_mode;
    unsigned frequency;
    samp_grab_t *samp_grab;
    am_analyze_t *am_analyze;
    int analyze_pulses;
    file_info_t load_info;
    list_t dumper;

    /* Protocol states */
    list_t r_devs;

    pulse_data_t    pulse_data;
    pulse_data_t    fsk_pulse_data;
    unsigned frame_event_count;
    unsigned frame_start_ago;
    unsigned frame_end_ago;
    struct timeval now;
    float sample_file_pos;
};

#endif /* INCLUDE_R_PRIVATE_H_ */
