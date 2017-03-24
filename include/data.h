/*
 * A general structure for extracting hierarchical data from the devices;
 * typically key-value pairs, but allows for more rich data as well.
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

#ifndef INCLUDE_DATA_H_
#define INCLUDE_DATA_H_

#include <stdio.h>

typedef enum {
	DATA_DATA,		/* pointer to data is stored */
	DATA_INT,		/* pointer to integer is stored */
	DATA_DOUBLE,		/* pointer to a double is stored */
	DATA_STRING,		/* pointer to a string is stored */
	DATA_ARRAY,		/* pointer to an array of values is stored */
	DATA_COUNT,		/* invalid */
	DATA_FORMAT		/* indicates the following value is formatted */
} data_type_t;

typedef struct data_array {
	int	     num_values;
	data_type_t  type;
	void	    *values;
} data_array_t;

typedef struct data {
	char	    *key;
	char	    *pretty_key; /* the name used for displaying data to user in with a nicer name */
	data_type_t  type;
	char        *format; /* if not null, contains special formatting string */
	void	    *value;
	struct data* next; /* chaining to the next element in the linked list; NULL indicates end-of-list */
} data_t;

struct data_printer;
extern struct data_printer data_json_printer;
extern struct data_printer data_kv_printer;
extern struct data_printer data_csv_printer;

/** Constructs a structured data object.

    Example:
    data_make("key", "Pretty key", DATA_INT, 42,
	      "others", "More data", DATA_DATA, data_make("foo", DATA_DOUBLE, 42.0, NULL),
	      "zoom", NULL, data_array(2, DATA_STRING, (char*[]){"hello", "World"}),
	      "double", "Double", DATA_DOUBLE, 10.0/3,
	      NULL);

    Most of the time the function copies perhaps what you expect it to. Things
    it copies:
    - string contents for keys and values
    - numerical arrays
    - string arrays (copied deeply)

    Things it moves:
    - recursive data_t* and data_array_t* values

    The rule is: if an object is boxed (look at the dmt structure in the data.c)
    and it has a array_elementwise_import in the same structure, then it is
    copied deeply. Otherwise, it is copied shallowly.

    @param key Name of the first value to put in.
    @param pretty_key Pretty name for the key. Use "" if to omit pretty label for this field completely,
                      or NULL if to use key name for it.
    @param type Type of the first value to put in.
    @param ... The value of the first value to put in, follwed by the rest of the
               key-type-values. The list is terminated with a NULL.

    @return A constructed data_t* object or NULL if there was a memory allocation error.
*/
data_t *data_make(const char *key, const char *pretty_key, ...);

/** Constructs an array from given data of the given uniform type.

    @param ptr The contents pointed by the argument are copied in.

    @return The constructed data array object, typically placed inside a data_t or NULL
            if there was a memory allocation error.
*/
data_array_t *data_array(int num_values, data_type_t type, void *ptr);

/** Releases a data array */
void data_array_free(data_array_t *array);

/** Prints a structured data object as JSON to the given stream */
void data_print(data_t *data, FILE* file, struct data_printer *printer, void *aux);

/** Releases a structure object */
void data_free(data_t *data);

/** Construct auxiliary data for CSV construction

    @param fields the list of fields to accept and expect. Array is copied, but the actual
                  strings not. The list may contain duplicates and they are eliminated.
    @param num_fields number of fields

    @return The auxiliary data to pass along with data_csv_printer to data_print.
            You must release this object with data_csv_free once you're done with it.
*/
void *data_csv_init(const char **fields, int num_fields);

/** Destructs auxiliary CSV data. */
void data_csv_free(void *csv);

#endif // INCLUDE_DATA_H_
