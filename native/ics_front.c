/* ics_front.c -- text to Flint full-context labels; see ics_front.h.
 *
 * Kept line-for-line equivalent to tools/reference/_icsfront.py and _nrl.py;
 * tools/test_front.py compares the two over the dictionary and a sentence set.
 */
#include "ics_front.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- phones */

typedef struct {
	const char *name; /* ICS phone */
	int stress;       /* 0..2, or -1 for consonants */
} Phone;

typedef struct {
	char base[3]; /* ARPAbet without stress, e.g. "AH" */
	int stress;   /* 0..2, or -1 */
} Arpa;

static const char *const ARPA_PAIRS[][2] = {
	{"AA", "aa"}, {"AE", "a"}, {"AH", "uh"}, {"AO", "oo"}, {"AW", "au"}, {"AY", "ai"},
	{"EH", "e"}, {"ER", "er"}, {"EY", "ei"}, {"IH", "i"}, {"IY", "ii"}, {"OW", "ou"},
	{"OY", "oi"}, {"UH", "u"}, {"UW", "uu"},
	{"B", "b"}, {"CH", "ch"}, {"D", "d"}, {"DH", "dh"}, {"F", "f"}, {"G", "g"}, {"HH", "h"},
	{"JH", "jh"}, {"K", "k"}, {"L", "l"}, {"M", "m"}, {"N", "n"}, {"NG", "ng"}, {"P", "p"},
	{"R", "r"}, {"S", "s"}, {"SH", "sh"}, {"T", "t"}, {"TH", "th"}, {"V", "v"}, {"W", "w"},
	{"Y", "y"}, {"Z", "z"}, {"ZH", "zh"},
};

static const char *arpa_ics(const char *base)
{
	size_t i;
	for (i = 0; i < sizeof ARPA_PAIRS / sizeof ARPA_PAIRS[0]; i++)
		if (strcmp(ARPA_PAIRS[i][0], base) == 0)
			return ARPA_PAIRS[i][1];
	return NULL;
}

static int in_list(const char *s, const char *const *list)
{
	for (; *list; list++)
		if (strcmp(s, *list) == 0)
			return 1;
	return 0;
}

static const char *const VOWELS[] = {"a", "aa", "au", "ax", "e", "i", "ii", "oo", "ou", "u", "uu", "uh",
	"ai", "ei", "oi", "er", "o", "eax", "iax", "uax", "aax", NULL};
static const char *const ARPA_VOWELS[] = {"AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH",
	"IY", "OW", "OY", "UH", "UW", NULL};
static const char *const SONORANT_CONS[] = {"n", "m", "ng", "l", "r", "y", "w", NULL};

static int is_vowel(const char *p) { return in_list(p, VOWELS); }
static int is_arpa_vowel(const char *b) { return b && in_list(b, ARPA_VOWELS); }
static int is_sonorant_cons(const char *p) { return in_list(p, SONORANT_CONS); }

/* ------------------------------------------------- accent mappings (RP/GA) */

static const char *const RP_BEFORE_R[][2] = {
	{"AA", "aa"}, {"AO", "oo"}, {"OW", "oo"}, {"EH", "eax"}, {"AE", "eax"}, {"EY", "eax"},
	{"IH", "iax"}, {"IY", "iax"}, {"UH", "uax"}, {"UW", "uax"}, {"AH", "aax"},
};
static const char *const RP_PLAIN[][2] = {
	{"AE", "a"}, {"AW", "au"}, {"AY", "ai"}, {"EH", "e"}, {"EY", "ei"}, {"IH", "i"}, {"IY", "ii"},
	{"OW", "ou"}, {"OY", "oi"}, {"UH", "u"}, {"UW", "uu"},
};
static const char *const RP_BATH[] = {"after", "afternoon", "answer", "ask", "asked", "asking", "basket",
	"bath", "brass", "can't", "cast", "castle", "class", "craft", "dance", "draft", "example", "fast",
	"father", "grass", "half", "last", "laugh", "laughed", "laughter", "mask", "master", "nasty", "pass",
	"passed", "past", "path", "plant", "rather", "staff", "task", "vast", "branch", "chance", "demand",
	"command", "glass", NULL};
static const char *const RP_STRUT_LOT[] = {"of", "from", "because", "what", "what's", "whatever",
	"wasn't", "cos", NULL};

static const char *pair_lookup(const char *const (*pairs)[2], size_t n, const char *base)
{
	size_t i;
	for (i = 0; i < n; i++)
		if (strcmp(pairs[i][0], base) == 0)
			return pairs[i][1];
	return NULL;
}

/* re.search(r'au|aw|al[lkt]|ough' + extra, word) */
static int thought_spelling(const char *word, int with_wa)
{
	const char *p;
	if (strstr(word, "au") || strstr(word, "aw") || strstr(word, "ough"))
		return 1;
	if (with_wa && strstr(word, "wa"))
		return 1;
	for (p = word; (p = strstr(p, "al")) != NULL; p++)
		if (p[2] == 'l' || p[2] == 'k' || p[2] == 't')
			return 1;
	return 0;
}

