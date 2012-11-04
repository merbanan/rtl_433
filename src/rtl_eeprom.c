/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * rtl_eeprom, EEPROM modification tool
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"

#define EEPROM_SIZE	256
#define MAX_STR_SIZE	256
#define STR_OFFSET	0x09

static rtlsdr_dev_t *dev = NULL;

typedef struct rtlsdr_config {
	uint16_t vendor_id;
	uint16_t product_id;
	char manufacturer[MAX_STR_SIZE];
	char product[MAX_STR_SIZE];
	char serial[MAX_STR_SIZE];
	int have_serial;
	int enable_ir;
	int remote_wakeup;
} rtlsdr_config_t;

void dump_config(rtlsdr_config_t *conf)
{
	fprintf(stderr, "__________________________________________\n");
	fprintf(stderr, "Vendor ID:\t\t0x%04x\n", conf->vendor_id);
	fprintf(stderr, "Product ID:\t\t0x%04x\n", conf->product_id);
	fprintf(stderr, "Manufacturer:\t\t%s\n", conf->manufacturer);
	fprintf(stderr, "Product:\t\t%s\n", conf->product);
	fprintf(stderr, "Serial number:\t\t%s\n", conf->serial);
	fprintf(stderr, "Serial number enabled:\t");
	fprintf(stderr, conf->have_serial ? "yes\n": "no\n");
	fprintf(stderr, "IR endpoint enabled:\t");
	fprintf(stderr, conf->enable_ir ? "yes\n": "no\n");
	fprintf(stderr, "Remote wakeup enabled:\t");
	fprintf(stderr, conf->remote_wakeup ? "yes\n": "no\n");
	fprintf(stderr, "__________________________________________\n");
}

void usage(void)
{
	fprintf(stderr,
		"rtl_eeprom, an EEPROM programming tool for "
		"RTL2832 based DVB-T receivers\n\n"
		"Usage:\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-m <str> set manufacturer string]\n"
		"\t[-p <str> set product string]\n"
		"\t[-s <str> set serial number string]\n"
		"\t[-i <0,1> disable/enable IR-endpoint]\n"
		"\t[-g <conf> generate default config and write to device]\n"
		"\t[   <conf> can be one of:]\n"
		"\t[   realtek\t\tRealtek default (as without EEPROM)]\n"
		"\t[   realtek_oem\t\tRealtek default OEM with EEPROM]\n"
		"\t[   noxon\t\tTerratec NOXON DAB Stick]\n"
		"\t[   terratec_black\tTerratec T Stick Black]\n"
		"\t[   terratec_plus\tTerratec T Stick+ (DVB-T/DAB)]\n"
		"\t[-w <filename> write dumped file to device]\n"
		"\t[-r <filename> dump EEPROM to file]\n"
		"\t[-h display this help text]\n"
		"\nUse on your own risk, especially -w!\n");
	exit(1);
}

int get_string_descriptor(int pos, uint8_t *data, char *str)
{
	int len, i, j = 0;

	len = data[pos];

	if (data[pos + 1] != 0x03)
		fprintf(stderr, "Error: invalid string descriptor!\n");

	for (i = 2; i < len; i += 2)
		str[j++] = data[pos + i];

	str[j] = 0x00;

	return pos + i;
}

int set_string_descriptor(int pos, uint8_t *data, char *str)
{
	int i = 0, j = 2;

	if (pos < 0)
		return -1;

	data[pos + 1] = 0x03;

	while (str[i] != 0x00) {
		if ((pos + j) >= 78) {
			fprintf(stderr, "Error: string too long, truncated!\n");
			return -1;
		}
		data[pos + j++] = str[i++];
		data[pos + j++] = 0x00;
	}

	data[pos] = j;

	return pos + j;
}

int parse_eeprom_to_conf(rtlsdr_config_t *conf, uint8_t *dat)
{
	int pos;

	if ((dat[0] != 0x28) || (dat[1] != 0x32))
		fprintf(stderr, "Error: invalid RTL2832 EEPROM header!\n");

	conf->vendor_id = dat[2] | (dat[3] << 8);
	conf->product_id = dat[4] | (dat[5] << 8);
	conf->have_serial = (dat[6] == 0xa5) ? 1 : 0;
	conf->remote_wakeup = (dat[7] & 0x01) ? 1 : 0;
	conf->enable_ir = (dat[7] & 0x02) ? 1 : 0;

	pos = get_string_descriptor(STR_OFFSET, dat, conf->manufacturer);
	pos = get_string_descriptor(pos, dat, conf->product);
	get_string_descriptor(pos, dat, conf->serial);

	return 0;
}

