/* ics_hts.h -- the engine API: hts_engine API plus the ICS front end.
 *
 * Built into ics_hts.dll for the NVDA add-on (ctypes), and linked statically
 * into the SAPI voice, which defines ICS_EXPORT empty.
 */
#ifndef ICS_HTS_H
#define ICS_HTS_H

#include <stddef.h>

#include "ics_front.h"

#ifndef ICS_EXPORT
#define ICS_EXPORT __declspec(dllexport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HTS_Engine IcsEngine;

ICS_EXPORT IcsEngine *ics_open(const char *voice_path);
ICS_EXPORT void ics_close(IcsEngine *e);
ICS_EXPORT int ics_sample_rate(IcsEngine *e);
ICS_EXPORT int ics_fperiod(IcsEngine *e);
ICS_EXPORT void ics_set_vocoder(double noise_gain, int postfilter_voiced_only);
ICS_EXPORT void ics_stop(IcsEngine *e);

ICS_EXPORT IcsLexicon *ics_lexicon_open(const char *path, int accent);
ICS_EXPORT void ics_lexicon_close(IcsLexicon *lex);
ICS_EXPORT int ics_labels(IcsLexicon *lex, const char *text, int final_sentence, char **out);
ICS_EXPORT void ics_free(void *p);

/* Text (UTF-8) to speech; returns the number of samples, 0 for nothing to
   say, -1 on failure or stop. Samples stay in the engine until the next call. */
ICS_EXPORT int ics_speak(IcsEngine *e, IcsLexicon *lex, const char *text, int final_sentence,
                         double dur_factor, int frame_period, double half_tone, double beta,
                         int edge_ms, int pause_ms);
ICS_EXPORT void ics_get_samples(IcsEngine *e, short *out, double gain);
ICS_EXPORT void ics_trim_range(IcsEngine *e, double gain, int *start, int *end);

ICS_EXPORT int ics_convert_voice(const unsigned char *data, size_t len, const char *out_path,
                                 const char *source_name);

#ifdef __cplusplus
}
#endif

#endif
