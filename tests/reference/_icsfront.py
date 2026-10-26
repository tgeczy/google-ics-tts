"""Text -> ICS Flint full-context labels (the format the eng-usa voice was trained on).

Label layout (fields from assets/common/compile_cfg):
  ^L-C+R=/B:phon_pos:/C:sonorant:/D:stress:/E:phrasing:/F:word_type:
  /p1D:/p1E:/p1F:/n1C:/n1D:/n1E:/n1F:/p1sD:/n1sD:
Pronunciations come from CMUdict, mapped onto the ICS phone set.
"""
import re

from . import _nrl

ARPA = {
    'AA': 'aa', 'AE': 'a', 'AH': 'uh', 'AO': 'oo', 'AW': 'au', 'AY': 'ai',
    'EH': 'e', 'ER': 'er', 'EY': 'ei', 'IH': 'i', 'IY': 'ii', 'OW': 'ou',
    'OY': 'oi', 'UH': 'u', 'UW': 'uu',
    'B': 'b', 'CH': 'ch', 'D': 'd', 'DH': 'dh', 'F': 'f', 'G': 'g', 'HH': 'h',
    'JH': 'jh', 'K': 'k', 'L': 'l', 'M': 'm', 'N': 'n', 'NG': 'ng', 'P': 'p',
    'R': 'r', 'S': 's', 'SH': 'sh', 'T': 't', 'TH': 'th', 'V': 'v', 'W': 'w',
    'Y': 'y', 'Z': 'z', 'ZH': 'zh',
}
VOWELS = {'a', 'aa', 'au', 'ax', 'e', 'i', 'ii', 'oo', 'ou', 'u', 'uu', 'uh', 'ai', 'ei', 'oi', 'er',
          'o', 'eax', 'iax', 'uax', 'aax'}
ARPA_VOWELS = {'AA', 'AE', 'AH', 'AO', 'AW', 'AY', 'EH', 'ER', 'EY', 'IH', 'IY', 'OW', 'OY', 'UH', 'UW'}

# RP (eng-gbr voice): a vowel + coda R from CMUdict becomes one non-rhotic vowel.
RP_BEFORE_R = {'AA': 'aa', 'AO': 'oo', 'OW': 'oo', 'EH': 'eax', 'AE': 'eax', 'EY': 'eax',
               'IH': 'iax', 'IY': 'iax', 'UH': 'uax', 'UW': 'uax', 'AH': 'aax'}
RP_PLAIN = {'AE': 'a', 'AW': 'au', 'AY': 'ai', 'EH': 'e', 'EY': 'ei', 'IH': 'i', 'IY': 'ii',
            'OW': 'ou', 'OY': 'oi', 'UH': 'u', 'UW': 'uu'}
RP_BATH = set('''after afternoon answer ask asked asking basket bath brass can't cast castle class
craft dance draft example fast father grass half last laugh laughed laughter mask master nasty
pass passed past path plant rather staff task vast branch chance demand command glass'''.split())
RP_STRUT_LOT = set("of from because what what's whatever wasn't cos".split())
SONORANT_CONS = {'n', 'm', 'ng', 'l', 'r', 'y', 'w'}


def _set(s):
    return s.split()


ONE_INITIAL = _set('b ch d dh f g h jh k l m n p r s sh t th v w y z zh')
_PAIRS = [
    ('p b k f h v m n g', 'y'), ('p b k g f v', 'l r'), ('s z l', 'y'),
    ('s sh', 'v'), ('g', 'w r'), ('t d th sh k', 'r w y'),
    ('s', 'p t k n m l r w f'), ('p', 'w'),
]
_TRIPLES = [
    ('s sh', 'p', 'y'), ('s sh', 't', 'r'), ('s sh', 't', 'y'), ('s sh', 'k', 'r w y'),
    ('s sh', 'k', 'l r'), ('s sh', 'p', 'l r y'),
]
ONSETS = {(c,) for c in ONE_INITIAL}
ONSETS |= {(a, b) for x, y in _PAIRS for a in _set(x) for b in _set(y)}
ONSETS |= {(a, b, c) for x, y, z in _TRIPLES for a in _set(x) for b in _set(y) for c in _set(z)}