static const char *rp_lot(const char *word, const char *base)
{
	int has_o = strchr(word, 'o') != NULL, has_a = strchr(word, 'a') != NULL;
	if (strcmp(base, "AA") == 0) {
		if (has_o && !has_a)
			return "o";
		if (has_a && !strstr(word, "wa") && !strstr(word, "qua"))
			return "aa";
		return "o";
	}
	if (thought_spelling(word, 1) || (has_a && !has_o))
		return "oo";
	return "o";
}

static int to_rp(const char *word, const Arpa *a, int n, Phone *out)
{
	int i = 0, k = 0;
	while (i < n) {
		const char *base = a[i].base;
		int stress = a[i].stress;
		const char *nxt = i + 1 < n ? a[i + 1].base : NULL;
		const char *after = i + 2 < n ? a[i + 2].base : NULL;
		int full = stress == 0 ? 2 : stress;
		const char *v;
		if (!is_arpa_vowel(base)) {
			const char *c = arpa_ics(base);
			if (c) {
				out[k].name = c;
				out[k++].stress = -1;
			}
			i++;
			continue;
		}
		if (strcmp(base, "ER") == 0) {
			out[k].name = stress == 0 ? "ax" : "aax";
			out[k++].stress = stress;
			if (is_arpa_vowel(nxt)) {
				out[k].name = "r";
				out[k++].stress = -1;
			}
			i++;
			continue;
		}
		if (nxt && strcmp(nxt, "R") == 0 && !is_arpa_vowel(after)
				&& (v = pair_lookup(RP_BEFORE_R, sizeof RP_BEFORE_R / sizeof RP_BEFORE_R[0], base)) != NULL) {
			if (strcmp(base, "AH") == 0 && stress == 0) {
				out[k].name = "ax";
				out[k++].stress = 0;
			} else {
				out[k].name = v;
				out[k++].stress = full;
			}
			i += 2;
			continue;
		}
		if (strcmp(base, "AH") == 0) {
			if (stress == 0) {
				out[k].name = "ax";
				out[k++].stress = 0;
			} else {
				out[k].name = in_list(word, RP_STRUT_LOT) ? "o" : "uh";
				out[k++].stress = stress;
			}
		} else if (strcmp(base, "AA") == 0 || strcmp(base, "AO") == 0) {
			out[k].name = rp_lot(word, base);
			out[k++].stress = full;
		} else if (strcmp(base, "AE") == 0 && in_list(word, RP_BATH) && stress) {
			out[k].name = "aa";
			out[k++].stress = full;
		} else {
			out[k].name = pair_lookup(RP_PLAIN, sizeof RP_PLAIN / sizeof RP_PLAIN[0], base);
			out[k++].stress = full;
		}
		i++;
	}
	return k;
}

static int to_ga(const char *word, const Arpa *a, int n, Phone *out)
{
	int i, k = 0;
	for (i = 0; i < n; i++) {
		const char *base = a[i].base;
		int st = a[i].stress;
		const char *c = arpa_ics(base);
		if (!c)
			continue;
		if (st < 0) {
			out[k].name = c;
			out[k++].stress = -1;
		} else if (strcmp(base, "AH") == 0) {
			out[k].name = st == 0 ? "ax" : "uh";
			out[k++].stress = st;
		} else if (strcmp(base, "ER") == 0) {
			out[k].name = "er";
			out[k++].stress = st;
		} else if (strcmp(base, "AO") == 0) {
			int before_r = i + 1 < n && strcmp(a[i + 1].base, "R") == 0 && a[i + 1].stress < 0;
			out[k].name = (before_r || thought_spelling(word, 0) || !strchr(word, 'o')) ? "oo" : "aa";
			out[k++].stress = st == 0 ? 2 : st;
		} else {
			out[k].name = c;
			out[k++].stress = st == 0 ? 2 : st;
		}
	}
	return k;
}

/* --------------------------------------------------------------- lexicon */

typedef struct {
	const char *word;
	const char *arpa; /* space-separated, CMUdict style */
} Entry;

struct IcsLexicon {
	int accent;
	char *text;      /* the dictionary file, cut up in place */
	Entry *slots;    /* open addressing */
	size_t mask;
};

static const char *const EXTRA_WORDS[][2] = {
	{"paren", "P AH0 R EH1 N"}, {"parens", "P AH0 R EH1 N Z"}, {"caret", "K EH1 R AH0 T"},
	{"cedilla", "S AH0 D IH1 L AH0"}, {"diaeresis", "D AY0 EH1 R AH0 S IH0 S"}, {"macron", "M EY1 K R AA0 N"},
	{"semicolon", "S EH1 M IY0 K OW2 L AH0 N"}, {"subscript", "S AH1 B S K R IH2 P T"},
	{"superscript", "S UW1 P ER0 S K R IH2 P T"}, {"superset", "S UW1 P ER0 S EH2 T"},
	{"fullwidth", "F UH1 L W IH2 D TH"}, {"servicemark", "S ER1 V AH0 S M AA2 R K"},
	{"anticlockwise", "AE2 N T IY0 K L AA1 K W AY2 Z"}, {"ideographic", "IH2 D IY0 AH0 G R AE1 F IH0 K"},
	{"coproduct", "K OW0 P R AA1 D AH0 K T"}, {"multiset", "M AH1 L T IY0 S EH2 T"},
	{"nonnegative", "N AA0 N N EH1 G AH0 T IH0 V"}, {"quaternions", "K W AH0 T ER1 N IY0 AH0 N Z"},
	{"asymptotically", "EY2 S IH0 M P T AA1 T IH0 K L IY0"}, {"xray", "EH1 K S R EY2"},
};