int gen_eeprom_from_conf(rtlsdr_config_t *conf, uint8_t *dat)
{
	int pos;

	dat[0] = 0x28;
	dat[1] = 0x32;
	dat[2] = conf->vendor_id & 0xff;
	dat[3] = (conf->vendor_id >> 8) & 0xff ;
	dat[4] = conf->product_id & 0xff;
	dat[5] = (conf->product_id >> 8) & 0xff;
	dat[6] = conf->have_serial ? 0xa5 : 0x00;
	dat[7] = 0x14;
	dat[7] |= conf->remote_wakeup ? 0x01 : 0x00;
	dat[7] |= conf->enable_ir ? 0x02 : 0x00;
	dat[8] = 0x02;

	pos = set_string_descriptor(STR_OFFSET, dat, conf->manufacturer);
	pos = set_string_descriptor(pos, dat, conf->product);
	pos = set_string_descriptor(pos, dat, conf->serial);

	dat[78] = 0x00;		/* length of IR config */

	return pos;
}

enum configs {
	CONF_NONE = 0,
	REALTEK,
	REALTEK_EEPROM,
	TERRATEC_NOXON,
	TERRATEC_T_BLACK,
	TERRATEC_T_PLUS,
};

void gen_default_conf(rtlsdr_config_t *conf, int config)
{
	switch (config) {
	case REALTEK:
		fprintf(stderr, "Realtek default (as without EEPROM)\n");
		conf->vendor_id = 0x0bda;
		conf->product_id = 0x2832;
		strcpy(conf->manufacturer, "Generic");
		strcpy(conf->product, "RTL2832U DVB-T");
		strcpy(conf->serial, "0");
		conf->have_serial = 1;
		conf->enable_ir = 0;
		conf->remote_wakeup = 1;
		break;
	case REALTEK_EEPROM:
		fprintf(stderr, "Realtek default OEM with EEPROM\n");
		conf->vendor_id = 0x0bda;
		conf->product_id = 0x2838;
		strcpy(conf->manufacturer, "Realtek");
		strcpy(conf->product, "RTL2838UHIDIR");
		strcpy(conf->serial, "00000001");
		conf->have_serial = 1;
		conf->enable_ir = 1;
		conf->remote_wakeup = 0;
		break;
	case TERRATEC_NOXON:
		fprintf(stderr, "Terratec NOXON DAB Stick\n");
		conf->vendor_id = 0x0ccd;
		conf->product_id = 0x00b3;
		strcpy(conf->manufacturer, "NOXON");
		strcpy(conf->product, "DAB Stick");
		strcpy(conf->serial, "0");
		conf->have_serial = 1;
		conf->enable_ir = 0;
		conf->remote_wakeup = 1;
		break;
	case TERRATEC_T_BLACK:
		fprintf(stderr, "Terratec T Stick Black\n");
		conf->vendor_id = 0x0ccd;
		conf->product_id = 0x00a9;
		strcpy(conf->manufacturer, "Realtek");
		strcpy(conf->product, "RTL2838UHIDIR");
		strcpy(conf->serial, "00000001");
		conf->have_serial = 1;
		conf->enable_ir = 1;
		conf->remote_wakeup = 0;
		break;
	case TERRATEC_T_PLUS:
		fprintf(stderr, "Terratec ran T Stick+\n");
		conf->vendor_id = 0x0ccd;
		conf->product_id = 0x00d7;
		strcpy(conf->manufacturer, "Realtek");
		strcpy(conf->product, "RTL2838UHIDIR");
		strcpy(conf->serial, "00000001");
		conf->have_serial = 1;
		conf->enable_ir = 1;
		conf->remote_wakeup = 0;
		break;
	default:
		break;
	};
}

