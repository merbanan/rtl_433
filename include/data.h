/** @file
    A general structure for extracting hierarchical data from the devices;
    typically key-value pairs, but allows for more rich data as well.

    Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INCLUDE_DATA_H_
#define INCLUDE_DATA_H_

#if defined _WIN32 || defined __CYGWIN__
    #if defined data_EXPORTS
        #define R_API __stdcall __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #elif defined data_IMPORTS
        #define R_API __stdcall __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #else
        #define R_API // for static linking
    #endif
    #define R_API_CALLCONV __stdcall
#else
    #if __GNUC__ >= 4
        #define R_API __attribute__((visibility ("default")))
    #else
        #define R_API
    #endif
    #define R_API_CALLCONV
#endif

#include <stddef.h>

typedef enum {
    DATA_DATA,   /**< pointer to data is stored */
    DATA_INT,    /**< pointer to integer is stored */
    DATA_DOUBLE, /**< pointer to a double is stored */
    DATA_STRING, /**< pointer to a string is stored */
    DATA_ARRAY,  /**< pointer to an array of values is stored */
    DATA_COUNT,  /**< invalid */
    DATA_FORMAT, /**< indicates the following value is formatted */
    DATA_COND,   /**< add data only if condition is true, skip otherwise */
} data_type_t;

typedef struct data_array {
    int         num_values;
    data_type_t type;
    void        *values;
} data_array_t;

// Note: Do not unwrap a packed array to data_value_t,
// on 32-bit the union has different size/alignment than a pointer.
typedef union data_value {
    int         v_int;  /**< A data value of type int, 4 bytes size/alignment */
    double      v_dbl;  /**< A data value of type double, 8 bytes size/alignment */
    void        *v_ptr; /**< A data value pointer, 4/8 bytes size/alignment */
} data_value_t;

typedef struct data {
    struct data *next; /**< chaining to the next element in the linked list; NULL indicates end-of-list */
    char        *key;
    char        *pretty_key; /**< the name used for displaying data to user in with a nicer name */
    char        *format; /**< if not null, contains special formatting string */
    data_value_t value;
    data_type_t type;
    unsigned    retain; /**< incremented on data_retain, data_free only frees if this is zero */
} data_t;

/** Constructs a structured data object.

    Example:
    data_make(
            "key",      "Pretty key",   DATA_INT, 42,
            "others",   "More data",    DATA_DATA, data_make("foo", DATA_DOUBLE, 42.0, NULL),
            "zoom",     NULL,           data_array(2, DATA_STRING, (char*[]){"hello", "World"}),
            "double",   "Double",       DATA_DOUBLE, 10.0/3,
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
    @param ... Type and then value of the item to put in, followed by the rest of the
               key-type-values. The list is terminated with a NULL.

    @return A constructed data_t* object or NULL if there was a memory allocation error.
*/
R_API data_t *data_make(const char *key, const char *pretty_key, ...);

/** Adds to a structured data object, by appending data.

    @see data_make()
*/
R_API data_t *data_append(data_t *first, const char *key, const char *pretty_key, ...);

/** Adds to a structured data object, by prepending data.

    @see data_make()
*/
R_API data_t *data_prepend(data_t *first, const char *key, const char *pretty_key, ...);

/** Constructs an array from given data of the given uniform type.

    @param num_values The number of values to be copied.
    @param type The type of values to be copied.
    @param ptr The contents pointed by the argument are copied in.

    @return The constructed data array object, typically placed inside a data_t or NULL
            if there was a memory allocation error.
*/
R_API data_array_t *data_array(int num_values, data_type_t type, void const *ptr);

/** Releases a data array. */
R_API void data_array_free(data_array_t *array);

/** Retain a structure object, returns the structure object passed in. */
R_API data_t *data_retain(data_t *data);

/** Releases a structure object if retain is zero, decrement retain otherwise. */
R_API void data_free(data_t *data);

struct data_output;

typedef struct data_output {
    void (R_API_CALLCONV *print_data)(struct data_output *output, data_t *data, char const *format);
    void (R_API_CALLCONV *print_array)(struct data_output *output, data_array_t *data, char const *format);
    void (R_API_CALLCONV *print_string)(struct data_output *output, const char *data, char const *format);
    void (R_API_CALLCONV *print_double)(struct data_output *output, double data, char const *format);
    void (R_API_CALLCONV *print_int)(struct data_output *output, int data, char const *format);
    void (R_API_CALLCONV *output_start)(struct data_output *output, char const *const *fields, int num_fields);
    void (R_API_CALLCONV *output_print)(struct data_output *output, data_t *data);
    void (R_API_CALLCONV *output_free)(struct data_output *output);
    int log_level; ///< the maximum log level (verbosity) allowed, more verbose messages must be ignored.
} data_output_t;

/** Setup known field keys and start output, used by CSV only.

    @param output the data_output handle from data_output_x_create
    @param fields the list of fields to accept and expect. Array is copied, but the actual
                  strings not. The list may contain duplicates and they are eliminated.
    @param num_fields number of fields
*/
R_API void data_output_start(struct data_output *output, char const *const *fields, int num_fields);

/** Prints a structured data object, flushes the output if applicable. */
R_API void data_output_print(struct data_output *output, data_t *data);

R_API void data_output_free(struct data_output *output);

/* data output helpers */

R_API void print_value(data_output_t *output, data_type_t type, data_value_t value, char const *format);

R_API void print_array_value(data_output_t *output, data_array_t *array, char const *format, int idx);

R_API size_t data_print_jsons(data_t *data, char *dst, size_t len);

#endif // INCLUDE_DATA_H_
