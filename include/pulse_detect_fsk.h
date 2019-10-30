/** @file
    Pulse detect functions.

    FSK pulse detector

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

void pulse_FSK_detect(int16_t fm_n, pulse_data_t *fsk_pulses, pulse_FSK_state_t *s);
void pulse_FSK_wrap_up(pulse_data_t *fsk_pulses, pulse_FSK_state_t *s);