static size_t hash_str(const char *s)
{
	size_t h = 2166136261u;
	for (; *s; s++)
		h = (h ^ (unsigned char) *s) * 16777619u;
	return h;
}

static const char *lex_find(const IcsLexicon *lex, const char *word)
{
	size_t i = hash_str(word) & lex->mask;
	while (lex->slots[i].word) {
		if (strcmp(lex->slots[i].word, word) == 0)
			return lex->slots[i].arpa;
		i = (i + 1) & lex->mask;
	}
	return NULL;
}

/* Keeps the first entry for a word, like the reference's dict. */
static void lex_add(IcsLexicon *lex, const char *word, const char *arpa)
{
	size_t i = hash_str(word) & lex->mask;
	while (lex->slots[i].word) {
		if (strcmp(lex->slots[i].word, word) == 0)
			return;
		i = (i + 1) & lex->mask;
	}
	lex->slots[i].word = word;
	lex->slots[i].arpa = arpa;
}

IcsLexicon *icsf_lexicon_load(const char *path, int accent)
{
	FILE *f = fopen(path, "rb");
	IcsLexicon *lex;
	long size;
	size_t lines = 0, cap, i;
	char *p, *end;
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	lex = (IcsLexicon *) calloc(1, sizeof *lex);
	if (!lex || size < 0 || !(lex->text = (char *) malloc((size_t) size + 1))) {
		fclose(f);
		free(lex);
		return NULL;
	}
	if (fread(lex->text, 1, (size_t) size, f) != (size_t) size) {
		fclose(f);
		icsf_lexicon_free(lex);
		return NULL;
	}
	fclose(f);
	lex->text[size] = '\0';
	lex->accent = accent;
	for (p = lex->text; *p; p++)
		lines += *p == '\n';
	for (cap = 1024; cap < 2 * (lines + 64); cap <<= 1)
		;
	lex->slots = (Entry *) calloc(cap, sizeof(Entry));
	if (!lex->slots) {
		icsf_lexicon_free(lex);
		return NULL;
	}
	lex->mask = cap - 1;
	for (p = lex->text; *p; p = end) {
		char *word, *w, *arpa, *hash, *q;
		end = strchr(p, '\n');
		end = end ? end + 1 : p + strlen(p);
		if (end > p && end[-1] == '\n')
			end[-1] = '\0';
		hash = strchr(p, '#');
		if (hash)
			*hash = '\0';
		while (*p == ' ' || *p == '\t' || *p == '\r')
			p++;
		if (!*p || strncmp(p, ";;;", 3) == 0)
			continue;
		word = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p)
			*p++ = '\0';
		for (w = word; *w; w++)
			*w = (char) tolower((unsigned char) *w);
		q = strrchr(word, '(');
		if (q && q[1] && w[-1] == ')') {
			char *d = q + 1;
			while (isdigit((unsigned char) *d))
				d++;
			if (d == w - 1 && d > q + 1)
				*q = '\0';
		}
		while (*p == ' ' || *p == '\t')
			p++;
		arpa = p;
		for (q = arpa + strlen(arpa); q > arpa && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r'); q--)
			;
		*q = '\0';
		if (*word && *arpa)
			lex_add(lex, word, arpa);
	}
	for (i = 0; i < sizeof EXTRA_WORDS / sizeof EXTRA_WORDS[0]; i++)
		lex_add(lex, EXTRA_WORDS[i][0], EXTRA_WORDS[i][1]);
	return lex;
}

void icsf_lexicon_free(IcsLexicon *lex)
{
	if (lex) {
		free(lex->text);
		free(lex->slots);
		free(lex);
	}
}

/* "AH0 L OW1" -> Arpa[]; returns count. */
static int parse_arpa(const char *s, Arpa *out, int max)
{
	int n = 0;
	while (*s && n < max) {
		int len = 0;
		while (*s == ' ' || *s == '\t')
			s++;
		if (!*s)
			break;
		while (s[len] && s[len] != ' ' && s[len] != '\t')
			len++;
		if (len >= 1 && len <= 3) {
			int blen = isdigit((unsigned char) s[len - 1]) ? len - 1 : len;
			if (blen >= 1 && blen <= 2) {
				memcpy(out[n].base, s, (size_t) blen);
				out[n].base[blen] = '\0';
				out[n].stress = blen < len ? s[len - 1] - '0' : -1;
				n++;
			}
		}
		s += len;
	}
	return n;
}

