/* ics_front.h -- text to Flint (Android 4.0 Google TTS) full-context labels.
 *
 * A port of the add-on's reference front end (tests/reference/_icsfront.py):
 * CMUdict pronunciations mapped onto the voice's phone set (General American
 * for eng-usa, RP for eng-gbr), NRL letter-to-sound rules for unknown words,
 * syllabification by the voice's own phonotactics, and the label fields from
 * the APK's compile_cfg.
 */
#ifndef ICS_FRONT_H
#define ICS_FRONT_H

#include <stddef.h>

#define ICSF_ACCENT_US 0
#define ICSF_ACCENT_GB 1

typedef struct IcsLexicon IcsLexicon;

/* Load a CMUdict-format file (path in UTF-8). NULL on failure. */
IcsLexicon *icsf_lexicon_load(const char *path, int accent);
void icsf_lexicon_free(IcsLexicon *lex);

/* Labels for UTF-8 text, joined by '\n', in a malloc'd buffer (*out, free
 * with free()). final_sentence: an unpunctuated end is sentence-final (1) or
 * continues (0). Returns the number of labels, or -1 on allocation failure. */
int icsf_text_to_labels(const IcsLexicon *lex, const char *text, int final_sentence, char **out);

#endif
