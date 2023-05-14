/** @file
    Option parsing functions to complement getopt.

    Copyright (C) 2017 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OPTPARSE_H_
#define INCLUDE_OPTPARSE_H_

#include <stdint.h>

// makes strcasecmp() and strncasecmp() available when including optparse.h
#ifdef _MSC_VER
    #include <string.h>
    #define strcasecmp(s1,s2)     _stricmp(s1,s2)
    #define strncasecmp(s1,s2,n)  _strnicmp(s1,s2,n)
#else
    #include <strings.h>
#endif

/// TLS settings.
typedef struct tls_opts {
    /// Client certificate to present to the server.
    char const *tls_cert;
    /// Private key corresponding to the certificate.
    /// If tls_cert is set but tls_key is not, tls_cert is used.
    char const *tls_key;
    /// Verify server certificate using this CA bundle. If set to "*", then TLS
    /// is enabled but no cert verification is performed.
    char const *tls_ca_cert;
    /// Colon-delimited list of acceptable cipher suites.
    /// Names depend on the library used, for example:
    /// ECDH-ECDSA-AES128-GCM-SHA256:DHE-RSA-AES128-SHA256 (OpenSSL)
    /// For OpenSSL the list can be obtained by running "openssl ciphers".
    /// If NULL, a reasonable default is used.
    char const *tls_cipher_suites;
    /// Server name verification. If tls_ca_cert is set and the certificate has
    /// passed verification, its subject will be verified against this string.
    /// By default (if tls_server_name is NULL) hostname part of the address will
    /// be used. Wildcard matching is supported. A special value of "*" disables
    /// name verification.
    char const *tls_server_name;
    /// PSK identity is a NUL-terminated string.
    /// Note: Default list of cipher suites does not include PSK suites, if you
    /// want to use PSK you will need to set tls_cipher_suites as well.
    char const *tls_psk_identity;
    /// PSK key hex string, must be either 16 or 32 bytes (32 or 64 hex digits)
    /// for AES-128 or AES-256 respectively.
    char const *tls_psk_key;
} tls_opts_t;

/// Parse a TLS option.
///
/// @sa tls_opts_t
/// @return 0 if the option was valid, error code otherwise
int tls_param(tls_opts_t *tls_opts, char const *key, char const *val);

/// Convert string to bool with fallback default.
/// Parses "true", "yes", "on", "enable" (not case-sensitive) to 1, atoi() otherwise.
int atobv(char const *arg, int def);

/// Convert string to int with fallback default.
int atoiv(char const *arg, int def);

/// Get the next colon or comma separated arg, NULL otherwise.
/// Returns string including comma if a comma is found first,
/// otherwise string after colon if found, NULL otherwise.
char *arg_param(char const *arg);

/// Convert a string with optional leading equals char to a double.
///
/// Parse errors will fprintf(stderr, ...) and exit(1).
///
/// @param str character string to parse
/// @param error_hint prepended to error output
/// @return parsed number value
double arg_float(char const *str, char const *error_hint);

/// Parse param string to host and port.
/// E.g. ":514", "localhost", "[::1]", "127.0.0.1:514", "[::1]:514",
/// also "//localhost", "//localhost:514", "//:514".
/// Host or port are terminated at a comma, if found.
/// @return the remaining options
char *hostport_param(char *param, char const **host, char const **port);

/// Convert a string to an unsigned integer, uses strtod() and accepts
/// metric suffixes of 'k', 'M', and 'G' (also 'K', 'm', and 'g').
///
/// Parse errors will fprintf(stderr, ...) and exit(1).
///
/// @param str character string to parse
/// @param error_hint prepended to error output
/// @return parsed number value
uint32_t atouint32_metric(char const *str, char const *error_hint);

/// Convert a string to an integer, uses strtod() and accepts
/// time suffixes of 'd', 'h', 'm', and 's' (also 'D', 'H', 'M', and 'S'),
/// or the form hours:minutes[:seconds].
///
/// Parse errors will fprintf(stderr, ...) and exit(1).
///
/// @param str character string to parse
/// @param error_hint prepended to error output
/// @return parsed number value in seconds
int atoi_time(char const *str, char const *error_hint);

/// Similar to strsep.
///
/// @param[in,out] stringp String to parse inplace
/// @param delim the delimiter character
/// @return the original value of *stringp
char *asepc(char **stringp, char delim);

/// Similar to strsep but bounded by a stop character.
///
/// @param[in,out] stringp String to parse inplace
/// @param delim the delimiter character
/// @param stop the bounding character at which to stop
/// @return the original value of *stringp
char *asepcb(char **stringp, char delim, char stop);

/// Match the first key in a comma-separated list of key/value pairs.
///
/// @param s String of key=value pairs, separated by commas
/// @param key keyword argument to match with
/// @param[out] val value if found, NULL otherwise
/// @return 1 if the key matches exactly, 0 otherwise
int kwargs_match(char const *s, char const *key, char const **val);

/// Skip the first key/value in a comma-separated list of key/value pairs.
///
/// @param s String of key=value pairs, separated by commas
/// @return the next key in s, end of string or NULL otherwise
char const *kwargs_skip(char const *s);

/// Parse a comma-separated list of key/value pairs into kwargs.
///
/// The input string will be modified and the pointer advanced.
/// The key and val pointers will be into the original string.
///
/// @param[in,out] s String of key=value pairs, separated by commas
/// @param[out] key keyword argument if found, NULL otherwise
/// @param[out] val value if found, NULL otherwise
/// @return the original value of *stringp (the keyword found)
char *getkwargs(char **s, char **key, char **val);

/// Trim left and right whitespace in string.
///
/// @param[in,out] str String to change inplace
/// @return the trimmed value of str
char *trim_ws(char *str);

/// Remove all whitespace from string.
///
/// @param[in,out] str String to change inplace
/// @return the stripped value of str
char *remove_ws(char *str);

#endif /* INCLUDE_OPTPARSE_H_ */