#define MAX_PHONES 128

static int convert(const IcsLexicon *lex, const char *word, const Arpa *a, int n, Phone *out)
{
	return lex->accent == ICSF_ACCENT_GB ? to_rp(word, a, n, out) : to_ga(word, a, n, out);
}

static int lex_pron(const IcsLexicon *lex, const char *word, Phone *out)
{
	Arpa a[MAX_PHONES];
	const char *s = lex_find(lex, word);
	if (!s)
		return -1;
	/* RP can add an 'r' after each vowel, so leave room for doubling. */
	return convert(lex, word, a, parse_arpa(s, a, MAX_PHONES / 2), out);
}

/* ------------------------------------------------------------- NRL rules */

typedef struct {
	const char *left, *match, *right, *out;
} NrlRule;

#include "nrl_rules.inc"

static int nrl_vowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }
static int nrl_consonant(char c) { return c >= 'A' && c <= 'Z' && !nrl_vowel(c); }
static int nrl_voiced(char c) { return c && strchr("BDVGJLMNRWZ", c) != NULL; }
static int nrl_front(char c) { return c == 'E' || c == 'I' || c == 'Y'; }

static int nrl_left(const char *pat, const char *word, int i)
{
	int k;
	for (k = (int) strlen(pat) - 1; k >= 0; k--) {
		char p = pat[k], c = i >= 0 ? word[i] : ' ';
		if (isalpha((unsigned char) p) || p == '\'' || p == ' ') {
			if (p != c)
				return 0;
			i--;
		} else if (p == '#') {
			if (!nrl_vowel(c))
				return 0;
			i--;
			while (i >= 0 && nrl_vowel(word[i]))
				i--;
		} else if (p == ':') {
			while (i >= 0 && nrl_consonant(word[i]))
				i--;
		} else if (p == '^') {
			if (!nrl_consonant(c))
				return 0;
			i--;
		} else if (p == '.') {
			if (!nrl_voiced(c))
				return 0;
			i--;
		} else if (p == '+') {
			if (!nrl_front(c))
				return 0;
			i--;
		} else {
			return 0;
		}
	}
	return 1;
}

static int nrl_right(const char *pat, const char *word, int i)
{
	int n = (int) strlen(word);
#define AT(k) ((k) < n ? word[k] : ' ')
	for (; *pat; pat++) {
		char p = *pat, c = AT(i);
		if (isalpha((unsigned char) p) || p == '\'' || p == ' ') {
			if (p != c)
				return 0;
			i++;
		} else if (p == '#') {
			if (!nrl_vowel(c))
				return 0;
			i++;
			while (nrl_vowel(AT(i)))
				i++;
		} else if (p == ':') {
			while (nrl_consonant(AT(i)))
				i++;
		} else if (p == '^') {
			if (!nrl_consonant(c))
				return 0;
			i++;
		} else if (p == '.') {
			if (!nrl_voiced(c))
				return 0;
			i++;
		} else if (p == '+') {
			if (!nrl_front(c))
				return 0;
			i++;
		} else if (p == '%') {
			if (c == 'E') {
				i++;
				if (AT(i) == 'L' && AT(i + 1) == 'Y')
					i += 2;
				else if (AT(i) == 'R' || AT(i) == 'S' || AT(i) == 'D')
					i++;
			} else if (c == 'I' && AT(i + 1) == 'N' && AT(i + 2) == 'G') {
				i += 3;
			} else {
				return 0;
			}
		} else {
			return 0;
		}
	}
#undef AT
	return 1;
}

/* ARPAbet (CMUdict style, stress digits) for an unknown word; count or 0. */
static int nrl_guess(const char *word, Arpa *out, int max)
{
	char padded[256], raw[1024];
	size_t wl = strlen(word), rl = 0;
	int i, n = 0, stressed = 0;
	if (wl + 3 > sizeof padded)
		return 0;
	padded[0] = ' ';
	for (i = 0; i < (int) wl; i++)
		padded[i + 1] = (char) toupper((unsigned char) word[i]);
	padded[wl + 1] = ' ';
	padded[wl + 2] = '\0';
	raw[0] = '\0';
	for (i = 1; i < (int) wl + 1;) {
		char c = padded[i];
		const NrlRule *r = nrl_tables[c >= 'A' && c <= 'Z' ? c - 'A' + 1 : 0];
		int hit = 0;
		for (; r->match; r++) {
			size_t ml = strlen(r->match);
			if (strncmp(padded + i, r->match, ml) != 0)
				continue;
			if (nrl_left(r->left, padded, i - 1) && nrl_right(r->right, padded, i + (int) ml)) {
				size_t ol = strlen(r->out);
				if (rl + ol < sizeof raw) {
					memcpy(raw + rl, r->out, ol + 1);
					rl += ol;
				}
				i += (int) ml;
				hit = 1;
				break;
			}
		}
		if (!hit)
			i++;
	}
	for (i = 0; raw[i] && n < max;) {
		char c = raw[i];
		char tok[3];
		if (c == ' ') {
			i++;
			continue;
		}
		if (isupper((unsigned char) c)) {
			tok[0] = c;
			tok[1] = raw[i + 1];
			tok[2] = '\0';
			i += 2;
		} else {
			if (c == 'j' || c == 'h') {
				tok[0] = (char) toupper((unsigned char) c);
				tok[1] = 'H';
				tok[2] = '\0';
			} else {
				tok[0] = (char) toupper((unsigned char) c);
				tok[1] = '\0';
			}
			i++;
		}
		if (strcmp(tok, "WH") == 0) {
			strcpy(out[n].base, "W");
			out[n++].stress = -1;
		} else if (strcmp(tok, "AX") == 0) {
			strcpy(out[n].base, "AH");
			out[n++].stress = 0;
		} else if (is_arpa_vowel(tok)) {
			strcpy(out[n].base, tok);
			out[n++].stress = stressed ? 0 : 1;
			stressed = 1;
		} else {
			strcpy(out[n].base, tok);
			out[n++].stress = -1;
		}
	}
	return n;
}

