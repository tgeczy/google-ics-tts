/* ics_convert.c -- Flint PHONETIC-ARTS3 voice (Android 4.0 GoogleTTS.apk
 * assets/<lang>/voice) to an hts_engine API 1.10 .htsvoice.
 *
 * Port of tools/flint2htsvoice.py (with flint_parser.py and flint_trees.py);
 * tests/test_convert.py checks the two give byte-identical files.
 *
 * Voice layout, all little-endian:
 *   text header with <SAMPLE-RATE>, <FRAME-PERIOD>, <FREQ-WARPING>, <STAGE> ...
 *   </DSP>\n then the tree block: u32 size, then
 *     u16 nq, u8[nq] name lengths, names;
 *     u16 K, u8[K] group sizes, u16[] question ids   (compact questions)
 *     u8 streams; per stream u8 id, u16 trees; per tree a Jacobson level-order
 *     bitmap (u32 words, u32 bits, u32[words]; u32 c1, u32[c1]; u32 c2,
 *     u8[c2], pad to 4) and u16 N, u16[N] node values
 *   <MODELS>\n u32 states; per stream (duration, mcp, me, lf0): u16 dim,
 *     u16[states] pdf counts, u16[total*dim] (mean index << 8 | var index),
 *     pad to 4, f32[dim][256] mean and f32[dim][256] variance codebooks;
 *     then the lf0 voicing bitmap (8-byte header, one bit per pdf)
 *   <WINDOW-COEFFS> text
 */
#include "ics_convert.h"
#include "ics_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const unsigned char *p;
	size_t n;
} Bytes;

static unsigned u16at(const unsigned char *p) { return p[0] | (unsigned) p[1] << 8; }
static unsigned long u32at(const unsigned char *p)
{
	return p[0] | (unsigned long) p[1] << 8 | (unsigned long) p[2] << 16 | (unsigned long) p[3] << 24;
}

static const unsigned char *find(Bytes b, const char *s, size_t from)
{
	size_t n = strlen(s), i;
	for (i = from; i + n <= b.n; i++)
		if (memcmp(b.p + i, s, n) == 0)
			return b.p + i;
	return NULL;
}

/* Content between "<TAG>...\n" and the first "</TAG>". */
static int section(Bytes v, const char *tag, Bytes *out)
{
	char open[64], close[64];
	const unsigned char *a, *nl, *e;
	snprintf(open, sizeof open, "<%s>", tag);
	snprintf(close, sizeof close, "</%s>", tag);
	a = find(v, open, 0);
	if (!a)
		return 0;
	nl = memchr(a, '\n', (size_t) (v.p + v.n - a));
	e = find(v, close, 0);
	if (!nl || !e || e < nl + 1)
		return 0;
	out->p = nl + 1;
	out->n = (size_t) (e - (nl + 1));
	return 1;
}

static int tag_value(Bytes v, const char *tag, char *out, size_t cap)
{
	Bytes s;
	size_t a = 0, b;
	if (!section(v, tag, &s))
		return 0;
	b = s.n;
	while (a < b && (s.p[a] == ' ' || s.p[a] == '\n' || s.p[a] == '\r' || s.p[a] == '\t'))
		a++;
	while (b > a && (s.p[b - 1] == ' ' || s.p[b - 1] == '\n' || s.p[b - 1] == '\r' || s.p[b - 1] == '\t'))
		b--;
	if (b - a >= cap)
		return 0;
	memcpy(out, s.p + a, b - a);
	out[b - a] = '\0';
	return 1;
}

/* ----------------------------------------------------------- output buffer */

typedef struct {
	unsigned char *p;
	size_t n, cap;
	int failed;
} Buf;

static void put(Buf *b, const void *data, size_t n)
{
	if (b->failed)
		return;
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap : 1 << 16;
		unsigned char *q;
		while (cap < b->n + n)
			cap *= 2;
		q = (unsigned char *) realloc(b->p, cap);
		if (!q) {
			b->failed = 1;
			return;
		}
		b->p = q;
		b->cap = cap;
	}
	memcpy(b->p + b->n, data, n);
	b->n += n;
}

