/* ics_convert.h -- Flint voice (from an Android 4.0 GoogleTTS.apk) to .htsvoice. */
#ifndef ICS_CONVERT_H
#define ICS_CONVERT_H

#include <stddef.h>

#define ICSC_OK 0
#define ICSC_ERR_FORMAT 1 /* not a Flint voice this converter understands */
#define ICSC_ERR_WRITE 2
#define ICSC_ERR_MEMORY 3

/* Convert the bytes of assets/<lang>/voice into an hts_engine API .htsvoice
   at out_path. source_name goes into the header's COMMENT line. */
int icsc_convert(const unsigned char *data, size_t len, const char *out_path, const char *source_name);

#endif
