/** @file
    SigMF file read and write support.

    Copyright (C) 2023 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_SIGMF_H_
#define INCLUDE_SIGMF_H_

#include <stdint.h>
#include <stdio.h>

#include "microtar.h"

/** A data structure for SigMF reader/writer related fields.
*/
typedef struct sigmf {
    char *datatype; ///< meta data
    uint32_t sample_rate; ///< meta data
    char *recorder; ///< meta data
    char *description; ///< meta data
    uint32_t first_sample_start; ///< meta data
    uint32_t first_frequency; ///< meta data

    //FILE *file; ///< data stream output
    uint32_t data_len; ///< data size, if known beforehand
    uint32_t data_offset; ///< data offset in the file
    mtar_t mtar; ///< base tar file
} sigmf_t;

/** Check if the given path is a valid .sigmf file name.
*/
int sigmf_valid_filename(char const *p);

/** A simple SigMF reader.

    Opens the file at @p path, reads the meta data and seeks to the data offset.

    Collections are not supported and a warning will be reported if encountered.
    Multiple streams per file are not supported and a warning will be reported if encountered.
*/
int sigmf_reader_open(sigmf_t *sigmf, char const *path);

/** Closes the SigMF reader.
*/
int sigmf_reader_close(sigmf_t *sigmf);

/** A simple SigMF writer.

    Creates the file at @p path, writes the meta data and seeks to the data offset.
*/
int sigmf_writer_open(sigmf_t *sigmf, char const *path, int overwrite);

/** Finalizes writing and closes the SigMF writer.
 */
int sigmf_writer_close(sigmf_t *sigmf);

/** Frees SigMF data items, but not the struct itself.
 */
int sigmf_free_items(sigmf_t *sigmf);

#endif /* INCLUDE_SIGMF_H_ */
