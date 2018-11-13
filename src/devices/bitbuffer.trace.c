// **** pretty print and trace utility functions ****

#include <stdarg.h>
#include "rtl_433.h"

// Declarations
char trace_label[LOCAL_TIME_BUFLEN + 48] = {0};

/// Get label for trace messages(test mode = in_filename, otherwise = timestamp).
/// Should be invoked  **before** either of the `..._trace` functions
char *get_trace_label();

/// Pretty print a buffer followed by a trace message
/// @param numbits : (max) number of bits printed, bits above numbits are masked
/// @param showbits : if true, pretty print bit strings as well as hex strings
/// @param format, ... : the trace message, set out as per printf
void buffer_trace(uint8_t *buffer,const uint16_t numbits, const bool showbits,
  char *format, ...);

/// Pretty print the rows in the bitbuffer with a trace message
/// @param showbits : if true, print bit strings as well as hex strings
/// @param format, ... : the trace message, set out as per printf
void bitbuffer_trace(bitbuffer_t *bits, const bool showbits, char *format, ...);


// Definitions

extern char *in_filename;

char *get_trace_label() {
    time_t current;
    if (in_filename) {
        strncpy(trace_label, in_filename, 48);
        trace_label[48] = '\0';
    } else {
        // Running realtime, use the current time string as a default label
        local_time_str(time(&current), &trace_label[0]);
    }
    return trace_label;
}

/// Format byte as bit string
/// @param numbits : bits above numbits are masked
/// @param result returns bitstring, requires 10 characters (inc the \0)
/// @return result
static char *mask_bitstr(const uint8_t byte, const uint16_t numbits, char *result) {
    uint8_t col = 0;
    for (uint16_t bit = 0; bit < 8; ++bit) {
        if (bit < numbits) {
            if (byte & (0x80 >> (bit % 8))) result[col] = '1';
            else result[col] = '0';
        } else result[col] = '-'; //mask bit
        col++;
        // Separator between nibbles
        if ((bit % 8) == 3) { result[col] = ' '; col++; }
    }
    result[col] = 0x00; // play safe
    return result;
}
/// Format byte as hexstr
/// @param numbits : bits above numbits are masked
/// @param result returns hexstring, requires 3 characters (inc the \0)
/// @return result
static char *mask_hexstr(const uint8_t byte, const uint16_t numbits, char *result) {
    uint8_t maskedbyte, maskshift;
    if (numbits >= 8) {
        maskedbyte = byte;
    } else {
        maskshift = (8 - numbits);
        maskedbyte = (byte >> maskshift) << maskshift;
    };
    snprintf(result, 3, "%02X", maskedbyte);
    return result;
}

/// Format byte as string
/// @param numbits : bits above numbits are masked
/// @param showbits : if true, include bit strings as well as hex strings
/// @param result returns hexstring, requires up to 13 characters (inc the \0)
/// @return result
static char *mask_bytestr(const uint8_t byte, const uint16_t numbits,
  const bool showbits, char *result) {
    char bitstr[10] = {0};
    char hexstr[3] = {0};
    if (showbits) mask_bitstr(byte, numbits, bitstr);
    mask_hexstr( byte, numbits, hexstr);
    snprintf (result, 13,  "%s %s", hexstr, bitstr);
    return result;
}


void buffer_trace(uint8_t *buffer, const uint16_t numbits, const bool showbits,
  char *format, ...) {
    uint16_t col, bitsleft;
    char masked_bytestr[14] = {0};
    va_list args;
    va_start(args, format);
    fprintf(stderr, "\n%s, ", trace_label);
    for (col = 0; col < (numbits+7)/8; ++col) {
        bitsleft = numbits - col * 8;
        mask_bytestr(buffer[col], bitsleft, showbits, masked_bytestr);
        //`leading tab character (\t) stops spreadsheet stripping leading zeros
        // trailing comma makes a "nicer" csv file
        if (showbits) fprintf (stderr, "\t%s, ", masked_bytestr);
        // else keep as close as possible to bitbuffer_print format
        else fprintf(stderr,"%s", masked_bytestr);
    }
    vfprintf(stderr, format, args);
    va_end(args);
}


void bitbuffer_trace(bitbuffer_t *bits, const bool showbits, char *format, ...) {
    uint16_t row;
    uint8_t *buffer;
    va_list args;
    va_list copied_args;
    va_start(args, format);
    for (row = 0; row < bits->num_rows; ++row) {
        buffer = bits->bb[row];
        buffer_trace(buffer,  bits->bits_per_row[row], showbits,
          "nr[%d] row[%d] nc[%d], ",
          bits->num_rows, row, bits->bits_per_row[row]);
        // Since args can be consumed when used, and I may loop more than once
        va_copy(copied_args, args);
        vfprintf(stderr, format, copied_args);
        va_end(copied_args);
    }
    va_end(args);
}


// End of pretty print and trace utilities