static void puts_(Buf *b, const char *s) { put(b, s, strlen(s)); }

static void printf_(Buf *b, const char *fmt, ...);
#include <stdarg.h>
static void printf_(Buf *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t) n >= sizeof tmp)
		b->failed = 1;
	else
		put(b, tmp, (size_t) n);
}

static void put_u32(Buf *b, unsigned long v)
{
	unsigned char x[4] = {(unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16),
		(unsigned char) (v >> 24)};
	put(b, x, 4);
}

/* Python's repr() of a float, for the values window files hold. */
static void put_repr(Buf *b, double v)
{
	char s[40];
	int prec;
	for (prec = 1; prec <= 17; prec++) {
		snprintf(s, sizeof s, "%.*g", prec, v);
		if (strtod(s, NULL) == v)
			break;
	}
	if (!strpbrk(s, ".eEn"))
		strcat(s, ".0");
	puts_(b, s);
}

/* ------------------------------------------------------------------ trees */

typedef struct {
	unsigned char *bits;
	unsigned *rank; /* ones up to and including each position */
	const unsigned char *vals;
	unsigned nbits, nvals;
} Tree;

static int tree_is_node(const Tree *t, unsigned p) { return p < t->nbits && t->bits[p]; }
static unsigned tree_value(const Tree *t, unsigned p) { return u16at(t->vals + 2 * (t->rank[p] - 1)); }
static unsigned tree_left(const Tree *t, unsigned p) { return 2 * t->rank[p] - 1; }

typedef struct {
	char **names;
	unsigned nq;
	unsigned ncompact;
	unsigned *group_off, *group_len;
	const unsigned char *group_ids;
	Tree trees[4][8];
	unsigned ntrees[4];
} TreeSet;

static void free_trees(TreeSet *ts)
{
	unsigned s, i;
	if (ts->names) {
		for (i = 0; i < ts->nq; i++)
			free(ts->names[i]);
		free(ts->names);
	}
	free(ts->group_off);
	free(ts->group_len);
	for (s = 0; s < 4; s++)
		for (i = 0; i < ts->ntrees[s]; i++) {
			free(ts->trees[s][i].bits);
			free(ts->trees[s][i].rank);
		}
}

