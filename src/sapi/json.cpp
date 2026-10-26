// json.cpp -- see json.h.
#include "json.h"

#include <cstdlib>
#include <cstring>

namespace ics {

const Json &Json::operator[](const char *key) const
{
	static const Json none;
	auto it = fields.find(key);
	return it == fields.end() ? none : it->second;
}

namespace {

struct Parser {
	const std::string &s;
	size_t i = 0;
	int depth = 0;

	void space()
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
			i++;
	}

	bool literal(const char *word)
	{
		size_t n = strlen(word);
		if (s.compare(i, n, word) != 0)
			return false;
		i += n;
		return true;
	}

	static void put_utf8(std::string &out, unsigned cp)
	{
		if (cp < 0x80) {
			out += (char)cp;
		} else if (cp < 0x800) {
			out += (char)(0xC0 | cp >> 6);
			out += (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char)(0xE0 | cp >> 12);
			out += (char)(0x80 | (cp >> 6 & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xF0 | cp >> 18);
			out += (char)(0x80 | (cp >> 12 & 0x3F));
			out += (char)(0x80 | (cp >> 6 & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	bool hex4(unsigned &cp)
	{
		if (i + 4 > s.size())
			return false;
		cp = 0;
		for (int k = 0; k < 4; k++) {
			char c = s[i++];
			cp <<= 4;
			if (c >= '0' && c <= '9')
				cp |= c - '0';
			else if (c >= 'a' && c <= 'f')
				cp |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				cp |= c - 'A' + 10;
			else
				return false;
		}
		return true;
	}

	bool string(std::string &out)
	{
		if (i >= s.size() || s[i] != '"')
			return false;
		i++;
		while (i < s.size()) {
			char c = s[i++];
			if (c == '"')
				return true;
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i >= s.size())
				return false;
			char e = s[i++];
			switch (e) {
			case '"': case '\\': case '/': out += e; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u': {
				unsigned cp;
				if (!hex4(cp))
					return false;
				if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i, 2, "\\u") == 0) {
					i += 2;
					unsigned low;
					if (!hex4(low))
						return false;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				}
				put_utf8(out, cp);
				break;
			}
			default:
				return false;
			}
		}
		return false;
	}

	bool value(Json &v)
	{
		if (++depth > 64)
			return false;
		space();
		if (i >= s.size())
			return false;
		bool ok = true;
		char c = s[i];
		if (c == '{') {
			v.type = Json::Object;
			i++;
			space();
			if (i < s.size() && s[i] == '}') {
				i++;
			} else {
				for (;;) {
					space();
					std::string key;
					if (!string(key))
						return false;
					space();
					if (i >= s.size() || s[i++] != ':')
						return false;
					if (!value(v.fields[key]))
						return false;
					space();
					if (i < s.size() && s[i] == ',') {
						i++;
						continue;
					}
					if (i < s.size() && s[i] == '}') {
						i++;
						break;
					}
					return false;
				}
			}
		} else if (c == '[') {
			v.type = Json::Array;
			i++;
			space();
			if (i < s.size() && s[i] == ']') {
				i++;
			} else {
				for (;;) {
					v.items.emplace_back();
					if (!value(v.items.back()))
						return false;
					space();
					if (i < s.size() && s[i] == ',') {
						i++;
						continue;
					}
					if (i < s.size() && s[i] == ']') {
						i++;
						break;
					}
					return false;
				}
			}
		} else if (c == '"') {
			v.type = Json::String;
			ok = string(v.string);
		} else if (literal("true")) {
			v.type = Json::Bool;
			v.boolean = true;
		} else if (literal("false")) {
			v.type = Json::Bool;
		} else if (literal("null")) {
			v.type = Json::Null;
		} else {
			char *end = nullptr;
			v.number = strtod(s.c_str() + i, &end);
			if (end == s.c_str() + i)
				return false;
			v.type = Json::Number;
			i = end - s.c_str();
		}
		depth--;
		return ok;
	}
};

}  // namespace

bool parse_json(const std::string &text, Json &out)
{
	out = Json();
	Parser p{text};
	if (!p.value(out))
		return false;
	p.space();
	return p.i == text.size();
}

}  // namespace ics