static int lex_guess(const IcsLexicon *lex, const char *word, Phone *out)
{
	Arpa a[MAX_PHONES];
	int n = nrl_guess(word, a, MAX_PHONES / 2);
	return n ? convert(lex, word, a, n, out) : -1;
}

/* ------------------------------------------------------------ text, words */

static const char *const ONES[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
	"nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
	"eighteen", "nineteen"};
static const char *const TENS[] = {"twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty",
	"ninety"};

static const char *const WORD_TYPES[] = {"am", "an", "and", "are", "as", "at", "because", "been", "but",
	"by", "could", "could've", "coulda", "couldn't", "de", "do", "does", "doesn't", "for", "from", "had",
	"has", "have", "he", "he'd", "he'll", "he's", "her", "him", "his", "huh", "i'd", "i'll", "i'm", "in",
	"la", "le", "of", "or", "per", "se", "shall", "she'd", "she'll", "she's", "sir", "so", "some", "than",
	"that", "that's", "the", "their", "them", "there", "there'll", "there's", "there've", "they'd",
	"they'll", "they're", "this", "this'll", "to", "twas", "uh", "us", "was", "we", "we'd", "we'll",
	"we're", "we've", "were", "what", "what's", "where", "where'd", "where're", "where's", "will",
	"would", "would've", "wouldn't", "you", "you'd", "you'll", "you're", "you've", "your", NULL};
static const char *const WORD_SPECIAL[][2] = {
	{"a", "a_determiner"}, {"i", "i_pronoun"}, {"can", "can_vrb"}, {"cause", "cause_cnj"}, {"cos", "cos_cnj"},
};

#define MAX_WORD 64

typedef struct {
	char text[MAX_WORD]; /* original case, or a punctuation character */
	int punct;
} Token;

typedef struct {
	char wt[MAX_WORD + 8];
	Phone ph[MAX_PHONES];
	int n;
	char brk; /* 0, 'p' or 's' */
} Word;

typedef struct {
	void *items;
	size_t n, cap, size;
} Vec;

static void *vec_push(Vec *v)
{
	if (v->n == v->cap) {
		size_t cap = v->cap ? v->cap * 2 : 32;
		void *p = realloc(v->items, cap * v->size);
		if (!p)
			return NULL;
		v->items = p;
		v->cap = cap;
	}
	memset((char *) v->items + v->n * v->size, 0, v->size);
	return (char *) v->items + v->size * v->n++;
}

static int push_token(Vec *toks, const char *text, size_t len, int punct)
{
	Token *t = (Token *) vec_push(toks);
	if (!t)
		return 0;
	if (len >= MAX_WORD)
		len = MAX_WORD - 1;
	memcpy(t->text, text, len);
	t->text[len] = '\0';
	t->punct = punct;
	return 1;
}

static int number_words(Vec *toks, long long n)
{
	static const long long divs[] = {1000000000LL, 1000000LL, 1000LL};
	static const char *const names[] = {"billion", "million", "thousand"};
	int i;
	if (n < 20)
		return push_token(toks, ONES[n], strlen(ONES[n]), 0);
	if (n < 100) {
		if (!push_token(toks, TENS[n / 10 - 2], strlen(TENS[n / 10 - 2]), 0))
			return 0;
		return n % 10 ? push_token(toks, ONES[n % 10], strlen(ONES[n % 10]), 0) : 1;
	}
	if (n < 1000) {
		if (!push_token(toks, ONES[n / 100], strlen(ONES[n / 100]), 0) || !push_token(toks, "hundred", 7, 0))
			return 0;
		return n % 100 ? number_words(toks, n % 100) : 1;
	}
	for (i = 0; i < 3; i++) {
		if (n >= divs[i]) {
			if (!number_words(toks, n / divs[i]) || !push_token(toks, names[i], strlen(names[i]), 0))
				return 0;
			return n % divs[i] ? number_words(toks, n % divs[i]) : 1;
		}
	}
	return 1;
}

