/* ics_hts.c -- C API over hts_engine API and the ICS front end, for the NVDA
   add-on (ctypes) and the SAPI voice. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "HTS_engine.h"
#include "ics_hts.h"
#include "ics_convert.h"

#define EXPORT ICS_EXPORT

extern double HTS_ics_noise_gain;
extern int HTS_ics_postfilter_voiced_only;

EXPORT HTS_Engine *ics_open(const char *voice_path)
{
   HTS_Engine *e = (HTS_Engine *) malloc(sizeof(HTS_Engine));
   char *voices[1];
   if (e == NULL)
      return NULL;
   voices[0] = (char *) voice_path;
   HTS_Engine_initialize(e);
   if (HTS_Engine_load(e, voices, 1) != TRUE) {
      HTS_Engine_clear(e);
      free(e);
      return NULL;
   }
   return e;
}

EXPORT void ics_close(HTS_Engine *e)
{
   if (e != NULL) {
      HTS_Engine_clear(e);
      free(e);
   }
}

EXPORT int ics_sample_rate(HTS_Engine *e)
{
   return (int) HTS_Engine_get_sampling_frequency(e);
}

EXPORT int ics_fperiod(HTS_Engine *e)
{
   return (int) HTS_Engine_get_fperiod(e);
}

EXPORT void ics_set_vocoder(double noise_gain, int postfilter_voiced_only)
{
   HTS_ics_noise_gain = noise_gain;
   HTS_ics_postfilter_voiced_only = postfilter_voiced_only;
}

EXPORT void ics_stop(HTS_Engine *e)
{
   HTS_Engine_set_stop_flag(e, TRUE);
}

/* Scale every state by 1/dur_factor, then cap label i to cap_ms[i] (0 = no
   cap), shrinking its states proportionally so long silences are never
   vocoded.  hts_engine's own speed control spreads the change by duration
   variance, which mostly stretches or squeezes the silences. */
static void set_durations(HTS_Engine *e, const int *cap_ms, int n, double dur_factor, size_t fperiod)
{
   HTS_SStreamSet *sss = &e->sss;
   size_t ns = sss->nstate, s, total = 0;
   size_t rate = HTS_Engine_get_sampling_frequency(e);
   int i;
   for (s = 0; s < sss->total_state; s++) {
      size_t d = (size_t) (sss->duration[s] / dur_factor + 0.5);
      sss->duration[s] = d < 1 ? 1 : d;
   }
   for (i = 0; i < n && (size_t) (i + 1) * ns <= sss->total_state; i++) {
      size_t *d = sss->duration + (size_t) i * ns;
      size_t sum = 0, cap = (size_t) cap_ms[i] * rate / 1000 / fperiod;
      for (s = 0; s < ns; s++)
         sum += d[s];
      if (cap_ms[i] > 0 && sum > cap) {
         for (s = 0; s < ns; s++) {
            d[s] = d[s] * cap / sum;
            if (d[s] < 1)
               d[s] = 1;
         }
      }
   }
   for (s = 0; s < sss->total_state; s++)
      total += sss->duration[s];
   sss->total_frame = total;
}

/* dur_factor > 1 shortens states; frame_period (samples) < the voice's
   shortens every frame evenly, which is how large speed-ups stay clean.
   Returns the number of samples, or -1 on failure/stop. */
EXPORT int ics_synth(HTS_Engine *e, char **labels, const int *cap_ms, int n, double dur_factor,
                     int frame_period, double half_tone, double beta)
{
   HTS_Engine_refresh(e);
   HTS_Engine_add_half_tone(e, half_tone);
   HTS_Engine_set_beta(e, beta);
   HTS_Engine_set_fperiod(e, (size_t) frame_period);
   if (HTS_Engine_generate_state_sequence_from_strings(e, labels, (size_t) n) != TRUE)
      return -1;
   set_durations(e, cap_ms, n, dur_factor, (size_t) frame_period);
   if (HTS_Engine_generate_parameter_sequence(e) != TRUE)
      return -1;
   if (HTS_Engine_generate_sample_sequence(e) != TRUE || HTS_Engine_get_stop_flag(e))
      return -1;
   return (int) HTS_Engine_get_nsamples(e);
}

static short sample_at(HTS_Engine *e, size_t i, double gain)
{
   double x = HTS_Engine_get_generated_speech(e, i) * gain;
   return (short) (x > 32767.0 ? 32767 : (x < -32768.0 ? -32768 : x));
}

