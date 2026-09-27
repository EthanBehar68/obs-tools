/*
obs-unified-chat
Copyright (C) 2026 ebehar

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "json-array-reader.hpp"

namespace unified_chat {

static bool IsJsonSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::vector<std::string> JsonArrayReader::Feed(std::string_view data)
{
	std::vector<std::string> objects;
	for (size_t i = 0; i < data.size() && state_ != State::Failed; ++i) {
		const char c = data[i];
		switch (state_) {
		case State::BeforeArray:
			if (c == '[')
				state_ = State::BetweenObjects;
			else if (!IsJsonSpace(c))
				state_ = State::Failed;
			break;

		case State::BetweenObjects:
			if (c == '{') {
				state_ = State::InObject;
				current_.assign(1, c);
				depth_ = 1;
				inString_ = escaped_ = false;
			} else if (c == ']') {
				state_ = State::Finished;
			} else if (c != ',' && !IsJsonSpace(c)) {
				state_ = State::Failed;
			}
			break;

		case State::InObject: {
			// Copy the run up to the next character that can change nesting, rather than byte by byte.
			size_t end = i;
			while (end < data.size()) {
				const char d = data[end];
				if (inString_) {
					if (escaped_)
						escaped_ = false;
					else if (d == '\\')
						escaped_ = true;
					else if (d == '"')
						inString_ = false;
				} else if (d == '"') {
					inString_ = true;
				} else if (d == '{' || d == '[') {
					++depth_;
				} else if (d == '}' || d == ']') {
					if (--depth_ == 0)
						break;
				}
				++end;
			}
			const bool closed = end < data.size();
			current_.append(data.substr(i, (closed ? end + 1 : end) - i));
			if (current_.size() > kMaxObjectBytes) {
				state_ = State::Failed;
				current_.clear();
				break;
			}
			if (closed) {
				objects.push_back(std::move(current_));
				current_.clear();
				state_ = State::BetweenObjects;
			}
			i = closed ? end : data.size() - 1;
			break;
		}

		case State::Finished:
			if (!IsJsonSpace(c))
				state_ = State::Failed;
			break;

		case State::Failed:
			break;
		}
	}
	return objects;
}

} // namespace unified_chat
