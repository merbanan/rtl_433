#include <device_reports.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define LEN(x)  (sizeof(x) / sizeof(x[0]))

char* float_to_str(float f);
void print_delimited(char *quote, ...);
void print_header(int size, char* headers[]);
void print_device_details(r_device_details_t *d);

r_device_details_t* get_device_details() {
	r_device_details_t *details = calloc(1, sizeof(r_device_details_t));
	details->battery_status = -1;
	details->channel = -1;
	return details;
}

weather_report_t* get_weather_report() {
	weather_report_t *report = malloc(sizeof(weather_report_t));
	report->outdoor_temperature = NAN;
	report->outdoor_humidity = NAN;
	report->wind_speed = NAN;
	report->wind_direction = NAN;
	report->rainfall = NAN;
	report->barometric_pressure = NAN;
	report->indoor_temperature = NAN;
	report->indoor_humidity = NAN;
	report->comfort_level = -1;
	report->forecast = -1;
	return report;
}

static int do_print_wr_header = 1;

void print_weather_report(r_device_details_t *device_details,
		weather_report_t *report) {

	if (do_print_wr_header) {
		print_header(LEN(DEVICE_FIELDS), DEVICE_FIELDS);
		printf(",");
		print_header(LEN(WEATHER_REPORT_FIELDS), WEATHER_REPORT_FIELDS);
		printf("\n");
		do_print_wr_header = 0;
	}

	print_device_details(device_details);

	printf(",%s,", float_to_str(report->outdoor_temperature));
	printf("%s,", float_to_str(report->outdoor_humidity));
	printf("%s,", float_to_str(report->wind_speed));
	printf("%s,", float_to_str(report->wind_direction));
	printf("%s,", float_to_str(report->rainfall));
	printf("%s,", float_to_str(report->barometric_pressure));
	printf("%s,", float_to_str(report->indoor_temperature));
	printf("%s,", float_to_str(report->indoor_humidity));

	char* comfort_level = "";
	if (report->comfort_level >= 0) {
		if (report->comfort_level < LEN(COMFORT_LEVELS)) {
			comfort_level = COMFORT_LEVELS[report->comfort_level];
		} else {
			fprintf(stderr, "Unknown comfort level: %d!\n", report->comfort_level);
		}
	}

	char* forecast = "";
	if (report->forecast >= 0) {
		if (report->forecast < LEN(FORECASTS)) {
			comfort_level = FORECASTS[report->forecast];
		} else {
			fprintf(stderr, "Unknown forecast: %d!\n", report->forecast);
		}
	}

	print_delimited("11", comfort_level, forecast);
	printf("\n");
}

float fahrenheit_to_celcius(float fh) {
	return (fh - 32) * 5 / 9;
}

float kph_to_mps(float kph) {
	return kph / 3.6;
}

float mph_to_mps(float mph) {
	return mph * 0.44704;
}

float kt_to_ms(float kt) {
	return kt * 463 / 900;
}

float inhg_to_hpa(float inhg) {
	return inhg * 33.8638866667;
}

float in_to_mm(float in) {
	return in * 25.4;
}

float celcius_to_fahrenheit(float c) {
	return (c * 9 / 5) + 32;
}

float mps_to_kph(float mps) {
	return mps * 3.6;
}

float mps_to_mph(float mps) {
	return mps / 0.44704;
}

float mps_to_kt(float mps) {
	return mps * 900 / 463;
}

float hpa_to_inhg(float hpa) {
	return hpa / 33.8638866667;
}

float mm_to_in(float mm) {
	return mm / 25.4;
}

char* float_to_str(float f) {
	if (isnan(f)) {
		return "";
	}

	if (f == 0.0) {
		return "0";
	}

	int s = log10(abs(f)) + 5;
	if (s < 0) {
		s = 5;
	}

	if (f < 0.0) {
		s += 1;
	}

	fprintf(stderr, "allocating %d for %.2f\n", s, f);

	char* str = malloc(s * sizeof(char));
	sprintf(str, "%.2f", f);

	return str;
}

void print_delimited(char *quote, ...) {
	int n = strlen(quote);
	char *str;

	va_list args;
	va_start(args, quote);

	for (int i = 0; i < n; ++i) {
		str = va_arg(args, char*);
		if (quote[i] == '1' && str[0] != '\0') {
			printf("\"%s\"", str);
		} else {
			printf("%s", str);
		}

		if (i + 1 < n) {
			printf(",");
		}
	}

	va_end(args);
}

void print_header(int size, char* headers[]) {
	for (int i = 0; i < size; i++) {
		printf("\"%s\"", headers[i]);
		if (i + 1 < size) {
			printf(",");
		}
	}
}

void print_device_details(r_device_details_t *d) {
	char* status = "";
	if (d->battery_status >= 0) {
		if (d->battery_status < LEN(BATTERY_STATUS)) {
			status = BATTERY_STATUS[d->battery_status];
		} else {
			fprintf(stderr, "Unknown battery status: %d!\n", d->battery_status);
		}
	}

	char* channel = "";
	if (d->channel >= 0) {
		channel = float_to_str((float) d->channel);
		if (strlen(channel) > 3) {
			channel[strlen(channel) - 3] = '\0';
		}
	}

	print_delimited("1110", d->name, d->id, status, channel);
}