EXPORT void ics_get_samples(HTS_Engine *e, short *out, double gain)
{
   size_t i, n = HTS_Engine_get_nsamples(e);
   for (i = 0; i < n; i++)
      out[i] = sample_at(e, i, gain);
}

/* Sample range [*start, *end) without the near-silent frames at either end,
   keeping one frame of lead-in; frames are 5 ms. */
EXPORT void ics_trim_range(HTS_Engine *e, double gain, int *start, int *end)
{
   size_t frame = HTS_Engine_get_sampling_frequency(e) / 200;
   size_t n = HTS_Engine_get_nsamples(e) / frame, f, i, first = 0, last = 0;
   double *energy, peak = 0.0, threshold;
   *start = 0;
   *end = (int) HTS_Engine_get_nsamples(e);
   if (n < 3 || (energy = (double *) malloc(n * sizeof(double))) == NULL)
      return;
   for (f = 0; f < n; f++) {
      double sum = 0.0;
      for (i = f * frame; i < (f + 1) * frame; i++) {
         double s = sample_at(e, i, gain);
         sum += s * s;
      }
      energy[f] = sum;
      if (sum > peak)
         peak = sum;
   }
   threshold = peak * 0.002;
   for (f = 0; f < n; f++)
      if (energy[f] > threshold) {
         first = f;
         break;
      }
   for (f = n; f-- > 0;)
      if (energy[f] > threshold) {
         last = f;
         break;
      }
   free(energy);
   *start = (int) ((first > 0 ? first - 1 : 0) * frame);
   *end = (int) ((last + 2 < n ? last + 2 : n) * frame);
}

/* ------------------------------------------------------- text front end */

EXPORT IcsLexicon *ics_lexicon_open(const char *path, int accent)
{
   return icsf_lexicon_load(path, accent);
}

EXPORT void ics_lexicon_close(IcsLexicon *lex)
{
   icsf_lexicon_free(lex);
}

/* Labels joined by '\n' in *out (free with ics_free); returns the count. */
EXPORT int ics_labels(IcsLexicon *lex, const char *text, int final_sentence, char **out)
{
   return icsf_text_to_labels(lex, text, final_sentence, out);
}

EXPORT void ics_free(void *p)
{
   free(p);
}

/* Flint voice bytes (assets/<lang>/voice) to an .htsvoice; ICSC_* result. */
EXPORT int ics_convert_voice(const unsigned char *data, size_t len, const char *out_path, const char *source_name)
{
   return icsc_convert(data, len, out_path, source_name);
}

static int is_silence(const char *label)
{
   const char *dash = strchr(label, '-');
   return dash && strncmp(dash + 1, "sil+", 4) == 0;
}

/* Text to speech: front end, silence caps (edge_ms for the utterance's first
   and last silence, pause_ms for the others), then ics_synth.  Returns the
   number of samples, 0 for text with nothing to say, or -1 on failure/stop. */
EXPORT int ics_speak(HTS_Engine *e, IcsLexicon *lex, const char *text, int final_sentence,
                     double dur_factor, int frame_period, double half_tone, double beta,
                     int edge_ms, int pause_ms)
{
   char *buf = NULL, **labels = NULL, *p;
   int *caps = NULL, n, i, result = -1;
   n = icsf_text_to_labels(lex, text, final_sentence, &buf);
   if (n < 0)
      return -1;
   if (n <= 2) {
      free(buf);
      return 0;
   }
   labels = (char **) malloc(sizeof(char *) * (size_t) n);
   caps = (int *) malloc(sizeof(int) * (size_t) n);
   if (labels && caps) {
      for (i = 0, p = buf; i < n; i++) {
         labels[i] = p;
         p = strchr(p, '\n');
         if (p)
            *p++ = '\0';
         else
            p = labels[i] + strlen(labels[i]);
         if (!is_silence(labels[i]))
            caps[i] = 0;
         else if (i == 0 || i == n - 1)
            caps[i] = edge_ms > 1 ? edge_ms : 1;
         else
            caps[i] = pause_ms;
      }
      result = ics_synth(e, labels, caps, n, dur_factor, frame_period, half_tone, beta);
   }
   free(labels);
   free(caps);
   free(buf);
   return result;
}