int main(int argc, char **argv)
{
	int i, r, opt;
	uint32_t dev_index = 0;
	int device_count;
	char *filename = NULL;
	FILE *file = NULL;
	char *manuf_str = NULL;
	char *product_str = NULL;
	char *serial_str = NULL;
	uint8_t buf[EEPROM_SIZE];
	rtlsdr_config_t conf;
	int flash_file = 0;
	int default_config = 0;
	int change = 0;
	int ir_endpoint = 0;
	char ch;

	while ((opt = getopt(argc, argv, "d:m:p:s:i:g:w:r:h?")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 'm':
			manuf_str = optarg;
			change = 1;
			break;
		case 'p':
			product_str = optarg;
			change = 1;
			break;
		case 's':
			serial_str = optarg;
			change = 1;
			break;
		case 'i':
			ir_endpoint = (atoi(optarg) > 0) ? 1 : -1;
			change = 1;
			break;
		case 'g':
			if (!strcmp(optarg, "realtek"))
				default_config = REALTEK;
			else if (!strcmp(optarg, "realtek_oem"))
				default_config = REALTEK_EEPROM;
			else if (!strcmp(optarg, "noxon"))
				default_config = TERRATEC_NOXON;
			else if (!strcmp(optarg, "terratec_black"))
				default_config = TERRATEC_T_BLACK;
			else if (!strcmp(optarg, "terratec_plus"))
				default_config = TERRATEC_T_PLUS;

			if (default_config != CONF_NONE)
				change = 1;
			break;
		case 'w':
			flash_file = 1;
			change = 1;
		case 'r':
			filename = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stderr, "No supported devices found.\n");
		exit(1);
	}

	fprintf(stderr, "Found %d device(s):\n", device_count);
	for (i = 0; i < device_count; i++)
		fprintf(stderr, "  %d:  %s\n", i, rtlsdr_get_device_name(i));
	fprintf(stderr, "\n");

	fprintf(stderr, "Using device %d: %s\n",
		dev_index,
		rtlsdr_get_device_name(dev_index));

	r = rtlsdr_open(&dev, dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}

	fprintf(stderr, "\n");

	r = rtlsdr_read_eeprom(dev, buf, 0, EEPROM_SIZE);
	if (r < 0) {
		if (r == -3)
			fprintf(stderr, "No EEPROM has been found.\n");
		else
			fprintf(stderr, "Failed to read EEPROM, err %i.\n", r);
		goto exit;
	}

	if (r < 0)
		return -1;

	fprintf(stderr, "Current configuration:\n");
	parse_eeprom_to_conf(&conf, buf);
	dump_config(&conf);

	if (filename) {
		file = fopen(filename, flash_file ? "rb" : "wb");
		if (!file) {
			fprintf(stderr, "Error opening file!\n");
			goto exit;
		}
		if (flash_file) {
			if (fread(buf, 1, sizeof(buf), file) != sizeof(buf))
				fprintf(stderr, "Error reading file!\n");
		} else {
			if (fwrite(buf, 1, sizeof(buf), file) != sizeof(buf))
				fprintf(stderr, "Short write, exiting!\n");
			else
				fprintf(stderr, "\nDump to %s successful.\n", filename);
		}
	}

	if (manuf_str)
		strncpy((char*)&conf.manufacturer, manuf_str, MAX_STR_SIZE);

	if (product_str)
		strncpy((char*)&conf.product, product_str, MAX_STR_SIZE);

	if (serial_str) {
		conf.have_serial = 1;
		strncpy((char*)&conf.serial, serial_str, MAX_STR_SIZE);
	}

	if (ir_endpoint != 0)
		 conf.enable_ir = (ir_endpoint > 0) ? 1 : 0;

	if (!change)
		goto exit;

	fprintf(stderr, "\nNew configuration:\n");

	if (default_config != CONF_NONE)
		gen_default_conf(&conf, default_config);

	if (!flash_file) {
		if (gen_eeprom_from_conf(&conf, buf) < 0)
			goto exit;
	}

	parse_eeprom_to_conf(&conf, buf);
	dump_config(&conf);

	fprintf(stderr, "Write new configuration to device [y/n]? ");

	while ((ch = getchar())) {
		if (ch != 'y')
			goto exit;
		else
			break;
	}

	r = rtlsdr_write_eeprom(dev, buf, 0, flash_file ? EEPROM_SIZE : 128);
	if (r < 0)
		fprintf(stderr, "Error while writing EEPROM: %i\n", r);
	else
		fprintf(stderr, "Configuration successfully written.\n");

exit:
	if (file)
		fclose(file);

	rtlsdr_close(dev);

	return r >= 0 ? r : -r;
}
