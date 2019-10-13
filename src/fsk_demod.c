#include "pulse_detect.h"
#include "fsk_demod.h"
#include "util.h"
#include "decoder.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern FILE *fsk_max_track_s16_file;
extern FILE *fsk_min_track_s16_file;
extern FILE *fsk_mid_track_s16_file;
extern FILE *fsk_demod_s16_file;
extern FILE *fsk_hysteris_hi_s16_file;
extern FILE *fsk_hysteris_low_s16_file;



/// Demodulate Frequency Shift Keying (FSK) sample by sample
///
/// Function is stateful between calls
/// @param fm_n: One single sample of FM data
/// @param *fsk_pulses: Will return a pulse_data_t structure for FSK demodulated data
/// @param *s: Internal state
void FSK_detect(int16_t fm_n, pulse_data_t *fsk_pulses, pulse_FSK_state_t *s){

    int16_t mid = 0;

    /* Skip a few samples in the beginning, need for framing
     * otherwise the min/max trackers wont converge properly
     */
    if (!s->skip_samples) {
        s->var_test_max = MAX(fm_n, s->var_test_max);
        s->var_test_min = MIN(fm_n, s->var_test_min);
        mid = (s->var_test_max + s->var_test_min) / 2;
        fwrite(&s->var_test_max, sizeof(int16_t), 1 , fsk_max_track_s16_file);
        fwrite(&s->var_test_min, sizeof(int16_t), 1 , fsk_min_track_s16_file);
        fwrite(&mid, sizeof(int16_t), 1 , fsk_mid_track_s16_file);
        fwrite(&fm_n, sizeof(int16_t), 1 , fsk_demod_s16_file);
        if (fm_n > mid)
            s->var_test_max -= 10;
        if (fm_n < mid)
            s->var_test_min += 10;

        s->fsk_pulse_length++;
        switch(s->fsk_state) {
            case PD_FSK_STATE_INIT:
                if (fm_n > mid)
                    s->fsk_state = PD_FSK_STATE_FH;
                if (fm_n <= mid)
                    s->fsk_state = PD_FSK_STATE_FL;
                break;
            case PD_FSK_STATE_FH:
                if (fm_n < mid) {
                    s->fsk_state = PD_FSK_STATE_FL;
                    fsk_pulses->pulse[fsk_pulses->num_pulses] = s->fsk_pulse_length;
                    s->fsk_pulse_length = 0;
                }
                break;
            case PD_FSK_STATE_FL:
                if (fm_n > mid) {
                    s->fsk_state = PD_FSK_STATE_FH;
                    fsk_pulses->gap[fsk_pulses->num_pulses] = s->fsk_pulse_length;
                    fsk_pulses->num_pulses++;
                    s->fsk_pulse_length = 0;
                    // When pulse buffer is full go to error state
                    if (fsk_pulses->num_pulses >= PD_MAX_PULSES) {
                        fprintf(stderr, "pulse_FSK_detect(): Maximum number of pulses reached!\n");
                        s->fsk_state = PD_FSK_STATE_ERROR;
                    }
                }
                break;
            case PD_FSK_STATE_ERROR:        // Stay here until cleared
                break;
            default:
                fprintf(stderr, "pulse_FSK_detect(): Unknown FSK state!!\n");
                s->fsk_state = PD_FSK_STATE_ERROR;
                break;
        }
    }
    if (s->skip_samples > 0) s->skip_samples--;
}
