/**
 * Copyright (c) 2017 rxi
 * modified 2023 by Christian Zuckschwerdt
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `microtar.c` for details.
 */

#ifndef MICROTAR_H
#define MICROTAR_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdlib.h>

#define MTAR_VERSION "0.1.0"

enum {
  MTAR_ESUCCESS     =  0,
  MTAR_EFAILURE     = -1,
  MTAR_EOPENFAIL    = -2,
  MTAR_EREADFAIL    = -3,
  MTAR_EWRITEFAIL   = -4,
  MTAR_ESEEKFAIL    = -5,
  MTAR_EBADCHKSUM   = -6,
  MTAR_ENULLRECORD  = -7,
  MTAR_ENOTFOUND    = -8
};

enum {
  MTAR_TREG   = '0',
  MTAR_TLNK   = '1',
  MTAR_TSYM   = '2',
  MTAR_TCHR   = '3',
  MTAR_TBLK   = '4',
  MTAR_TDIR   = '5',
  MTAR_TFIFO  = '6',
  MTAR_TCONT  = '7', /* reserved */
  MTAR_TXHD   = 'x', /* Extended header referring to the next file in the archive */
  MTAR_TXGL   = 'g'  /* Global extended header */
};

typedef struct {
  unsigned mode;
  unsigned owner;
  unsigned group;
  unsigned size;
  unsigned mtime;
  unsigned type;
  unsigned devmajor; /* USTAR ext. */
  unsigned devminor; /* USTAR ext. */
  char name[101];
  char linkname[101];
  char uname[33]; /* USTAR ext. */
  char gname[33]; /* USTAR ext. */
} mtar_header_t;


typedef struct mtar_t mtar_t;

struct mtar_t {
  void *stream;
  unsigned pos;
  unsigned remaining_data;
  unsigned last_header;
};


const char* mtar_strerror(int err);

int mtar_open(mtar_t *tar, const char *filename, const char *mode);
int mtar_close(mtar_t *tar);

int mtar_seek(mtar_t *tar, unsigned pos);
int mtar_rewind(mtar_t *tar);
int mtar_next(mtar_t *tar);
int mtar_find(mtar_t *tar, const char *name, mtar_header_t *h);
int mtar_read_header(mtar_t *tar, mtar_header_t *h);
int mtar_read_data(mtar_t *tar, void *ptr, unsigned size);

int mtar_write_header(mtar_t *tar, const mtar_header_t *h);
int mtar_write_file_header(mtar_t *tar, const char *name, unsigned size);
int mtar_write_dir_header(mtar_t *tar, const char *name);
int mtar_write_data(mtar_t *tar, const void *data, unsigned size);
int mtar_finalize(mtar_t *tar);

#ifdef __cplusplus
}
#endif

#endif
