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

#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "data.h"

typedef void* (*array_elementwise_import_fn)(void*);
typedef void* (*array_element_release_fn)(void*);
typedef void* (*value_release_fn)(void*);

typedef struct {
	 /* what is the element size when put inside an array? */
	int			    array_element_size;

	/* is the element boxed (ie. behind a pointer) when inside an array?
	   if it's not boxed ("unboxed"), json dumping function needs to make
	   a copy of the value beforehand, because the dumping function only
	   deals with boxed values.
	 */
	_Bool			    array_is_boxed;

	/* function for importing arrays. strings are specially handled (as they
	   are copied deeply), whereas other arrays are just copied shallowly
	   (but copied nevertheles) */
	array_elementwise_import_fn array_elementwise_import;

	/* a function for releasing an element when put in an array; integers
	 * don't need to be released, while ie. strings and arrays do. */
	array_element_release_fn    array_element_release;

	/* a function for releasing a value. everything needs to be released. */
	value_release_fn	    value_release;
} data_meta_type_t;

struct data_printer_context;

typedef struct data_printer {
	void (*print_data)(struct data_printer_context *printer_ctx, data_t *data, char *format, FILE *file);
	void (*print_array)(struct data_printer_context *printer_ctx, data_array_t *data, char *format, FILE *file);
	void (*print_string)(struct data_printer_context *printer_ctx, const char *data, char *format, FILE *file);
	void (*print_double)(struct data_printer_context *printer_ctx, double data, char *format, FILE *file);
	void (*print_int)(struct data_printer_context *printer_ctx, int data, char *format, FILE *file);
} data_printer_t;

typedef struct data_printer_context {
	data_printer_t *printer;
	void           *aux;
} data_printer_context_t;

static data_meta_type_t dmt[DATA_COUNT] = {
	//  DATA_DATA
	{ .array_element_size       = sizeof(data_t*),
	  .array_is_boxed           = true,
	  .array_elementwise_import = NULL,
	  .array_element_release    = (array_element_release_fn) data_free,
	  .value_release            = (value_release_fn) data_free },

	//  DATA_INT
	{ .array_element_size       = sizeof(int),
	  .array_is_boxed           = false,
	  .array_elementwise_import = NULL,
	  .array_element_release    = NULL,
	  .value_release            = (value_release_fn) free },

	//  DATA_DOUBLE
	{ .array_element_size       = sizeof(double),
	  .array_is_boxed           = false,
	  .array_elementwise_import = NULL,
	  .array_element_release    = NULL,
	  .value_release            = (value_release_fn) free },

	//  DATA_STRING
	{ .array_element_size       = sizeof(char*),
	  .array_is_boxed           = true,
	  .array_elementwise_import = (array_elementwise_import_fn) strdup,
	  .array_element_release    = (array_element_release_fn) free,
	  .value_release            = (value_release_fn) free },

	//  DATA_ARRAY
	{ .array_element_size       = sizeof(data_array_t*),
	  .array_is_boxed           = true,
	  .array_elementwise_import = NULL,
	  .array_element_release    = (array_element_release_fn) data_array_free ,
	  .value_release            = (value_release_fn) data_array_free },
};

static void print_json_data(data_printer_context_t *printer_ctx, data_t *data, char *format, FILE *file);
static void print_json_array(data_printer_context_t *printer_ctx, data_array_t *data, char *format, FILE *file);
static void print_json_string(data_printer_context_t *printer_ctx, const char *data, char *format, FILE *file);
static void print_json_double(data_printer_context_t *printer_ctx, double data, char *format, FILE *file);
static void print_json_int(data_printer_context_t *printer_ctx, int data, char *format, FILE *file);

static void print_kv_data(data_printer_context_t *printer_ctx, data_t *data, char *format, FILE *file);
static void print_kv_string(data_printer_context_t *printer_ctx, const char *data, char *format, FILE *file);
static void print_kv_double(data_printer_context_t *printer_ctx, double data, char *format, FILE *file);
static void print_kv_int(data_printer_context_t *printer_ctx, int data, char *format, FILE *file);

typedef struct {
	const char **fields;
	int          data_recursion;
	const char*  separator;
} data_csv_aux_t;

