#include "rtl_433.h"
#include "device_reports.h"

// ** Acurite 5n1 functions **

// as per Jens Jensen <zerog2k@yahoo.com> it should be
// { "NW", "WSW", "WNW", "W", "NNW", "SW", "N", "SSW",
//   "ENE", "SE", "E", "ESE", "NE", "SSE", "NNE", "S" };
const float acurite_winddirections[] = { 315.0, 247.5, 292.5, 270.0, 337.5,
		225.0, 0.0, 202.5, 67.5, 135.0, 90.0, 112.5, 45.0, 157.5, 22.5, 180.0 };

static int acurite_raincounter = 0;

static int acurite_crc(uint8_t row[BITBUF_COLS], int cols) {
	// sum of first n-1 bytes modulo 256 should equal nth byte
	int i;
	int sum = 0;
	for (i = 0; i < cols; i++) {
		sum += row[i];
	}
	if (sum % 256 == row[cols]) {
		return 1;
	} else {
		return 0;
	}
}

static int acurite_detect(uint8_t *pRow) {
	int i;
	if (pRow[0] != 0x00) {
		// invert bits due to wierd issue
		for (i = 0; i < 8; i++) {
			pRow[i] = ~pRow[i] & 0xFF;
		}
		pRow[0] |= pRow[8];  // fix first byte that has mashed leading bit

		if (acurite_crc(pRow, 7)) {
			return 1;  // passes crc check
		}
	}
	return 0;
}

static float acurite_getTemp(uint8_t highbyte, uint8_t lowbyte) {
	// range -40 to 158 F
	int highbits = (highbyte & 0x0F) << 7;
	int lowbits = lowbyte & 0x7F;
	int rawtemp = highbits | lowbits;
	float temp = (rawtemp - 400) / 10.0;
	return temp;
}

static double acurite_getWindSpeed(uint8_t highbyte, uint8_t lowbyte) {
	// range: 0 to 159 kph
	int highbits = (highbyte & 0x7F) << 3;
	int lowbits = (lowbyte & 0x7F) >> 4;
	double speed = highbits | lowbits;
	// speed formula according to empirical data
	if (speed > 0) {
		speed = speed * 0.23 + 0.28;
	}
	return speed;
}

static float acurite_getWindDirection(uint8_t byte) {
	// 16 compass points, ccw from (NNW) to 15 (N)
	int direction = byte & 0x0F;
	return acurite_winddirections[direction];
}

static int acurite_getHumidity(uint8_t byte) {
	// range: 1 to 99 %RH
	int humidity = byte & 0x7F;
	return humidity;
}

static int acurite_getRainfallCounter(uint8_t hibyte, uint8_t lobyte) {
	// range: 0 to 99.99 in, 0.01 in incr., rolling counter?
	int raincounter = ((hibyte & 0x7f) << 7) | (lobyte & 0x7F);
	return raincounter;
}

static int acurite5n1_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS],
		int16_t bits_per_row[BITBUF_ROWS]) {
	// acurite 5n1 weather sensor decoding for rtl_433
	// Jens Jensen 2014
	int i;
	uint8_t *buf = NULL;
	// run through rows til we find one with good crc (brute force)
	for (i = 0; i < BITBUF_ROWS; i++) {
		if (acurite_detect(bb[i])) {
			buf = bb[i];
			break; // done
		}
	}

	if (buf) {

    	r_device_details_t *VN1TXC = get_device_details();
    	VN1TXC->name = "VN1TXC";
    	VN1TXC->id = malloc(5 * sizeof(char));
    	sprintf(VN1TXC->id, "%02X%02X", buf[0], buf[1]);

    	VN1TXC->channel = (~buf[0] & 0xC0) >> 6;
		VN1TXC->battery_status = ((buf[2] & 0x40) >> 6);

		// decode packet here
		fprintf(stderr, "Detected Acurite 5n1 sensor, %d bits\n",
				bits_per_row[1]);
		if (debug_output) {
			for (i = 0; i < 8; i++)
				fprintf(stderr, "%02X ", buf[i]);
			fprintf(stderr, "CRC OK\n");
		}

		weather_report_t *report = get_weather_report();
		report->wind_speed = acurite_getWindSpeed(buf[3], buf[4]);

		int type = (buf[2] & 0x3F);
		if (type == 49) {
			// wind speed, wind direction, rainfall
			float rainfall = 0.00;
			int raincounter = acurite_getRainfallCounter(buf[5], buf[6]);
			if (acurite_raincounter > 0) {
				// track rainfall difference after first run
				rainfall = (raincounter - acurite_raincounter) * 0.01;
			}

			// capture starting counter
			acurite_raincounter = raincounter;

            report->wind_direction = acurite_getWindDirection(buf[4]);
            report->rainfall = in_to_mm(rainfall);

        } else if ((buf[2] & 0x0F) == 8) {
            // wind speed, temp, RH
            report->outdoor_temperature = fahrenheit_to_celcius(acurite_getTemp(buf[4], buf[5]));
            report->outdoor_humidity = acurite_getHumidity(buf[6]);
        }

        print_weather_report(VN1TXC, report);
    } else {
    	return 0;
    }

    if (debug_output)
    	debug_callback(bb, bits_per_row);

    return 1;
}