WORD_TYPES = set('''am an and are as at because been but by could could've coulda couldn't de do does
doesn't for from had has have he he'd he'll he's her him his huh i'd i'll i'm in la le of or per se
shall she'd she'll she's sir so some than that that's the their them there there'll there's there've
they'd they'll they're this this'll to twas uh us was we we'd we'll we're we've were what what's where
where'd where're where's will would would've wouldn't you you'd you'll you're you've your'''.split())
WORD_SPECIAL = {'a': 'a_determiner', 'i': 'i_pronoun', 'can': 'can_vrb', 'cause': 'cause_cnj', 'cos': 'cos_cnj'}

ONES = 'zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen'.split()
TENS = 'twenty thirty forty fifty sixty seventy eighty ninety'.split()


def number_words(n):
    if n < 20:
        return [ONES[n]]
    if n < 100:
        return [TENS[n // 10 - 2]] + ([ONES[n % 10]] if n % 10 else [])
    if n < 1000:
        return [ONES[n // 100], 'hundred'] + (number_words(n % 100) if n % 100 else [])
    for div, name in ((10 ** 9, 'billion'), (10 ** 6, 'million'), (1000, 'thousand')):
        if n >= div:
            return number_words(n // div) + [name] + (number_words(n % div) if n % div else [])
    return [ONES[d] for d in map(int, str(n))]


def _rp_lot(word, base):
    """CMUdict's AA/AO cover both RP 'o' (lot, dog) and the long vowels (father, thought)."""
    if base == 'AA':
        if 'o' in word and 'a' not in word:
            return 'o'
        if 'a' in word and not re.search(r'wa|qua', word):
            return 'aa'
        return 'o'
    if re.search(r'au|aw|al[lkt]|ough|wa', word) or ('a' in word and 'o' not in word):
        return 'oo'
    return 'o'


def _to_rp(word, arpa):
    """CMUdict (General American) -> eng-gbr RP phones with stress."""
    toks = [re.match(r'([A-Z]+)([012]?)$', p).groups() for p in arpa]
    out = []
    i = 0
    while i < len(toks):
        base, st = toks[i]
        stress = int(st) if st else None
        nxt = toks[i + 1][0] if i + 1 < len(toks) else None
        after = toks[i + 2][0] if i + 2 < len(toks) else None
        if base not in ARPA_VOWELS:
            out.append((ARPA[base], None))
            i += 1
            continue
        full = 2 if stress == 0 else stress
        if base == 'ER':
            out.append(('ax', 0) if stress == 0 else ('aax', stress))
            if nxt in ARPA_VOWELS:
                out.append(('r', None))
            i += 1
            continue
        if nxt == 'R' and after not in ARPA_VOWELS and base in RP_BEFORE_R:
            if base == 'AH' and stress == 0:
                out.append(('ax', 0))
            else:
                out.append((RP_BEFORE_R[base], full))
            i += 2
            continue
        if base == 'AH':
            if stress == 0:
                out.append(('ax', 0))
            else:
                out.append(('o' if word in RP_STRUT_LOT else 'uh', stress))
        elif base in ('AA', 'AO'):
            out.append((_rp_lot(word, base), full))
        elif base == 'AE' and word in RP_BATH and stress:
            out.append(('aa', full))
        else:
            out.append((RP_PLAIN[base], full))
        i += 1
    return out


def _ga_ao(word, before_r):
    """CMUdict AO: THOUGHT 'oo' for au/aw/al/ough spellings and before R, else the 'aa' of log/frog."""
    if before_r or re.search(r'au|aw|al[lkt]|ough', word) or 'o' not in word:
        return 'oo'
    return 'aa'


def _to_ga(word, arpa):
    """CMUdict (General American) -> eng-usa phones with stress."""
    out = []
    for i, p in enumerate(arpa):
        m = re.match(r'([A-Z]+)([012]?)$', p)
        base, st = m.group(1), m.group(2)
        if not st:
            out.append((ARPA[base], None))
            continue
        st = int(st)
        if base == 'AH':
            out.append(('ax', 0) if st == 0 else ('uh', st))
        elif base == 'ER':
            out.append(('er', st))
        elif base == 'AO':
            before_r = i + 1 < len(arpa) and arpa[i + 1] == 'R'
            out.append((_ga_ao(word, before_r), 2 if st == 0 else st))
        else:
            out.append((ARPA[base], 2 if st == 0 else st))
    return out


#: Words NVDA speaks (symbols.dic) that CMUdict lacks.
EXTRA_WORDS = {
    'paren': 'P AH0 R EH1 N', 'parens': 'P AH0 R EH1 N Z', 'caret': 'K EH1 R AH0 T',
    'cedilla': 'S AH0 D IH1 L AH0', 'diaeresis': 'D AY0 EH1 R AH0 S IH0 S', 'macron': 'M EY1 K R AA0 N',
    'semicolon': 'S EH1 M IY0 K OW2 L AH0 N', 'subscript': 'S AH1 B S K R IH2 P T',
    'superscript': 'S UW1 P ER0 S K R IH2 P T', 'superset': 'S UW1 P ER0 S EH2 T',
    'fullwidth': 'F UH1 L W IH2 D TH', 'servicemark': 'S ER1 V AH0 S M AA2 R K',
    'anticlockwise': 'AE2 N T IY0 K L AA1 K W AY2 Z', 'ideographic': 'IH2 D IY0 AH0 G R AE1 F IH0 K',
    'coproduct': 'K OW0 P R AA1 D AH0 K T', 'multiset': 'M AH1 L T IY0 S EH2 T',
    'nonnegative': 'N AA0 N N EH1 G AH0 T IH0 V', 'quaternions': 'K W AH0 T ER1 N IY0 AH0 N Z',
    'asymptotically': 'EY2 S IH0 M P T AA1 T IH0 K L IY0', 'xray': 'EH1 K S R EY2',
}


class Lexicon:
    def __init__(self, path, accent='us'):
        self.accent = accent
        self.entries = {}
        with open(path, encoding='utf-8') as f:
            for line in f:
                parts = line.split('#', 1)[0].split()
                if not parts or parts[0].startswith(';;;'):
                    continue
                word = re.sub(r'\(\d+\)$', '', parts[0].lower())
                if word not in self.entries:
                    self.entries[word] = parts[1:]
        for word, arpa in EXTRA_WORDS.items():
            self.entries.setdefault(word, arpa.split())

    def pron(self, word):
        """[(phone, stress or None)] in the ICS phone set, or None."""
        arpa = self.entries.get(word)
        return None if arpa is None else self._convert(word, arpa)

    def guess(self, word):
        """Letter-to-sound pronunciation for a word CMUdict lacks."""
        arpa = _nrl.guess(word)
        return None if arpa is None else self._convert(word, arpa)

    def _convert(self, word, arpa):
        if self.accent == 'gb':
            return _to_rp(word, arpa)
        return _to_ga(word, arpa)


def tokenize(text):
    """Words (case kept, for acronym detection), spelled-out numbers and punctuation."""
    text = text.replace('\u2019', "'")
    toks = []
    for m in re.finditer(r"\d+|[A-Za-z]+(?:'[A-Za-z]+)*|[.,!?;:]", text):
        t = m.group(0)
        if t.isdigit():
            toks.extend(number_words(int(t)) if len(t) <= 12 else [ONES[int(d)] for d in t])
        else:
            toks.append(t)
    return toks


def _letters(lex, word):
    out = []
    for ch in word.lower():
        p = lex.pron(ch) or lex.pron(ch + '.')
        if p:
            out.append([ch + '_letter', p, None])
    return out


def words_from_text(lex, text, final='s'):
    """[(word_type, [(phone, stress)], break_after)] where break is None, 'p' or 's'."""
    words = []
    for tok in tokenize(text):
        if tok in '.!?;:,':
            if words:
                words[-1][2] = 's' if tok in '.!?' else 'p'
            continue
        t = tok.lower()
        if len(t) == 1 and t not in ('a', 'i'):
            words.extend(_letters(lex, t))
            continue
        pron = lex.pron(t)
        if pron is not None:
            wt = WORD_SPECIAL.get(t, t if t in WORD_TYPES else '0')
            words.append([wt, pron, None])
            continue
        if (tok.isupper() and len(tok) <= 5) or not re.search(r'[aeiouy]', t):
            words.extend(_letters(lex, t))
            continue
        pron = lex.guess(t)
        if pron:
            words.append(['0', pron, None])
        else:
            words.extend(_letters(lex, t))
    if words and words[-1][2] is None:
        words[-1][2] = final
    return words


def syllabify(pron):
    """Split one word into syllables: list of (onset, vowel, coda, stress)."""
    phones = [p for p, _ in pron]
    vidx = [i for i, p in enumerate(phones) if p in VOWELS]
    if not vidx:
        return [(phones, None, [], 0)] if phones else []
    sylls = []
    start = 0
    for n, vi in enumerate(vidx):
        if n + 1 < len(vidx):
            nxt = vidx[n + 1]
            cluster = phones[vi + 1:nxt]
            split = len(cluster)
            for k in range(len(cluster) + 1):
                if k == len(cluster) or tuple(cluster[k:]) in ONSETS:
                    split = k
                    break
            end = vi + 1 + split
        else:
            end = len(phones)
        sylls.append((phones[start:vi], phones[vi], phones[vi + 1:end], pron[vi][1]))
        start = end
    return sylls


def segments(words):
    """Flatten words into phone segments with per-segment features."""
    segs = [dict(name='sil', B='|sil|', D='sil', E='sil', F='sil', syl=-1)]
    syl_stress = ['sil']
    for wi, (wt, pron, brk) in enumerate(words):
        phrasing = {'s': 's_final', 'p': 'p_final'}.get(brk, 'mid')
        word_segs = []
        for onset, vowel, coda, stress in syllabify(pron):
            syl_stress.append(str(stress))
            sidx = len(syl_stress) - 1
            for i, c in enumerate(onset):
                word_segs.append(dict(name=c, B='i%d%d' % (min(i + 1, 3), min(len(onset), 3)), syl=sidx))
            if vowel is not None:
                word_segs.append(dict(name=vowel, B='v', syl=sidx))
            for i, c in enumerate(coda):
                word_segs.append(dict(name=c, B='f%d%d' % (min(i + 1, 3), min(len(coda), 3)), syl=sidx))
        if not word_segs:
            continue
        first, last = word_segs[0], word_segs[-1]
        if first['B'].startswith('i'):
            first['B'] = '|i1' + first['B'][2]
        elif first['B'] == 'v':
            first['B'] = '|v'
        if last['B'].startswith('f'):
            last['B'] = 'f%s%s|' % (last['B'][2], last['B'][2])
        elif last['B'] in ('v', '|v'):
            last['B'] += '|'
        for s in word_segs:
            s.update(D=syl_stress[s['syl']], E=phrasing, F=wt)
        segs.extend(word_segs)
        # A pause right before the final silence is a sil-sil context the
        # voice never saw; its gain explodes into a full-scale thump.
        if brk == 'p' and wi < len(words) - 1:
            segs.append(dict(name='sil', B='sil', D='sil', E='sil', F='sil', syl=-1))
    segs.append(dict(name='sil', B='|sil|', D='sil', E='sil', F='sil', syl=-1))
    syl_stress.append('sil')

    for i, s in enumerate(segs):
        nxt = segs[i + 1]['name'] if i + 1 < len(segs) else 'sil'
        # Obstruents always carry 0: the voice never saw p/t/k with a sonorant
        # here, and its trees then pick vowel-like, very loud bursts.
        if s['name'] not in VOWELS and s['name'] not in SONORANT_CONS:
            s['C'] = '0'
        elif nxt in SONORANT_CONS:
            s['C'] = nxt
        elif s['name'] in SONORANT_CONS and nxt in VOWELS:
            s['C'] = 'v'
        else:
            s['C'] = '0'
        if s['syl'] >= 0:
            s['pS'] = syl_stress[s['syl'] - 1]
            s['nS'] = syl_stress[s['syl'] + 1]
        else:
            s['pS'] = s['nS'] = 'sil'
    return segs


def labels(segs):
    out = []
    for i, s in enumerate(segs):
        p = segs[i - 1] if i > 0 else segs[0]
        n = segs[i + 1] if i + 1 < len(segs) else segs[-1]
        out.append(
            '^%s-%s+%s=/B:%s:/C:%s:/D:%s:/E:%s:/F:%s:/p1D:%s:/p1E:%s:/p1F:%s:'
            '/n1C:%s:/n1D:%s:/n1E:%s:/n1F:%s:/p1sD:%s:/n1sD:%s:' % (
                p['name'], s['name'], n['name'], s['B'], s['C'], s['D'], s['E'], s['F'],
                p['D'], p['E'], p['F'], n['C'], n['D'], n['E'], n['F'], s['pS'], s['nS']))
    return out


def text_to_labels(lex, text, final='s'):
    """final: phrasing of an unpunctuated end, 's' (sentence-final) or None (continues)."""
    return labels(segments(words_from_text(lex, text, final)))


if __name__ == '__main__':
    import sys
    lex = Lexicon(sys.argv[1])
    for line in text_to_labels(lex, ' '.join(sys.argv[2:])):
        print(line)