static void print_csv_data(data_printer_context_t *printer_ctx, data_t *data, char *format, FILE *file);
static void print_csv_string(data_printer_context_t *printer_ctx, const char *data, char *format, FILE *file);

data_printer_t data_json_printer = {
	.print_data   = print_json_data,
	.print_array  = print_json_array,
	.print_string = print_json_string,
	.print_double = print_json_double,
	.print_int    = print_json_int
};

data_printer_t data_kv_printer = {
	.print_data   = print_kv_data,
	.print_array  = print_json_array,
	.print_string = print_kv_string,
	.print_double = print_kv_double,
	.print_int    = print_kv_int
};

data_printer_t data_csv_printer = {
	.print_data   = print_csv_data,
	.print_array  = print_json_array,
	.print_string = print_csv_string,
	.print_double = print_json_double,
	.print_int    = print_json_int
};

static _Bool import_values(void* dst, void* src, int num_values, data_type_t type) {
	int element_size = dmt[type].array_element_size;
	array_elementwise_import_fn import = dmt[type].array_elementwise_import;
	if (import) {
		for (int i = 0; i < num_values; ++i) {
			void *copy = import(*(void**)(src + element_size * i));
			if (!copy) {
				--i;
				while (i >= 0) {
					free(*(void**)(dst + element_size * i));
					--i;
				}
				return false;
			} else {
				*((char**) dst + i) = copy;
			}

		}
	} else {
		memcpy(dst, src, element_size * num_values);
	}
	return true; // error is returned early
}

data_array_t *data_array(int num_values, data_type_t type, void *values) {
	data_array_t *array = calloc(1, sizeof(data_array_t));
	if (array) {
		int element_size = dmt[type].array_element_size;
		array->values = calloc(num_values, element_size);
		if (!array->values)
			goto alloc_error;
		if (!import_values(array->values, values, num_values, type))
			goto alloc_error;

		array->num_values = num_values;
		array->type = type;
	}
	return array;

 alloc_error:
	if (array)
		free(array->values);
	free(array);
	return NULL;
}

data_t *data_make(const char *key, const char *pretty_key, ...) {
	va_list ap;
	data_type_t type;
	va_start(ap, pretty_key);

	data_t *first = NULL;
	data_t *prev = NULL;
	char* format = false;
	type = va_arg(ap, data_type_t);
	do {
		data_t *current;
		void *value = NULL;

		switch (type) {
		case DATA_FORMAT : {
			format = strdup(va_arg(ap, char*));
			if (!format)
				goto alloc_error;
			type = va_arg(ap, data_type_t);
			continue;
		} break;
		case DATA_COUNT  : {
			assert(0);
		} break;
		case DATA_DATA   : {
			value = va_arg(ap, data_t*);
		} break;
		case DATA_INT    : {
			value = malloc(sizeof(int));
			if (value)
				*(int*) value = va_arg(ap, int);
		} break;
		case DATA_DOUBLE : {
			value = malloc(sizeof(double));
			if (value)
				*(double*) value = va_arg(ap, double);
		} break;
		case DATA_STRING : {
			value = strdup(va_arg(ap, char*));
		} break;
		case DATA_ARRAY  : {
			value = va_arg(ap, data_t*);
		} break;
		}

		// also some null arguments are mapped to an alloc error;
		// that's ok, because they originate (typically..) from
		// an alloc error anyway
		if (!value)
			goto alloc_error;

		current = calloc(1, sizeof(*current));
		if (!current)
			goto alloc_error;
		if (prev)
			prev->next = current;

		current->key = strdup(key);
		if (!current->key)
			goto alloc_error;
		current->pretty_key = strdup(pretty_key ? pretty_key : key);
		if (!current->pretty_key)
			goto alloc_error;
		current->type = type;
		current->format = format;
		current->value = value;
		current->next = NULL;

		prev = current;
		if (!first)
			first = current;

		key = va_arg(ap, const char*);
		if (key) {
			pretty_key = va_arg(ap, const char*);
			type = va_arg(ap, data_type_t);
			format = NULL;
		}
	} while (key);
	va_end(ap);

	return first;

 alloc_error:
	data_free(first);
	va_end(ap);
	return NULL;
}

