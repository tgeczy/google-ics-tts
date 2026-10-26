// json.h -- a small JSON reader, enough for the GitHub releases API.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ics {

struct Json {
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool boolean = false;
	double number = 0;
	std::string string;  // UTF-8
	std::vector<Json> items;
	std::map<std::string, Json> fields;

	const Json &operator[](const char *key) const;  // a Null for a missing key
	bool truthy() const { return type == Bool ? boolean : type != Null; }
};

// false if text is not valid JSON.
bool parse_json(const std::string &text, Json &out);

}  // namespace ics