static int parse_trees(Bytes v, TreeSet *ts)
{
	const unsigned char *dsp = find(v, "</DSP>", 0), *models = find(v, "<MODELS>", 0), *nl, *p, *end;
	unsigned long size;
	unsigned i, k, ns;
	memset(ts, 0, sizeof *ts);
	if (!dsp || !models || !(nl = memchr(dsp, '\n', (size_t) (models - dsp))))
		return 0;
	p = nl + 1;
	if (models - p < 4 || (size = u32at(p)) > (unsigned long) (models - p - 4))
		return 0;
	end = p + 4 + size;
	p += 4;
#define NEED(n) do { if ((size_t) (end - p) < (size_t) (n)) return 0; } while (0)
	NEED(2);
	ts->nq = u16at(p);
	p += 2;
	NEED(ts->nq);
	ts->names = (char **) calloc(ts->nq ? ts->nq : 1, sizeof(char *));
	if (!ts->names)
		return 0;
	{
		const unsigned char *lens = p, *q = p + ts->nq;
		for (i = 0; i < ts->nq; i++) {
			if ((size_t) (end - q) < lens[i] || !(ts->names[i] = (char *) malloc(lens[i] + 1u)))
				return 0;
			memcpy(ts->names[i], q, lens[i]);
			ts->names[i][lens[i]] = '\0';
			q += lens[i];
		}
		p = q;
	}
	NEED(2);
	ts->ncompact = u16at(p);
	p += 2;
	NEED(ts->ncompact);
	ts->group_off = (unsigned *) malloc(sizeof(unsigned) * (ts->ncompact + 1));
	ts->group_len = (unsigned *) malloc(sizeof(unsigned) * (ts->ncompact + 1));
	if (!ts->group_off || !ts->group_len)
		return 0;
	{
		unsigned total = 0;
		for (i = 0; i < ts->ncompact; i++) {
			ts->group_off[i] = total;
			ts->group_len[i] = p[i];
			total += p[i];
		}
		p += ts->ncompact;
		NEED(2 * (size_t) total);
		ts->group_ids = p;
		for (i = 0; i < total; i++)
			if (u16at(p + 2 * i) >= ts->nq)
				return 0;
		p += 2 * (size_t) total;
	}
	NEED(1);
	ns = *p++;
	for (k = 0; k < ns; k++) {
		unsigned sid, nt, t;
		NEED(3);
		sid = p[0];
		nt = u16at(p + 1);
		p += 3;
		if (sid >= 4 || nt > 8)
			return 0;
		ts->ntrees[sid] = nt;
		for (t = 0; t < nt; t++) {
			Tree *tr = &ts->trees[sid][t];
			unsigned long words, nbits, c1, c2, w, ones = 0;
			NEED(8);
			words = u32at(p);
			nbits = u32at(p + 4);
			p += 8;
			if (nbits > words * 32)
				return 0;
			NEED(4 * words);
			tr->nbits = (unsigned) nbits;
			tr->bits = (unsigned char *) malloc(nbits + 1);
			tr->rank = (unsigned *) malloc(sizeof(unsigned) * (nbits + 1));
			if (!tr->bits || !tr->rank)
				return 0;
			for (w = 0; w < nbits; w++) {
				tr->bits[w] = (unsigned char) ((u32at(p + 4 * (w / 32)) >> (w % 32)) & 1);
				ones += tr->bits[w];
				tr->rank[w] = (unsigned) ones;
			}
			p += 4 * words;
			NEED(4);
			c1 = u32at(p);
			p += 4;
			NEED(4 * c1);
			p += 4 * c1;
			NEED(4);
			c2 = u32at(p);
			p += 4;
			NEED(c2 + (4 - c2 % 4) % 4);
			p += c2 + (4 - c2 % 4) % 4;
			NEED(2);
			tr->nvals = u16at(p);
			p += 2;
			NEED(2 * (size_t) tr->nvals);
			if (tr->nvals != ones)
				return 0;
			tr->vals = p;
			p += 2 * (size_t) tr->nvals;
		}
	}
#undef NEED
	return p == end;
}

static void put_questions(Buf *b, const TreeSet *ts)
{
	unsigned i, j;
	for (i = 0; i < ts->ncompact; i++) {
		printf_(b, "QS \"Q%u\" { ", i);
		for (j = 0; j < ts->group_len[i]; j++) {
			if (j)
				puts_(b, ",");
			printf_(b, "\"%s\"", ts->names[u16at(ts->group_ids + 2 * (ts->group_off[i] + j))]);
		}
		puts_(b, " }\n");
	}
}

/* Yes is the left child; hts_engine lines are "index question no yes". */
static int put_tree(Buf *b, const Tree *t, int state, const char *prefix)
{
	unsigned *queue = (unsigned *) malloc(sizeof(unsigned) * (t->nbits + 1));
	int *index = (int *) malloc(sizeof(int) * (t->nbits + 1));
	unsigned head = 0, tail = 0;
	int next_id = -1;
	if (!queue || !index) {
		free(queue);
		free(index);
		return 0;
	}
	printf_(b, "{*}[%d]\n{\n", state);
	index[0] = 0;
	queue[tail++] = 0;
	while (head < tail) {
		unsigned p = queue[head++], c, k;
		char refs[2][64];
		for (k = 0; k < 2; k++) {
			c = tree_left(t, p) + (k == 0 ? 1 : 0); /* no (right) first, then yes (left) */
			if (tree_is_node(t, tree_left(t, c))) {
				index[c] = next_id--;
				queue[tail++] = c;
				snprintf(refs[k], sizeof refs[k], "%d", index[c]);
			} else {
				snprintf(refs[k], sizeof refs[k], "\"%s_s%d_%u\"", prefix, state, tree_value(t, c));
			}
		}
		printf_(b, "%6d \"Q%u\" %s %s\n", index[p], tree_value(t, p), refs[0], refs[1]);
	}
	puts_(b, "}\n\n");
	free(queue);
	free(index);
	return 1;
}