static int acurite_rain_gauge_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS],
		int16_t bits_per_row[BITBUF_ROWS]) {
	// This needs more validation to positively identify correct sensor type, but it basically works if message is really from acurite raingauge and it doesn't have any errors
	if ((bb[0][0] != 0) && (bb[0][1] != 0) && (bb[0][2] != 0) && (bb[0][3] == 0)
			&& (bb[0][4] == 0)) {
		float total_rain = ((bb[0][1] & 0xf) << 8) + bb[0][2];
		total_rain /= 2; // Sensor reports number of bucket tips.  Each bucket tip is .5mm
		fprintf(stderr, "AcuRite Rain Gauge Total Rain is %2.1fmm\n",
				total_rain);
		fprintf(stderr, "Raw Message: %02x %02x %02x %02x %02x\n", bb[0][0],
				bb[0][1], bb[0][2], bb[0][3], bb[0][4]);
		return 1;
	}
	return 0;
}

static int acurite_th_detect(uint8_t *buf) {
	if (buf[5] != 0) {
		return 0;
	}
	uint8_t sum = (buf[0] + buf[1] + buf[2] + buf[3]) & 0xff;
	if (sum == 0) {
		return 0;
	}
	return sum == buf[4];
}
static float acurite_th_temperature(uint8_t *s) {
	uint16_t shifted = (((s[1] & 0x0f) << 8) | s[2]) << 4; // Logical left shift
	return (((int16_t) shifted) >> 4) / 10.0; // Arithmetic right shift
}
static int acurite_th_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS],
		int16_t bits_per_row[BITBUF_ROWS]) {
	uint8_t *buf = NULL;
	int i;
	for (i = 0; i < BITBUF_ROWS; i++) {
		if (acurite_th_detect(bb[i])) {
			buf = bb[i];
			break;
		}
	}
	if (buf) {
		fprintf(stderr, "Temperature event:\n");
		fprintf(stderr, "protocol      = Acurite Temp&Humidity\n");
		fprintf(stderr, "temp          = %.1f°C\n",
				acurite_th_temperature(buf));
		fprintf(stderr, "humidity      = %d%%\n\n", buf[3]);
		return 1;
	}

	return 0;
}

r_device acurite5n1 = {
/* .id             = */10,
/* .name           = */"Acurite 5n1 Weather Station",
/* .modulation     = */OOK_PWM_P,
/* .short_limit    = */70,
/* .long_limit     = */240,
/* .reset_limit    = */21000,
/* .json_callback  = */&acurite5n1_callback, };

r_device acurite_rain_gauge = {
/* .id             = */10,
/* .name           = */"Acurite 896 Rain Gauge",
/* .modulation     = */OOK_PWM_D,
/* .short_limit    = */1744 / 4,
/* .long_limit     = */3500 / 4,
/* .reset_limit    = */5000 / 4,
/* .json_callback  = */&acurite_rain_gauge_callback, };

r_device acurite_th = {
/* .id             = */11,
/* .name           = */"Acurite Temperature and Humidity Sensor",
/* .modulation     = */OOK_PWM_D,
/* .short_limit    = */300,
/* .long_limit     = */550,
/* .reset_limit    = */2500,
/* .json_callback  = */&acurite_th_callback, };
