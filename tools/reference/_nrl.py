"""NRL letter-to-sound rules (port of Wasser's phoneme.c) for words CMUdict lacks."""
from ._nrl_rules import RULES

_VOWELS = set("AEIOU")
_VOICED = set("BDVGJLMNRWZ")
_FRONT = set("EIY")
_ARPA_VOWELS = {"IY", "IH", "EY", "EH", "AE", "AA", "AO", "OW", "UH", "UW", "ER", "AX", "AH", "AY", "AW", "OY"}


def _vowel(c):
	return c in _VOWELS


def _consonant(c):
	return c.isupper() and c not in _VOWELS


def _left(pattern, word, i):
	"""Match pattern leftward ending at word[i]."""
	for p in reversed(pattern):
		c = word[i] if i >= 0 else " "
		if p.isalpha() or p in "' ":
			if p != c:
				return False
			i -= 1
		elif p == "#":
			if not _vowel(c):
				return False
			i -= 1
			while i >= 0 and _vowel(word[i]):
				i -= 1
		elif p == ":":
			while i >= 0 and _consonant(word[i]):
				i -= 1
		elif p == "^":
			if not _consonant(c):
				return False
			i -= 1
		elif p == ".":
			if c not in _VOICED:
				return False
			i -= 1
		elif p == "+":
			if c not in _FRONT:
				return False
			i -= 1
		else:
			return False
	return True


def _right(pattern, word, i):
	"""Match pattern rightward starting at word[i]."""
	n = len(word)

	def at(k):
		return word[k] if k < n else " "

	for p in pattern:
		c = at(i)
		if p.isalpha() or p in "' ":
			if p != c:
				return False
			i += 1
		elif p == "#":
			if not _vowel(c):
				return False
			i += 1
			while _vowel(at(i)):
				i += 1
		elif p == ":":
			while _consonant(at(i)):
				i += 1
		elif p == "^":
			if not _consonant(c):
				return False
			i += 1
		elif p == ".":
			if c not in _VOICED:
				return False
			i += 1
		elif p == "+":
			if c not in _FRONT:
				return False
			i += 1
		elif p == "%":
			if c == "E":
				i += 1
				if at(i) == "L" and at(i + 1) == "Y":
					i += 2
				elif at(i) in "RSD":
					i += 1
			elif c == "I" and at(i + 1) == "N" and at(i + 2) == "G":
				i += 3
			else:
				return False
		else:
			return False
	return True


def _phonemes(word):
	"""Raw NRL output string for an uppercase word."""
	padded = " " + word + " "
	out = []
	i = 1
	while i < len(padded) - 1:
		c = padded[i]
		table = RULES.get(c if c.isupper() else "", RULES[""])
		for left, match, right, phon in table:
			if not padded.startswith(match, i):
				continue
			end = i + len(match)
			if _left(left, padded, i - 1) and _right(right, padded, end):
				out.append(phon)
				i = end
				break
		else:
			i += 1
	return "".join(out)


def guess(word):
	"""ARPAbet with stress digits (CMUdict style) for an unknown word, or None."""
	raw = _phonemes(word.upper())
	toks = []
	i = 0
	while i < len(raw):
		c = raw[i]
		if c == " ":
			i += 1
		elif c.isupper():
			toks.append(raw[i:i + 2])
			i += 2
		else:
			toks.append({"j": "JH", "h": "HH"}.get(c, c.upper()))
			i += 1
	arpa = []
	stressed = False
	for t in toks:
		if t == "WH":
			arpa.append("W")
		elif t == "AX":
			arpa.append("AH0")
		elif t in _ARPA_VOWELS:
			arpa.append(t + ("0" if stressed else "1"))
			stressed = True
		else:
			arpa.append(t)
	return arpa or None