/* ----------------------------------------------------------------- models */

typedef struct {
	unsigned dim, counts[8], total;
	const unsigned char *q;   /* u16[total * dim] */
	const unsigned char *cb;  /* f32 means[dim][256], then variances[dim][256] */
} Stream;

static int parse_stream(Bytes m, size_t *off, unsigned ns, Stream *s)
{
	size_t o = *off, data, end, i;
	if (o + 2 + 2 * ns > m.n)
		return 0;
	s->dim = u16at(m.p + o);
	s->total = 0;
	for (i = 0; i < ns; i++)
		s->total += s->counts[i] = u16at(m.p + o + 2 + 2 * i);
	data = o + 2 + 2 * ns;
	end = data + (size_t) s->total * s->dim * 2;
	end += (4 - end % 4) % 4;
	if (!s->dim || end + (size_t) s->dim * 2048 > m.n)
		return 0;
	s->q = m.p + data;
	s->cb = m.p + end;
	*off = end + (size_t) s->dim * 2048;
	return 1;
}

/* Mean (which = 0) or variance (1) of pdf row r, dimension d, as raw f32. */
static const unsigned char *param(const Stream *s, unsigned r, unsigned d, int which)
{
	unsigned q = u16at(s->q + 2 * ((size_t) r * s->dim + d));
	unsigned idx = which ? (q & 0xff) : (q >> 8);
	return s->cb + 4 * ((size_t) which * s->dim * 256 + (size_t) d * 256 + idx);
}

static unsigned row_start(const Stream *s, unsigned state)
{
	unsigned i, start = 0;
	for (i = 0; i < state; i++)
		start += s->counts[i];
	return start;
}

/* -------------------------------------------------------------- converter */

typedef struct {
	const char *key;
	size_t start, end;
} Pos;

