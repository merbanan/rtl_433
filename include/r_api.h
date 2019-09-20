/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_R_API_H_
#define INCLUDE_R_API_H_

#include <stdint.h>

struct r_cfg;
struct r_device;
struct data;
struct pulse_data;
struct list;

/* general */

char const *version_string(void);

struct r_cfg *r_create_cfg(void);

void r_init_cfg(struct r_cfg *cfg);

void r_free_cfg(struct r_cfg *cfg);

/* device decoder protocols */

void update_protocol(struct r_cfg *cfg, struct r_device *r_dev);

void register_protocol(struct r_cfg *cfg, struct r_device *r_dev, char *arg);

void free_protocol(struct r_device *r_dev);

void unregister_protocol(struct r_cfg *cfg, struct r_device *r_dev);

void register_all_protocols(struct r_cfg *cfg, unsigned disabled);

void update_protocols(struct r_cfg *cfg);

/* output helper */

void calc_rssi_snr(struct r_cfg *cfg, struct pulse_data *pulse_data);

char *time_pos_str(struct r_cfg *cfg, unsigned samples_ago, char *buf);

char const **well_known_output_fields(struct r_cfg *cfg);

char const **determine_csv_fields(struct r_cfg *cfg, char const **well_known, int *num_fields);

int run_ook_demods(struct list *r_devs, struct pulse_data *pulse_data);

int run_fsk_demods(struct list *r_devs, struct pulse_data *fsk_pulse_data);

/* handlers */

void event_occurred_handler(struct r_cfg *cfg, struct data *data);

void data_acquired_handler(struct r_device *r_dev, struct data *data);

struct data *create_report_data(struct r_cfg *cfg, int level);

void flush_report_data(struct r_cfg *cfg);

/* setup */

void add_json_output(struct r_cfg *cfg, char *param);

void add_csv_output(struct r_cfg *cfg, char *param);

void add_kv_output(struct r_cfg *cfg, char *param);

void add_mqtt_output(struct r_cfg *cfg, char *param);

void add_influx_output(struct r_cfg *cfg, char *param);

void add_syslog_output(struct r_cfg *cfg, char *param);

void add_null_output(struct r_cfg *cfg, char *param);

void start_outputs(struct r_cfg *cfg, char const **well_known);

void add_dumper(struct r_cfg *cfg, char const *spec, int overwrite);

void add_infile(struct r_cfg *cfg, char *in_file);

/* runtime */

//void set_center_freq(struct r_cfg *cfg, uint32_t center_frequency);

//void set_sample_rate(struct r_cfg *cfg, uint32_t samp_rate);

#endif /* INCLUDE_R_API_H_ */
