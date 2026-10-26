// text.h -- SAPI fragments into speakable batches, and batches into sentences.
//
// JAWS sends each word as its own fragment with a bookmark between them; the
// words are batched into one utterance (so they are not spoken as isolated
// words) and each bookmark remembers its character position, so its event
// fires when the audio reaches that point.
#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>

#include <string>
#include <vector>

namespace ics {

struct Mark {
	std::wstring name;
	size_t pos;  // character position in Batch::text
};

struct Batch {
	enum Kind { Speak, Spell, Silence } kind = Speak;
	std::wstring text;
	std::vector<Mark> marks;
	std::vector<std::pair<size_t, ULONG>> src;  // (position in text, source offset) per fragment
	SPVSTATE state{};
	bool has_state = false;
	ULONG silence_ms = 0;
	ULONG source_offset(size_t pos) const;
};

// A change of rate, volume or pitch starts a new batch.
std::vector<Batch> batch_fragments(const SPVTEXTFRAG *frags);

struct Sentence {
	size_t begin, end;  // [begin, end) in the batch text
};

// Splits after . ! ? followed by whitespace, which is not part of the next
// sentence; never returns an empty list.
std::vector<Sentence> split_sentences(const std::wstring &text);

}  // namespace ics