int icsc_convert(const unsigned char *data, size_t len, const char *out_path, const char *source_name)
{
	Bytes v = {data, len}, models, wins;
	TreeSet ts;
	Stream st[4];
	Buf out = {0}, d = {0};
	Pos pos[16];
	int npos = 0, rc = ICSC_ERR_FORMAT;
	unsigned ns, s, k, r, j;
	size_t off;
	char sr[32], fp[32], warp[32], stage[32];
	char wtext[3][4][256];
	unsigned nwin[3] = {0, 0, 0};
	FILE *f;

	if (len < 16 || memcmp(data, "PHONETIC-ARTS3", 14) != 0)
		return ICSC_ERR_FORMAT;
	if (!parse_trees(v, &ts))
		goto done;
	if (ts.ntrees[0] != 1 || ts.ntrees[1] != 5 || ts.ntrees[3] != 5)
		goto done;
	if (!tag_value(v, "SAMPLE-RATE", sr, sizeof sr) || !tag_value(v, "FRAME-PERIOD", fp, sizeof fp)
			|| !tag_value(v, "FREQ-WARPING", warp, sizeof warp))
		goto done;
	if (!tag_value(v, "STAGE", stage, sizeof stage) && !tag_value(v, "MGC-GAMMA-STAGE", stage, sizeof stage))
		strcpy(stage, "0");
	if (!section(v, "MODELS", &models) || models.n < 4)
		goto done;
	ns = (unsigned) u32at(models.p);
	if (ns != 5)
		goto done;
	off = 4;
	for (s = 0; s < 4; s++)
		if (!parse_stream(models, &off, ns, &st[s]))
			goto done;
	if (st[0].dim != 1 || st[1].dim % 3 || st[3].dim != 3)
		goto done;
	if (off + 8 + (st[3].total + 7) / 8 > models.n)
		goto done;

	/* Window coefficients: lines under <mcp> and <lf0>, "size c0 c1 ...". */
	if (!section(v, "WINDOW-COEFFS", &wins))
		goto done;
	{
		const char *tags[2] = {"<mcp>", "<lf0>"};
		unsigned w;
		for (w = 0; w < 2; w++) {
			Bytes sub;
			const unsigned char *a = find(wins, tags[w], 0), *p;
			if (!a)
				goto done;
			sub.p = a;
			sub.n = (size_t) (wins.p + wins.n - a);
			p = memchr(a, '\n', sub.n);
			while (p && nwin[w] < 3) {
				const unsigned char *e;
				char line[256];
				size_t n;
				p++;
				e = memchr(p, '\n', (size_t) (wins.p + wins.n - p));
				if (!e)
					break;
				n = (size_t) (e - p);
				if (n >= sizeof line)
					goto done;
				memcpy(line, p, n);
				line[n] = '\0';
				{
					char *t = line, *endp;
					Buf lb = {0};
					double size;
					while (*t == ' ')
						t++;
					if (*t == '<')
						break;
					size = strtod(t, &endp);
					if (endp == t)
						break;
					printf_(&lb, "%d", (int) size);
					for (t = endp;;) {
						double c = strtod(t, &endp);
						if (endp == t)
							break;
						puts_(&lb, " ");
						put_repr(&lb, c);
						t = endp;
					}
					puts_(&lb, "\n");
					put(&lb, "", 1);
					if (lb.failed || lb.n > sizeof wtext[w][nwin[w]]) {
						free(lb.p);
						goto done;
					}
					memcpy(wtext[w][nwin[w]++], lb.p, lb.n);
					free(lb.p);
				}
				p = e;
			}
			if (nwin[w] != 3)
				goto done;
		}
	}

#define BEGIN(name) (pos[npos].key = (name), pos[npos].start = d.n)
#define END() (pos[npos].end = d.n - 1, npos++)
	/* duration: one tree; pdf k holds the five state durations */
	BEGIN("DURATION_PDF");
	{
		unsigned n = st[0].counts[0];
		put_u32(&d, n);
		for (k = 0; k < n; k++) {
			for (s = 0; s < ns; s++)
				put(&d, param(&st[0], s * n + k, 0, 0), 4);
			for (s = 0; s < ns; s++)
				put(&d, param(&st[0], s * n + k, 0, 1), 4);
		}
	}
	END();
	BEGIN("DURATION_TREE");
	put_questions(&d, &ts);
	puts_(&d, "\n");
	if (!put_tree(&d, &ts.trees[0][0], 2, "dur"))
		goto oom;
	END();
	for (j = 0; j < 2; j++)
		for (k = 0; k < 3; k++) {
			BEGIN(j ? "WINLF0" : "WINMCP");
			puts_(&d, wtext[j][k]);
			END();
		}
	for (j = 1; j <= 3; j += 2) {
		const Stream *x = &st[j];
		BEGIN(j == 1 ? "STREAM_PDF[MCP]" : "STREAM_PDF[LF0]");
		for (s = 0; s < ns; s++)
			put_u32(&d, x->counts[s]);
		for (s = 0; s < ns; s++) {
			unsigned start = row_start(x, s);
			for (r = start; r < start + x->counts[s]; r++) {
				unsigned dd;
				for (dd = 0; dd < x->dim; dd++)
					put(&d, param(x, r, dd, 0), 4);
				for (dd = 0; dd < x->dim; dd++)
					put(&d, param(x, r, dd, 1), 4);
				if (j == 3) {
					const unsigned char *msd = models.p + off + 8;
					float w = (float) ((msd[r >> 3] >> (r & 7)) & 1);
					put(&d, &w, 4);
				}
			}
		}
		END();
	}
	for (j = 1; j <= 3; j += 2) {
		BEGIN(j == 1 ? "STREAM_TREE[MCP]" : "STREAM_TREE[LF0]");
		put_questions(&d, &ts);
		puts_(&d, "\n");
		for (s = 0; s < ns; s++)
			if (!put_tree(&d, &ts.trees[j][s], (int) s + 2, j == 1 ? "mcp" : "lf0"))
				goto oom;
		END();
	}
#undef BEGIN
#undef END
	if (d.failed)
		goto oom;

	puts_(&out, "[GLOBAL]\nHTS_VOICE_VERSION:1.0\n");
	printf_(&out, "SAMPLING_FREQUENCY:%d\nFRAME_PERIOD:%d\nNUM_STATES:%u\n", atoi(sr), atoi(fp), ns);
	puts_(&out, "NUM_STREAMS:2\nSTREAM_TYPE:MCP,LF0\nFULLCONTEXT_FORMAT:FLINT_ICS\nFULLCONTEXT_VERSION:1.0\n"
		"GV_OFF_CONTEXT:\"*-sil+*\"\n");
	printf_(&out, "COMMENT:converted from %s\n", source_name ? source_name : "voice");
	printf_(&out, "[STREAM]\nVECTOR_LENGTH[MCP]:%u\nVECTOR_LENGTH[LF0]:1\nIS_MSD[MCP]:0\nIS_MSD[LF0]:1\n"
		"NUM_WINDOWS[MCP]:3\nNUM_WINDOWS[LF0]:3\nUSE_GV[MCP]:0\nUSE_GV[LF0]:0\n", st[1].dim / 3);
	printf_(&out, "OPTION[MCP]:ALPHA=%.3f,GAMMA=%d,LN_GAIN=0\nOPTION[LF0]:\n", strtod(warp, NULL), atoi(stage));
	puts_(&out, "[POSITION]\n");
	printf_(&out, "DURATION_PDF:%u-%u\n", (unsigned) pos[0].start, (unsigned) pos[0].end);
	printf_(&out, "DURATION_TREE:%u-%u\n", (unsigned) pos[1].start, (unsigned) pos[1].end);
	printf_(&out, "STREAM_WIN[MCP]:%u-%u,%u-%u,%u-%u\n", (unsigned) pos[2].start, (unsigned) pos[2].end,
		(unsigned) pos[3].start, (unsigned) pos[3].end, (unsigned) pos[4].start, (unsigned) pos[4].end);
	printf_(&out, "STREAM_WIN[LF0]:%u-%u,%u-%u,%u-%u\n", (unsigned) pos[5].start, (unsigned) pos[5].end,
		(unsigned) pos[6].start, (unsigned) pos[6].end, (unsigned) pos[7].start, (unsigned) pos[7].end);
	printf_(&out, "STREAM_PDF[MCP]:%u-%u\n", (unsigned) pos[8].start, (unsigned) pos[8].end);
	printf_(&out, "STREAM_PDF[LF0]:%u-%u\n", (unsigned) pos[9].start, (unsigned) pos[9].end);
	printf_(&out, "STREAM_TREE[MCP]:%u-%u\n", (unsigned) pos[10].start, (unsigned) pos[10].end);
	printf_(&out, "STREAM_TREE[LF0]:%u-%u\n", (unsigned) pos[11].start, (unsigned) pos[11].end);
	puts_(&out, "[DATA]\n");
	put(&out, d.p, d.n);
	if (out.failed)
		goto oom;
	f = ics_fopen(out_path, "wb");
	if (!f) {
		rc = ICSC_ERR_WRITE;
		goto done;
	}
	rc = fwrite(out.p, 1, out.n, f) == out.n ? ICSC_OK : ICSC_ERR_WRITE;
	if (fclose(f) != 0)
		rc = ICSC_ERR_WRITE;
	if (rc != ICSC_OK)
		ics_remove(out_path);
	goto done;
oom:
	rc = ICSC_ERR_MEMORY;
done:
	free_trees(&ts);
	free(out.p);
	free(d.p);
	return rc;
}
