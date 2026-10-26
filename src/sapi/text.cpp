// text.cpp -- see text.h.
#include "text.h"

#include <cwctype>

namespace ics {

ULONG Batch::source_offset(size_t pos) const
{
	ULONG best = src.empty() ? 0 : src.front().second;
	for (const auto &s : src)
		if (s.first <= pos)
			best = s.second + (ULONG)(pos - s.first);
	return best;
}

static bool same_voice(const SPVSTATE &a, const SPVSTATE &b)
{
	return a.RateAdj == b.RateAdj && a.Volume == b.Volume && a.PitchAdj.MiddleAdj == b.PitchAdj.MiddleAdj;
}

std::vector<Batch> batch_fragments(const SPVTEXTFRAG *frags)
{
	std::vector<Batch> out;
	Batch cur;
	auto flush = [&]() {
		if (!cur.text.empty() || !cur.marks.empty())
			out.push_back(std::move(cur));
		cur = Batch();
	};
	for (const SPVTEXTFRAG *f = frags; f; f = f->pNext) {
		switch (f->State.eAction) {
		case SPVA_Bookmark:
			if (f->pTextStart)
				cur.marks.push_back({std::wstring(f->pTextStart, f->ulTextLen), cur.text.size()});
			break;
		case SPVA_Speak:
		case SPVA_Pronounce:
			if (cur.has_state && !cur.text.empty() && !same_voice(cur.state, f->State))
				flush();
			if (!cur.has_state) {
				cur.state = f->State;
				cur.has_state = true;
			}
			if (f->pTextStart && f->ulTextLen) {
				// Words arrive as separate fragments; keep them apart.
				if (!cur.text.empty() && !iswspace(cur.text.back()) && !iswspace(f->pTextStart[0]))
					cur.text += L' ';
				cur.src.push_back({cur.text.size(), f->ulTextSrcOffset});
				cur.text.append(f->pTextStart, f->ulTextLen);
			}
			break;
		case SPVA_SpellOut: {
			flush();
			Batch b;
			b.kind = Batch::Spell;
			b.state = f->State;
			b.has_state = true;
			if (f->pTextStart && f->ulTextLen) {
				b.src.push_back({0, f->ulTextSrcOffset});
				// One letter per word, which the front end speaks as letter names.
				for (ULONG i = 0; i < f->ulTextLen; i++) {
					if (iswspace(f->pTextStart[i]))
						continue;
					if (!b.text.empty())
						b.text += L' ';
					b.text += f->pTextStart[i];
				}
			}
			out.push_back(std::move(b));
			break;
		}
		case SPVA_Silence: {
			flush();
			Batch b;
			b.kind = Batch::Silence;
			b.silence_ms = f->State.SilenceMSecs;
			out.push_back(std::move(b));
			break;
		}
		default:
			flush();
			break;
		}
	}
	flush();
	return out;
}

std::vector<Sentence> split_sentences(const std::wstring &text)
{
	std::vector<Sentence> out;
	size_t begin = 0;
	for (size_t i = 0; i + 1 < text.size(); i++) {
		wchar_t c = text[i];
		if ((c == L'.' || c == L'!' || c == L'?') && iswspace(text[i + 1])) {
			out.push_back({begin, i + 1});
			for (begin = i + 1; begin < text.size() && iswspace(text[begin]); begin++)
				;
		}
	}
	if (begin < text.size() || out.empty())
		out.push_back({begin, text.size()});
	return out;
}

}  // namespace ics