static int is_alpha(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

/* \d+ | [A-Za-z]+('[A-Za-z]+)* | [.,!?;:], with U+2019 read as an apostrophe. */
static int tokenize(const char *text, Vec *toks)
{
	const unsigned char *s = (const unsigned char *) text;
	char buf[MAX_WORD * 4];
	while (*s) {
		if (isdigit(*s)) {
			const unsigned char *b = s;
			size_t len;
			while (isdigit(*s))
				s++;
			len = (size_t) (s - b);
			if (len <= 12) {
				long long v = 0;
				size_t k;
				for (k = 0; k < len; k++)
					v = v * 10 + (b[k] - '0');
				if (!number_words(toks, v))
					return 0;
			} else {
				size_t k;
				for (k = 0; k < len; k++)
					if (!push_token(toks, ONES[b[k] - '0'], strlen(ONES[b[k] - '0']), 0))
						return 0;
			}
		} else if (is_alpha(*s)) {
			size_t len = 0;
			for (;;) {
				while (is_alpha(*s)) {
					if (len < sizeof buf - 1)
						buf[len++] = (char) *s;
					s++;
				}
				if (s[0] == '\'' && is_alpha(s[1])) {
					s++;
				} else if (s[0] == 0xE2 && s[1] == 0x80 && s[2] == 0x99 && is_alpha(s[3])) {
					s += 3;
				} else {
					break;
				}
				if (len < sizeof buf - 1)
					buf[len++] = '\'';
			}
			if (!push_token(toks, buf, len, 0))
				return 0;
		} else if (strchr(".,!?;:", *s)) {
			if (!push_token(toks, (const char *) s, 1, 1))
				return 0;
			s++;
		} else {
			s++;
		}
	}
	return 1;
}

static int is_upper_word(const char *t)
{
	int cased = 0;
	for (; *t; t++) {
		if (*t >= 'a' && *t <= 'z')
			return 0;
		if (*t >= 'A' && *t <= 'Z')
			cased = 1;
	}
	return cased;
}

static int add_letters(const IcsLexicon *lex, const char *word, Vec *words)
{
	for (; *word; word++) {
		char ch[3] = {(char) tolower((unsigned char) *word), 0, 0};
		Phone ph[MAX_PHONES];
		int n = lex_pron(lex, ch, ph);
		if (n <= 0) {
			ch[1] = '.';
			n = lex_pron(lex, ch, ph);
			ch[1] = '\0';
		}
		if (n > 0) {
			Word *w = (Word *) vec_push(words);
			if (!w)
				return 0;
			snprintf(w->wt, sizeof w->wt, "%s_letter", ch);
			memcpy(w->ph, ph, sizeof(Phone) * (size_t) n);
			w->n = n;
		}
	}
	return 1;
}

static int words_from_text(const IcsLexicon *lex, const char *text, char final, Vec *words)
{
	Vec toks = {NULL, 0, 0, sizeof(Token)};
	size_t i;
	int ok = tokenize(text, &toks);
	for (i = 0; ok && i < toks.n; i++) {
		const Token *tok = (const Token *) toks.items + i;
		char t[MAX_WORD];
		const char *wt = "0";
		Phone ph[MAX_PHONES];
		int n, k, vowel = 0;
		if (tok->punct) {
			if (words->n) {
				Word *last = (Word *) words->items + words->n - 1;
				last->brk = strchr(".!?", tok->text[0]) ? 's' : 'p';
			}
			continue;
		}
		for (k = 0; tok->text[k]; k++) {
			t[k] = (char) tolower((unsigned char) tok->text[k]);
			vowel |= strchr("aeiouy", t[k]) != NULL;
		}
		t[k] = '\0';
		if (k == 1 && strcmp(t, "a") && strcmp(t, "i")) {
			ok = add_letters(lex, t, words);
			continue;
		}
		n = lex_pron(lex, t, ph);
		if (n < 0) {
			if ((is_upper_word(tok->text) && k <= 5) || !vowel) {
				ok = add_letters(lex, t, words);
				continue;
			}
			n = lex_guess(lex, t, ph);
			if (n <= 0) {
				ok = add_letters(lex, t, words);
				continue;
			}
		} else {
			const char *special = pair_lookup(WORD_SPECIAL, sizeof WORD_SPECIAL / sizeof WORD_SPECIAL[0], t);
			wt = special ? special : in_list(t, WORD_TYPES) ? t : "0";
		}
		{
			Word *w = (Word *) vec_push(words);
			if (!w) {
				ok = 0;
				break;
			}
			snprintf(w->wt, sizeof w->wt, "%s", wt);
			memcpy(w->ph, ph, sizeof(Phone) * (size_t) n);
			w->n = n;
		}
	}
	free(toks.items);
	if (ok && words->n) {
		Word *last = (Word *) words->items + words->n - 1;
		if (!last->brk)
			last->brk = final;
	}
	return ok;
}

/* ------------------------------------------------------ syllables, labels */

static const char *const ONE_INITIAL[] = {"b", "ch", "d", "dh", "f", "g", "h", "jh", "k", "l", "m", "n",
	"p", "r", "s", "sh", "t", "th", "v", "w", "y", "z", "zh", NULL};

static int in_words(const char *p, const char *list)
{
	size_t n = strlen(p);
	const char *s = list;
	while (*s) {
		const char *e = strchr(s, ' ');
		size_t len = e ? (size_t) (e - s) : strlen(s);
		if (len == n && strncmp(s, p, n) == 0)
			return 1;
		s += len;
		while (*s == ' ')
			s++;
	}
	return 0;
}

static int is_onset(const Phone *c, int n)
{
	static const char *const pairs[][2] = {
		{"p b k f h v m n g", "y"}, {"p b k g f v", "l r"}, {"s z l", "y"}, {"s sh", "v"}, {"g", "w r"},
		{"t d th sh k", "r w y"}, {"s", "p t k n m l r w f"}, {"p", "w"},
	};
	static const char *const triples[][3] = {
		{"s sh", "p", "y"}, {"s sh", "t", "r"}, {"s sh", "t", "y"}, {"s sh", "k", "r w y"},
		{"s sh", "k", "l r"}, {"s sh", "p", "l r y"},
	};
	size_t i;
	if (n == 1)
		return in_list(c[0].name, ONE_INITIAL);
	if (n == 2) {
		for (i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
			if (in_words(c[0].name, pairs[i][0]) && in_words(c[1].name, pairs[i][1]))
				return 1;
		return 0;
	}
	if (n == 3) {
		for (i = 0; i < sizeof triples / sizeof triples[0]; i++)
			if (in_words(c[0].name, triples[i][0]) && in_words(c[1].name, triples[i][1])
					&& in_words(c[2].name, triples[i][2]))
				return 1;
		return 0;
	}
	return 0;
}

typedef struct {
	const char *name;
	char B[8], C[4], D[8], E[10], F[MAX_WORD + 8], pS[8], nS[8];
	int syl;
} Seg;

static Seg *push_seg(Vec *segs, const char *name, const char *B, int syl)
{
	Seg *s = (Seg *) vec_push(segs);
	if (s) {
		s->name = name;
		snprintf(s->B, sizeof s->B, "%s", B);
		s->syl = syl;
	}
	return s;
}

static Seg *push_sil(Vec *segs, const char *B)
{
	Seg *s = push_seg(segs, "sil", B, -1);
	if (s) {
		strcpy(s->D, "sil");
		strcpy(s->E, "sil");
		strcpy(s->F, "sil");
	}
	return s;
}

typedef struct {
	char s[8];
} Str8;

static int push_stress(Vec *ss, const char *v)
{
	Str8 *x = (Str8 *) vec_push(ss);
	if (!x)
		return 0;
	snprintf(x->s, sizeof x->s, "%s", v);
	return 1;
}

static int build_segments(const Vec *words, Vec *segs, Vec *sylstress)
{
	size_t wi;
	if (!push_sil(segs, "|sil|") || !push_stress(sylstress, "sil"))
		return 0;
	for (wi = 0; wi < words->n; wi++) {
		const Word *w = (const Word *) words->items + wi;
		const char *phrasing = w->brk == 's' ? "s_final" : w->brk == 'p' ? "p_final" : "mid";
		size_t first = segs->n, k;
		int vidx[MAX_PHONES], nv = 0, i, start = 0, v;
		for (i = 0; i < w->n; i++)
			if (is_vowel(w->ph[i].name))
				vidx[nv++] = i;
		if (!nv && w->n) {
			if (!push_stress(sylstress, "0"))
				return 0;
			for (i = 0; i < w->n; i++) {
				char b[8];
				snprintf(b, sizeof b, "i%d%d", i + 1 < 3 ? i + 1 : 3, w->n < 3 ? w->n : 3);
				if (!push_seg(segs, w->ph[i].name, b, (int) sylstress->n - 1))
					return 0;
			}
		}
		for (v = 0; v < nv; v++) {
			int vi = vidx[v], end, sidx;
			char st[8];
			if (v + 1 < nv) {
				int cl = vidx[v + 1] - (vi + 1), split = cl, kk;
				for (kk = 0; kk <= cl; kk++)
					if (kk == cl || is_onset(w->ph + vi + 1 + kk, cl - kk)) {
						split = kk;
						break;
					}
				end = vi + 1 + split;
			} else {
				end = w->n;
			}
			if (w->ph[vi].stress < 0)
				strcpy(st, "None");
			else
				snprintf(st, sizeof st, "%d", w->ph[vi].stress);
			if (!push_stress(sylstress, st))
				return 0;
			sidx = (int) sylstress->n - 1;
			for (i = start; i < vi; i++) {
				char b[8];
				int on = vi - start;
				snprintf(b, sizeof b, "i%d%d", i - start + 1 < 3 ? i - start + 1 : 3, on < 3 ? on : 3);
				if (!push_seg(segs, w->ph[i].name, b, sidx))
					return 0;
			}
			if (!push_seg(segs, w->ph[vi].name, "v", sidx))
				return 0;
			for (i = vi + 1; i < end; i++) {
				char b[8];
				int co = end - vi - 1;
				snprintf(b, sizeof b, "f%d%d", i - vi < 3 ? i - vi : 3, co < 3 ? co : 3);
				if (!push_seg(segs, w->ph[i].name, b, sidx))
					return 0;
			}
			start = end;
		}
		if (segs->n == first)
			continue;
		{
			Seg *f = (Seg *) segs->items + first, *l = (Seg *) segs->items + segs->n - 1;
			char tmp[8];
			if (f->B[0] == 'i') {
				snprintf(tmp, sizeof tmp, "|i1%c", f->B[2]);
				strcpy(f->B, tmp);
			} else if (strcmp(f->B, "v") == 0) {
				strcpy(f->B, "|v");
			}
			if (l->B[0] == 'f') {
				snprintf(tmp, sizeof tmp, "f%c%c|", l->B[2], l->B[2]);
				strcpy(l->B, tmp);
			} else if (strcmp(l->B, "v") == 0 || strcmp(l->B, "|v") == 0) {
				strcat(l->B, "|");
			}
		}
		for (k = first; k < segs->n; k++) {
			Seg *s = (Seg *) segs->items + k;
			snprintf(s->D, sizeof s->D, "%s", ((Str8 *) sylstress->items)[s->syl].s);
			snprintf(s->E, sizeof s->E, "%s", phrasing);
			snprintf(s->F, sizeof s->F, "%s", w->wt);
		}
		/* A pause right before the final silence is a sil-sil context the
		   voice never saw; its gain explodes into a full-scale thump. */
		if (w->brk == 'p' && wi + 1 < words->n && !push_sil(segs, "sil"))
			return 0;
	}
	if (!push_sil(segs, "|sil|") || !push_stress(sylstress, "sil"))
		return 0;
	{
		size_t i;
		Seg *a = (Seg *) segs->items;
		Str8 *ss = (Str8 *) sylstress->items;
		for (i = 0; i < segs->n; i++) {
			Seg *s = a + i;
			const char *nxt = i + 1 < segs->n ? a[i + 1].name : "sil";
			/* Obstruents always carry 0: the voice never saw p/t/k with a
			   sonorant here, and its trees then pick loud, vowel-like bursts. */
			if (!is_vowel(s->name) && !is_sonorant_cons(s->name))
				strcpy(s->C, "0");
			else if (is_sonorant_cons(nxt))
				snprintf(s->C, sizeof s->C, "%s", nxt);
			else if (is_sonorant_cons(s->name) && is_vowel(nxt))
				strcpy(s->C, "v");
			else
				strcpy(s->C, "0");
			if (s->syl >= 0) {
				snprintf(s->pS, sizeof s->pS, "%s", ss[s->syl - 1].s);
				snprintf(s->nS, sizeof s->nS, "%s", ss[s->syl + 1].s);
			} else {
				strcpy(s->pS, "sil");
				strcpy(s->nS, "sil");
			}
		}
	}
	return 1;
}

int icsf_text_to_labels(const IcsLexicon *lex, const char *text, int final_sentence, char **out)
{
	Vec words = {NULL, 0, 0, sizeof(Word)};
	Vec segs = {NULL, 0, 0, sizeof(Seg)};
	Vec ss = {NULL, 0, 0, sizeof(Str8)};
	char *buf = NULL;
	int count = -1;
	*out = NULL;
	if (words_from_text(lex, text, final_sentence ? 's' : 0, &words) && build_segments(&words, &segs, &ss)) {
		size_t i, used = 0, cap = segs.n * 256 + 1;
		const Seg *a = (const Seg *) segs.items;
		buf = (char *) malloc(cap);
		if (buf) {
			for (i = 0; i < segs.n; i++) {
				const Seg *p = i > 0 ? a + i - 1 : a, *s = a + i, *n = i + 1 < segs.n ? a + i + 1 : a + segs.n - 1;
				int len = snprintf(buf + used, cap - used,
					"%s^%s-%s+%s=/B:%s:/C:%s:/D:%s:/E:%s:/F:%s:/p1D:%s:/p1E:%s:/p1F:%s:"
					"/n1C:%s:/n1D:%s:/n1E:%s:/n1F:%s:/p1sD:%s:/n1sD:%s:",
					i ? "\n" : "", p->name, s->name, n->name, s->B, s->C, s->D, s->E, s->F,
					p->D, p->E, p->F, n->C, n->D, n->E, n->F, s->pS, s->nS);
				if (len < 0 || (size_t) len >= cap - used)
					break;
				used += (size_t) len;
			}
			if (i == segs.n) {
				count = (int) segs.n;
				*out = buf;
				buf = NULL;
			}
		}
	}
	free(buf);
	free(words.items);
	free(segs.items);
	free(ss.items);
	return count;
}
