/** @file
    Meta include for all decoders.
*/

#ifndef INCLUDE_DECODER_H_
#define INCLUDE_DECODER_H_

#include <string.h>
#include "r_device.h"
#include "bitbuffer.h"
#include "data.h"
#include "util.h"
#include "decoder_util.h"

/* TODO: temporary allow to change to new style model keys */
#define _X(n, o) (decoder->new_model_keys ? (n) : (o))

#endif /* INCLUDE_DECODER_H_ */