void data_array_free(data_array_t *array) {
	array_element_release_fn release = dmt[array->type].array_element_release;
	if (release) {
		int element_size = dmt[array->type].array_element_size;
		for (int i = 0; i < array->num_values; ++i) 
			release(*(void**)(array->values + element_size * i));
	}
	free(array->values);
	free(array);
}

void data_free(data_t *data) {
	while (data) {
		data_t *prev_data = data;
		if (dmt[data->type].value_release)
			dmt[data->type].value_release(data->value);
		free(data->format);
		free(data->pretty_key);
		free(data->key);
		data = data->next;
		free(prev_data);
	}
}

void data_print(data_t* data, FILE *file, data_printer_t *printer, void *aux)
{
	data_printer_context_t ctx = {
		.printer = printer,
		.aux     = aux
	};
	ctx.printer->print_data(&ctx, data, NULL, file);
	if (file) {
		fputc('\n', file);
		fflush(file);
	}
}

static void print_value(data_printer_context_t *printer_ctx, FILE *file, data_type_t type, void *value, char *format) {
	switch (type) {
	case DATA_FORMAT :
	case DATA_COUNT : {
		assert(0);
	} break;
	case DATA_DATA : {
		printer_ctx->printer->print_data(printer_ctx, value, format, file);
	} break;
	case DATA_INT : {
		printer_ctx->printer->print_int(printer_ctx, *(int*) value, format, file);
	} break;
	case DATA_DOUBLE : {
		printer_ctx->printer->print_double(printer_ctx, *(double*) value, format, file);
	} break;
	case DATA_STRING : {
		printer_ctx->printer->print_string(printer_ctx, value, format, file);
	} break;
	case DATA_ARRAY : {
		printer_ctx->printer->print_array(printer_ctx, value, format, file);
	} break;
	}
}

/* JSON printer */
static void print_json_array(data_printer_context_t *printer_ctx, data_array_t *array, char *format, FILE *file) {
	int element_size = dmt[array->type].array_element_size;
	char buffer[element_size];
	fprintf(file, "[");
	for (int c = 0; c < array->num_values; ++c) {
		if (c)
			fprintf(file, ", ");
		if (!dmt[array->type].array_is_boxed) {
			memcpy(buffer, (void**)(array->values + element_size * c), element_size);
			print_value(printer_ctx, file, array->type, buffer, format);
		} else {
			print_value(printer_ctx, file, array->type, *(void**)(array->values + element_size * c), format);
		}
	}
	fprintf(file, "]");
}

static void print_json_data(data_printer_context_t *printer_ctx, data_t *data, char *format, FILE *file)
{
	_Bool separator = false;
	fputc('{', file);
	while (data) {
		if (separator)
			fprintf(file, ", ");
		printer_ctx->printer->print_string(printer_ctx, data->key, NULL, file);
		fprintf(file, " : ");
		print_value(printer_ctx, file, data->type, data->value, data->format);
		separator = true;
		data = data->next;
	}
	fputc('}', file);
}

static void print_json_string(data_printer_context_t *printer_ctx, const char *str, char *format, FILE *file) {
	fprintf(file, "\"");
	while (*str) {
		if (*str == '"')
			fputc('\\', file);
		fputc(*str, file);
		++str;
	}
	fprintf(file, "\"");
}

static void print_json_double(data_printer_context_t *printer_ctx, double data, char *format, FILE *file)
{
	fprintf(file, "%.3f", data);
}

static void print_json_int(data_printer_context_t *printer_ctx, int data, char *format, FILE *file)
{
	fprintf(file, "%d", data);
}

/* Key-Value printer */
static void print_kv_data(data_printer_context_t *printer_ctx, data_t *data, char *format, FILE *file)
{
	_Bool separator = false;
	_Bool was_labeled = false;
	_Bool written_title = false;
	while (data) {
		_Bool labeled = data->pretty_key[0];
		/* put a : between the first non-labeled and labeled */
		if (separator) {
			if (labeled && !was_labeled && !written_title) {
				fprintf(file, "\n");
				written_title = true;
				separator = false;
			} else {
				if (was_labeled)
					fprintf(file, "\n");
				else
					fprintf(file, " ");
			}
		}
		if (!strcmp(data->key, "time"))
                    /* fprintf(file, "") */ ;
                else if (!strcmp(data->key, "model"))
                    fprintf(file, ":\t");
                else
                    fprintf(file, "\t%s:\t", data->pretty_key);
		if (labeled)
			fputc(' ', file);
		print_value(printer_ctx, file, data->type, data->value, data->format);
		separator = true;
		was_labeled = labeled;
		data = data->next;
	}
}

