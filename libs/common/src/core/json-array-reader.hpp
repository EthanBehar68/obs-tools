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

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace unified_chat {

// Splits a JSON array of objects that arrives in arbitrary chunks ("[{...}", ",{...}", "]") into its complete
// top-level objects as soon as each one closes. It only tracks nesting and strings; each object is parsed
// separately by the caller.
class JsonArrayReader {
public:
	static constexpr size_t kMaxObjectBytes = 4 * 1024 * 1024;

	std::vector<std::string> Feed(std::string_view data);
	bool Failed() const { return state_ == State::Failed; }
	bool Finished() const { return state_ == State::Finished; }

private:
	enum class State { BeforeArray, BetweenObjects, InObject, Finished, Failed };

	State state_ = State::BeforeArray;
	std::string current_;
	int depth_ = 0;
	bool inString_ = false;
	bool escaped_ = false;
};

} // namespace unified_chat
