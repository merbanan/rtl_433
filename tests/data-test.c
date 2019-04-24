/*
 * A general structure for extracting hierarchical data from the devices;
 * typically key-value pairs, but allows for more rich data as well
 *
 * Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>
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

#include <stdio.h>

#include "data.h"

int main()
{
	data_t *data = data_make("label"      , "",		DATA_STRING, "1.2.3",
				 "house_code" , "House Code",	DATA_INT, 42,
				 "temp"	      , "Temperature",	DATA_DOUBLE, 99.9,
				 "array"      , "Array",	DATA_ARRAY, data_array(2, DATA_STRING, (char*[2]){"hello", "world"}),
				 "array2"     , "Array 2",	DATA_ARRAY, data_array(2, DATA_INT, (int[2]){4, 2}),
				 "array3"     , "Array 3",	DATA_ARRAY, data_array(2, DATA_ARRAY, (data_array_t*[2]){
				 				 data_array(2, DATA_INT, (int[2]){4, 2}),
				 					 data_array(2, DATA_INT, (int[2]){5, 5}) }),
				 "data"       , "Data",        DATA_DATA, data_make("Hello", "hello", DATA_STRING, "world", NULL),
				 NULL);
	const char *fields[] = { "label", "house_code", "temp", "array", "array2", "array3", "data", "house_code" };

	void *json_output = data_output_json_create(stdout);
	void *kv_output = data_output_kv_create(stdout);
	void *csv_output = data_output_csv_create(stdout);
	data_output_start(csv_output, fields, sizeof fields / sizeof *fields);

	data_output_print(json_output, data); fprintf(stdout, "\n");
	data_output_print(kv_output, data);
	data_output_print(csv_output, data);

	data_output_free(json_output);
	data_output_free(kv_output);
	data_output_free(csv_output);

	data_free(data);
}