static void print_kv_double(data_printer_context_t *printer_ctx, double data, char *format, FILE *file)
{
	fprintf(file, format ? format : "%.3f", data);
}

static void print_kv_int(data_printer_context_t *printer_ctx, int data, char *format, FILE *file)
{
	fprintf(file, format ? format : "%d", data);
}


static void print_kv_string(data_printer_context_t *printer_ctx, const char *data, char *format, FILE *file)
{
	fprintf(file, format ? format : "%s", data);
}

/* CSV printer; doesn't really support recursive data objects yes */
static void print_csv_data(data_printer_context_t *printer_ctx, data_t *data, char *format, FILE *file)
{
	data_csv_aux_t *csv = printer_ctx->aux;
	const char **fields = csv->fields;
	int i;

	if (csv->data_recursion)
		return;

	++csv->data_recursion;
	for (i = 0; fields[i]; ++i) {
		const char *key = fields[i];
		data_t *found = NULL;
		if (i) fprintf(file, "%s", csv->separator);
		for (data_t *iter = data; !found && iter; iter = iter->next)
			if (strcmp(iter->key, key) == 0)
				found = iter;

		if (found)
			print_value(printer_ctx, file, found->type, found->value, found->format);
	}
	--csv->data_recursion;
}

static void print_csv_string(data_printer_context_t *printer_ctx, const char *str, char *format, FILE *file)
{
	data_csv_aux_t *csv = printer_ctx->aux;
	while (*str) {
		if (strncmp(str,  csv->separator, strlen(csv->separator)) == 0)
			fputc('\\', file);
		fputc(*str, file);
		++str;
	}
}

static int compare_strings(char** a, char** b)
{
    return strcmp(*a, *b);
}

void *data_csv_init(const char **fields, int num_fields)
{
	data_csv_aux_t *csv = calloc(1, sizeof(data_csv_aux_t));
	int csv_fields = 0;
	int i, j;
	const char **allowed = NULL;
	int *use_count = NULL;
	int num_unique_fields;
	if (!csv)
		goto alloc_error;

	csv->separator = ",";

	allowed = calloc(num_fields, sizeof(const char*));
	memcpy(allowed, fields, sizeof(const char*) * num_fields);

	qsort(allowed, num_fields, sizeof(char*), (void*) compare_strings);

	// overwrite duplicates
	i = 0;
	j = 0;
	while (j < num_fields) {
		while (j > 0 && j < num_fields &&
		       strcmp(allowed[j - 1], allowed[j]) == 0)
			++j;

		if (j < num_fields) {
			allowed[i] = allowed[j];
			++i;
			++j;

		}
	}
	num_unique_fields = i;

	csv->fields = calloc(num_unique_fields + 1, sizeof(const char**));
	if (!csv->fields)
		goto alloc_error;

	use_count = calloc(num_unique_fields, sizeof(*use_count));
	if (!use_count)
		goto alloc_error;

	for (i = 0; i < num_fields; ++i) {
		const char **field = bsearch(&fields[i], allowed, num_unique_fields, sizeof(const char*),
					     (void*) compare_strings);
		int *field_use_count = use_count + (field - allowed);
		if (field && !*field_use_count) {
			csv->fields[csv_fields] = fields[i];
			++csv_fields;
			++*field_use_count;
		}
	}
	csv->fields[csv_fields] = NULL;
	free(allowed);
	free(use_count);

	// Output the CSV header
	for (i = 0; csv->fields[i]; ++i) {
		printf("%s%s", i > 0 ? csv->separator : "", csv->fields[i]);
	}
	printf("\n");
	return csv;

 alloc_error:
	free(use_count);
	free(allowed);
	if (csv) free(csv->fields);
	free(csv);
	return NULL;
}

void data_csv_free(void *aux)
{
	data_csv_aux_t *csv = aux;
	free(csv->fields);
	free(csv);
}
